#define _GNU_SOURCE
#include "server_common.h"

/*
 * Overbug server, three-thread pipeline version.
 *
 *   rx thread    waits for input packets and puts the newest state of each
 *                player, and every button press in arrival order, into the
 *                input mailbox
 *   game thread  takes the inputs, updates the game (+ --game-work-ms of
 *                simulated work) and puts a copy of the game into the
 *                state mailbox
 *   tx thread    waits for a game state, then builds and sends it as an
 *                OBST packet (+ --tx-work-ms of simulated work)
 *
 *          input mailbox              state mailbox
 *   rx  ------------------>  game  -------------------->  tx  --> display
 *
 * The state mailbox holds only the newest game state: a new one replaces an
 * old one that was not taken yet. The input mailbox keeps the newest button
 * state of each player and queues the presses until the game takes them.
 * Only the game thread touches the live game; the tx thread works on its own
 * copy. The two mailboxes are the only shared data, and each has its own
 * mutex.
 *
 * Where the threads run is decided at start-up, the code is the same:
 *
 *   ./server_threads --cpus 4        all three threads share CPU 4
 *   ./server_threads --cpus 4,5,6    rx on CPU 4, game on 5, tx on 6
 */

static struct config cfg;
static struct sockaddr_in display;
static int output_fd;

static struct {
	pthread_mutex_t lock;
	struct inputs inputs;		/* written by rx, taken by game */
} input_mailbox = {.lock = PTHREAD_MUTEX_INITIALIZER};

static struct {
	pthread_mutex_t lock;
	pthread_cond_t ready;		/* signalled when a state is put in */
	struct game game;		/* copy of the game, written by game */
	uint64_t tick_id;
	bool full;			/* a state is waiting for tx */
	bool closed;			/* game thread has stopped */
} state_mailbox = {.lock = PTHREAD_MUTEX_INITIALIZER, .ready = PTHREAD_COND_INITIALIZER};

static void *game_thread(void *arg)
{
	struct game game;
	struct inputs inputs;
	uint64_t last_update_ns, tick_id = 0;

	(void)arg;
	start_role(&cfg, 1, "game");
	init_game(&game);
	last_update_ns = monotonic_ns();

	while (!stop_requested) {
		check(pthread_mutex_lock(&input_mailbox.lock), "input lock");
		inputs = input_mailbox.inputs;
		for (int i = 0; i < 4; ++i)
			input_mailbox.inputs.fresh[i] = false;
		input_mailbox.inputs.press_count = 0;
		check(pthread_mutex_unlock(&input_mailbox.lock), "input unlock");

		advance_game(&game, &inputs, &last_update_ns);
		spin_cpu_ms(cfg.game_work_ms);
		++tick_id;

		check(pthread_mutex_lock(&state_mailbox.lock), "state lock");
		state_mailbox.game = game;
		state_mailbox.tick_id = tick_id;
		state_mailbox.full = true;
		check(pthread_cond_signal(&state_mailbox.ready), "state signal");
		check(pthread_mutex_unlock(&state_mailbox.lock), "state unlock");
	}

	check(pthread_mutex_lock(&state_mailbox.lock), "state lock");
	state_mailbox.closed = true;
	check(pthread_cond_signal(&state_mailbox.ready), "state signal");
	check(pthread_mutex_unlock(&state_mailbox.lock), "state unlock");
	return NULL;
}

static void *tx_thread(void *arg)
{
	static struct game game;	/* large; keep it off the thread stack */
	uint64_t tick_id;

	(void)arg;
	start_role(&cfg, 2, "tx");
	for (;;) {
		check(pthread_mutex_lock(&state_mailbox.lock), "state lock");
		while (!state_mailbox.full && !state_mailbox.closed)
			check(pthread_cond_wait(&state_mailbox.ready, &state_mailbox.lock),
			      "state wait");
		if (!state_mailbox.full) {
			check(pthread_mutex_unlock(&state_mailbox.lock), "state unlock");
			return NULL;
		}
		game = state_mailbox.game;
		tick_id = state_mailbox.tick_id;
		state_mailbox.full = false;
		check(pthread_mutex_unlock(&state_mailbox.lock), "state unlock");

		spin_cpu_ms(cfg.tx_work_ms);
		send_obst_packet(output_fd, &display, &game, tick_id);
	}
}

int main(int argc, char **argv)
{
	pthread_t game, tx;
	uint8_t last_mask[4] = {0};
	int input;

	cfg = parse_args(argc, argv, "RX,GAME,TX");
	input = open_input_socket(&cfg);
	output_fd = open_output_socket(&cfg, &display);
	install_signal_handlers();

	/* The main thread becomes the rx thread. */
	start_role(&cfg, 0, "rx");
	check(pthread_create(&game, NULL, game_thread, NULL), "create game thread");
	check(pthread_create(&tx, NULL, tx_thread, NULL), "create tx thread");

	while (!stop_requested) {
		struct inputs received = {0};

		if (!wait_for_input(input, 100))
			continue;
		receive_inputs(input, last_mask, &received);
		check(pthread_mutex_lock(&input_mailbox.lock), "input lock");
		merge_inputs(&input_mailbox.inputs, &received);
		check(pthread_mutex_unlock(&input_mailbox.lock), "input unlock");
	}

	check(pthread_join(game, NULL), "join game thread");
	check(pthread_join(tx, NULL), "join tx thread");
	close(input);
	close(output_fd);
	return 0;
}
