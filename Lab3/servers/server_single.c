#define _GNU_SOURCE
#include "server_common.h"

/*
 * Overbug server, single-thread version.
 *
 * One thread repeats three steps forever:
 *
 *   1. read every queued input packet (never waits)
 *   2. update the game, plus --game-work-ms of simulated CPU work
 *   3. build and send one OBST packet, plus --tx-work-ms of simulated CPU work
 *
 * Nothing is shared, so nothing needs a lock. The price is that every step
 * waits for the others: a new update can only start after the previous
 * OBST packet has been sent.
 *
 *   ./server_single --cpus 4
 */

int main(int argc, char **argv)
{
	struct config cfg = parse_args(argc, argv, "CPU");
	struct sockaddr_in display;
	int input = open_input_socket(&cfg);
	int output = open_output_socket(&cfg, &display);
	struct inputs inputs = {0};
	uint8_t last_mask[4] = {0};
	struct game game;
	uint64_t last_update_ns, tick_id = 0;

	install_signal_handlers();
	start_role(&cfg, 0, "single");
	init_game(&game);
	last_update_ns = monotonic_ns();

	while (!stop_requested) {
		receive_inputs(input, last_mask, &inputs);
		advance_game(&game, &inputs, &last_update_ns);
		spin_cpu_ms(cfg.game_work_ms);
		++tick_id;

		spin_cpu_ms(cfg.tx_work_ms);
		send_obst_packet(output, &display, &game, tick_id);
	}

	close(input);
	close(output);
	return 0;
}
