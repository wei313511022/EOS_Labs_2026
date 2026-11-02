#ifndef OVERBUG_SERVER_COMMON_H
#define OVERBUG_SERVER_COMMON_H

/* Plumbing shared by server_single.c and server_threads.c: options, sockets,
 * input decoding, OBST packet output, CPU pinning and the simulated work.
 * The two servers do the same steps; they differ only in which thread runs
 * each step. Define _GNU_SOURCE before including this header. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "game_core.h"

#define MAX_ROLES 3

struct config {
	int cpus[MAX_ROLES];	/* --cpus: one ID for every thread, or one per thread */
	int cpu_count;		/* 0 = let the scheduler choose */
	double game_work_ms;	/* simulated work per game update */
	double tx_work_ms;	/* simulated work per OBST packet */
	int input_port, display_port;
	const char *display_host;
};

#define PRESS_BUTTONS (OB_BTN_ACTION | OB_BTN_SOLDER)
#define MAX_PRESSES 256

/* A button that went down: player index 0-3 and the PRESS_BUTTONS bits. */
struct press {
	uint8_t player;
	uint8_t buttons;
};

/* What arrived since the game last took the inputs. mask[i] is the latest
 * button state of player i+1 (used for movement), and fresh[i] is set when a
 * packet for that player arrived. presses[] lists every ACTION or SOLDER
 * press in the order its packet arrived, so a short tap is never lost and
 * the first press to arrive acts first. */
struct inputs {
	uint8_t mask[4];
	bool fresh[4];
	struct press presses[MAX_PRESSES];
	int press_count;
};

static volatile sig_atomic_t stop_requested;

static inline void on_stop_signal(int signum)
{
	(void)signum;
	stop_requested = 1;
}

static inline void die(const char *what)
{
	perror(what);
	exit(1);
}

static inline void check(int rc, const char *what)
{
	if (rc) {
		fprintf(stderr, "%s: %s\n", what, strerror(rc));
		exit(1);
	}
}

static inline uint64_t clock_ns(clockid_t clock)
{
	struct timespec t;

	if (clock_gettime(clock, &t))
		die("clock_gettime");
	return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static inline uint64_t monotonic_ns(void)
{
	return clock_ns(CLOCK_MONOTONIC);
}

/* Simulated CPU-heavy work (physics, serialization, ...). The amount is
 * counted in this thread's own CPU time, so time spent waiting while another
 * thread uses the core does not count as work done. The CPU clock is a
 * system call, so it is read only between short chunks of arithmetic. */
static inline void spin_cpu_ms(double ms)
{
	static _Thread_local volatile uint64_t sink;	/* per thread: no shared cache line */
	uint64_t x = sink | 1;
	uint64_t end = clock_ns(CLOCK_THREAD_CPUTIME_ID) + (uint64_t)(ms * 1e6);

	while (clock_ns(CLOCK_THREAD_CPUTIME_ID) < end) {
		for (int i = 0; i < 50000; ++i) {
			x ^= x << 13;
			x ^= x >> 7;
			x ^= x << 17;
		}
		sink = x;
	}
}

static inline double parse_number(const char *text, double lo, double hi, const char *name)
{
	char *end;
	double value;

	errno = 0;
	value = strtod(text, &end);
	if (errno || !*text || *end || !isfinite(value) || value < lo || value > hi) {
		fprintf(stderr, "Invalid %s: %s\n", name, text);
		exit(2);
	}
	return value;
}

static inline void parse_cpus(struct config *cfg, const char *text)
{
	char *copy = strdup(text), *save = NULL;

	if (!copy)
		die("strdup");
	for (char *s = strtok_r(copy, ",", &save); s; s = strtok_r(NULL, ",", &save)) {
		if (cfg->cpu_count == MAX_ROLES) {
			fprintf(stderr, "--cpus takes at most %d CPU IDs\n", MAX_ROLES);
			exit(2);
		}
		cfg->cpus[cfg->cpu_count++] = (int)parse_number(s, 0, CPU_SETSIZE - 1, "--cpus");
	}
	free(copy);
}

/* roles: "CPU" for server_single, "RX,GAME,TX" for server_threads, ...
 * One CPU ID per comma-separated role, or one ID for all of them. */
static inline struct config parse_args(int argc, char **argv, const char *roles)
{
	int role_count = 1;
	struct config cfg = {
		.game_work_ms = 50, .tx_work_ms = 50,
		.input_port = 17666, .display_port = 17667, .display_host = "127.0.0.1",
	};

	for (const char *c = roles; *c; ++c)
		role_count += *c == ',';

	for (int i = 1; i < argc; ++i) {
		const char *key = argv[i];

		if (!strcmp(key, "--help")) {
			printf("Usage: %s [--cpus %s%s] [--game-work-ms 50] [--tx-work-ms 50]\n"
			       "  [--input-port 17666] [--display-host 127.0.0.1] [--display-port 17667]\n",
			       argv[0], role_count == 1 ? "" : "CPU | ", roles);
			exit(0);
		}
		if (++i == argc) {
			fprintf(stderr, "Missing value for %s\n", key);
			exit(2);
		}
		if (!strcmp(key, "--cpus"))
			parse_cpus(&cfg, argv[i]);
		else if (!strcmp(key, "--game-work-ms"))
			cfg.game_work_ms = parse_number(argv[i], 0, 1000, key);
		else if (!strcmp(key, "--tx-work-ms"))
			cfg.tx_work_ms = parse_number(argv[i], 0, 1000, key);
		else if (!strcmp(key, "--input-port"))
			cfg.input_port = (int)parse_number(argv[i], 1, 65535, key);
		else if (!strcmp(key, "--display-port"))
			cfg.display_port = (int)parse_number(argv[i], 1, 65535, key);
		else if (!strcmp(key, "--display-host"))
			cfg.display_host = argv[i];
		else {
			fprintf(stderr, "Unknown option: %s (try --help)\n", key);
			exit(2);
		}
	}
	if (cfg.cpu_count > 1 && cfg.cpu_count != role_count) {
		if (role_count == 1)
			fprintf(stderr, "--cpus needs one CPU ID\n");
		else
			fprintf(stderr, "--cpus needs one CPU ID or %d (%s)\n", role_count, roles);
		exit(2);
	}
	return cfg;
}

/* No SA_RESTART, so a blocked poll() returns early with EINTR on Ctrl-C. */
static inline void install_signal_handlers(void)
{
	struct sigaction action = {0};

	action.sa_handler = on_stop_signal;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL))
		die("sigaction");
}

/* Pins the calling thread to the CPU chosen for its role (its index in roles)
 * and prints its thread ID so it can be found in ps, top and /proc. */
static inline void start_role(const struct config *cfg, int role, const char *name)
{
	int cpu = -1;

	if (cfg->cpu_count) {
		cpu_set_t set;

		cpu = cfg->cpus[cfg->cpu_count == 1 ? 0 : role];
		CPU_ZERO(&set);
		CPU_SET(cpu, &set);
		check(pthread_setaffinity_np(pthread_self(), sizeof set, &set), "pin thread");
	}
	/* Name worker threads for top -H; renaming the main thread would also
	 * rename the process and break pgrep server_threads. */
	if (gettid() != getpid())
		pthread_setname_np(pthread_self(), name);
	if (cpu < 0)
		fprintf(stderr, "thread %-6s pid %d tid %d cpu any\n", name, getpid(), gettid());
	else
		fprintf(stderr, "thread %-6s pid %d tid %d cpu %d\n", name, getpid(), gettid(), cpu);
}

static inline int open_input_socket(const struct config *cfg)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)cfg->input_port),
		.sin_addr = {.s_addr = htonl(INADDR_ANY)},
	};
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	int flags;

	if (fd < 0)
		die("socket");
	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		die("fcntl");
	if (bind(fd, (struct sockaddr *)&address, sizeof address))
		die("bind input port");
	return fd;
}

static inline int open_output_socket(const struct config *cfg, struct sockaddr_in *address)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		die("socket");
	*address = (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)cfg->display_port),
	};
	if (inet_pton(AF_INET, cfg->display_host, &address->sin_addr) != 1) {
		fprintf(stderr, "--display-host must be an IPv4 address\n");
		exit(2);
	}
	return fd;
}

/* Blocks until input arrives, a signal arrives, or timeout_ms passes. */
static inline bool wait_for_input(int fd, int timeout_ms)
{
	struct pollfd p = {.fd = fd, .events = POLLIN};
	int rc = poll(&p, 1, timeout_ms);

	if (rc < 0 && errno != EINTR)
		die("poll");
	return rc > 0;
}

/* Queues one press. If the queue is full the new press is dropped, so the
 * presses that arrived earlier keep their place. */
static inline void add_press(struct inputs *in, struct press press)
{
	if (in->press_count < MAX_PRESSES)
		in->presses[in->press_count++] = press;
}

/* Reads every queued OBIN packet without blocking, in arrival order.
 * last_mask[i] is the previous packet of player i+1: a button set now but
 * not then was just pressed. */
static inline void receive_inputs(int fd, uint8_t last_mask[4], struct inputs *in)
{
	for (;;) {
		unsigned char packet[8];
		ssize_t len = recv(fd, packet, sizeof packet, 0);

		if (len < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				return;
			die("recv");
		}
		if (len != 6 || memcmp(packet, "OBIN", 4) || packet[4] < 1 || packet[4] > 4 ||
		    (packet[5] & 0xc0))
			continue;
		int i = packet[4] - 1;
		uint8_t pressed = packet[5] & ~last_mask[i] & PRESS_BUTTONS;

		if (pressed)
			add_press(in, (struct press){.player = (uint8_t)i, .buttons = pressed});
		last_mask[i] = packet[5];
		in->mask[i] = packet[5];
		in->fresh[i] = true;
	}
}

/* Adds what one receive collected to inputs the game has not taken yet. */
static inline void merge_inputs(struct inputs *into, const struct inputs *from)
{
	for (int i = 0; i < 4; ++i) {
		if (from->fresh[i]) {
			into->mask[i] = from->mask[i];
			into->fresh[i] = true;
		}
	}
	for (int k = 0; k < from->press_count; ++k)
		add_press(into, from->presses[k]);
}

/* One game update, then the inputs are used up:
 *   1. every player moves by the real time since the last update, along the
 *      latest direction buttons
 *   2. the presses run in arrival order, so when two players want the same
 *      thing the one whose packet arrived first gets it
 * Each player acts at most once per update and later presses of the same
 * player are dropped: one OBST packet is sent per update, so two actions in one
 * update (pick up and put back) would never be seen. */
static inline void advance_game(struct game *game, struct inputs *in, uint64_t *last_update_ns)
{
	uint64_t now = monotonic_ns();
	bool acted[4] = {false};

	for (int i = 0; i < 4; ++i) {
		if (in->fresh[i])
			game->players[i].input_mask = in->mask[i];
		in->fresh[i] = false;
	}
	update_game(game, (float)((now - *last_update_ns) / 1e9));
	*last_update_ns = now;

	for (int k = 0; k < in->press_count; ++k) {
		struct press press = in->presses[k];

		if (acted[press.player])
			continue;
		acted[press.player] = true;
		press_buttons(game, &game->players[press.player], press.buttons);
	}
	in->press_count = 0;
}

/* Sends one OBST packet. tick_id counts game updates, so a gap in the
 * received tick_ids means updates that were never sent or never arrived. */
static inline void send_obst_packet(int fd, const struct sockaddr_in *address,
			  const struct game *game, uint64_t tick_id)
{
	char json[OBST_BYTES];
	size_t len = build_obst_json(game, json, sizeof json);

	if (!len || json[len - 1] != '}') {
		fprintf(stderr, "OBST packet too large\n");
		return;
	}
	--len;
	json_append(json, sizeof json, &len, ",\"tick_id\":%" PRIu64 "}", tick_id);
	if (sendto(fd, json, len, 0, (const struct sockaddr *)address, sizeof *address) < 0 &&
	    errno != EINTR)
		perror("sendto");
}

#endif
