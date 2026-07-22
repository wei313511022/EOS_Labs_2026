#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

/*
 * Overbug Lab 4 button-event test.
 *
 * This program opens /dev/overbug_buttons exactly as reader.c does: with
 * O_ASYNC, O_NONBLOCK, F_SETOWN, and SIGIO.  It records every debounced state
 * delivered by the driver and reports duplicate masks and inter-event gaps.
 *
 * It measures userspace delivery time, not electrical edge-to-IRQ latency.
 */

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_DEVICE "/dev/overbug_buttons"

#define OB_BTN_UP      0x01
#define OB_BTN_DOWN    0x02
#define OB_BTN_LEFT    0x04
#define OB_BTN_RIGHT   0x08
#define OB_BTN_ACTION  0x10
#define OB_BTN_SOLDER  0x20

struct button_name {
	uint8_t mask;
	const char *name;
};

struct event_stats {
	double start_ms;
	double last_event_ms;
	double min_gap_ms;
	double max_gap_ms;
	double total_gap_ms;
	unsigned long events;
	unsigned long duplicate_masks;
	uint8_t last_mask;
	int have_last_mask;
};

static const struct button_name button_names[] = {
	{ OB_BTN_UP, "UP" },
	{ OB_BTN_DOWN, "DOWN" },
	{ OB_BTN_LEFT, "LEFT" },
	{ OB_BTN_RIGHT, "RIGHT" },
	{ OB_BTN_ACTION, "ACTION" },
	{ OB_BTN_SOLDER, "SOLDER" },
};

static volatile sig_atomic_t sigio_received;
static volatile sig_atomic_t running = 1;

static void handle_sigio(int signum)
{
	(void)signum;
	sigio_received = 1;
}

static void handle_stop(int signum)
{
	(void)signum;
	running = 0;
}

static double monotonic_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  --device PATH     default: /dev/overbug_buttons\n"
		"  --duration SEC    stop after SEC seconds; 0 waits until Ctrl-C\n"
		"  --expect N        fail unless at least N state events arrive\n"
		"\n"
		"Stop reader.c before running this test: the Lab 4 driver permits one reader.\n",
		argv0);
}

static int parse_button_mask(const char *line, uint8_t *mask)
{
	unsigned int value;

	if (sscanf(line, "OBTN %x", &value) != 1 || value > 0x3f)
		return -1;
	*mask = (uint8_t)value;
	return 0;
}

static int read_button_mask(int fd, uint8_t *mask)
{
	char line[64];
	ssize_t n;

	n = read(fd, line, sizeof(line) - 1);
	if (n < 0)
		return -1;
	if (n == 0) {
		errno = EIO;
		return -1;
	}
	line[n] = '\0';
	if (parse_button_mask(line, mask) != 0) {
		errno = EPROTO;
		return -1;
	}
	return 0;
}

static void print_changes(uint8_t before, uint8_t after, int have_before)
{
	size_t i;

	if (!have_before) {
		printf(" first state");
		return;
	}
	for (i = 0; i < sizeof(button_names) / sizeof(button_names[0]); ++i) {
		uint8_t changed = before ^ after;

		if (changed & button_names[i].mask) {
			printf(" %c%s", (after & button_names[i].mask) ? '+' : '-',
			       button_names[i].name);
		}
	}
}

static void record_event(struct event_stats *stats, uint8_t mask)
{
	double now = monotonic_ms();
	double elapsed = now - stats->start_ms;
	double gap = 0.0;
	int duplicate = stats->have_last_mask && mask == stats->last_mask;

	if (stats->events > 0) {
		gap = now - stats->last_event_ms;
		if (gap < stats->min_gap_ms)
			stats->min_gap_ms = gap;
		if (gap > stats->max_gap_ms)
			stats->max_gap_ms = gap;
		stats->total_gap_ms += gap;
	}

	printf("%10.3f ms  OBTN %02X", elapsed, mask);
	print_changes(stats->last_mask, mask, stats->have_last_mask);
	if (duplicate) {
		printf("  DUPLICATE");
		stats->duplicate_masks += 1;
	}
	if (stats->events > 0)
		printf("  gap %.3f ms", gap);
	putchar('\n');

	stats->last_mask = mask;
	stats->have_last_mask = 1;
	stats->last_event_ms = now;
	stats->events += 1;
}

/* Drain all state changes already made readable by the SIGIO notification. */
static int drain_device(int fd, struct event_stats *stats)
{
	for (;;) {
		uint8_t mask;

		if (read_button_mask(fd, &mask) != 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				return 0;
			return -1;
		}
		record_event(stats, mask);
	}
}

static int install_handler(int signum, void (*handler)(int))
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_handler = handler;
	sigemptyset(&action.sa_mask);
	return sigaction(signum, &action, NULL);
}

static int parse_unsigned(const char *text, unsigned long *value)
{
	char *end;

	errno = 0;
	*value = strtoul(text, &end, 10);
	return errno == 0 && *text != '\0' && *end == '\0' ? 0 : -1;
}

static int parse_duration(const char *text, double *value)
{
	char *end;

	errno = 0;
	*value = strtod(text, &end);
	return errno == 0 && *text != '\0' && *end == '\0' && *value >= 0.0 ? 0 : -1;
}

int main(int argc, char **argv)
{
	const char *device_path = DEFAULT_DEVICE;
	double duration_seconds = 0.0;
	unsigned long expected_events = 0;
	int fd = -1;
	int flags;
	int i;
	int exit_code = 1;
	struct event_stats stats = { .min_gap_ms = DBL_MAX };
	sigset_t sigio_set;
	sigset_t previous_mask;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
			device_path = argv[++i];
		} else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
			if (parse_duration(argv[++i], &duration_seconds) != 0) {
				usage(argv[0]);
				return 2;
			}
		} else if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc) {
			if (parse_unsigned(argv[++i], &expected_events) != 0) {
				usage(argv[0]);
				return 2;
			}
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (install_handler(SIGIO, handle_sigio) != 0 ||
	    install_handler(SIGINT, handle_stop) != 0 ||
	    install_handler(SIGTERM, handle_stop) != 0) {
		perror("sigaction");
		return 1;
	}

	/* Block SIGIO before enabling O_ASYNC, closing the check/sleep race. */
	sigemptyset(&sigio_set);
	sigaddset(&sigio_set, SIGIO);
	if (sigprocmask(SIG_BLOCK, &sigio_set, &previous_mask) != 0) {
		perror("sigprocmask");
		return 1;
	}

	fd = open(device_path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open device");
		goto cleanup;
	}
	if (fcntl(fd, F_SETOWN, getpid()) < 0) {
		perror("fcntl F_SETOWN");
		goto cleanup;
	}
	flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_ASYNC | O_NONBLOCK) < 0) {
		perror("fcntl O_ASYNC");
		goto cleanup;
	}

	stats.start_ms = monotonic_ms();
	printf("Testing debounced SIGIO events from %s. Press buttons; Ctrl-C stops.\n",
	       device_path);
	while (running) {
		double elapsed = monotonic_ms() - stats.start_ms;

		if (duration_seconds > 0.0 && elapsed >= duration_seconds * 1000.0)
			break;
		sigio_received = 0;
		if (drain_device(fd, &stats) != 0) {
			perror("read/parse buttons");
			goto cleanup;
		}

		if (duration_seconds > 0.0) {
			double remaining_ms = duration_seconds * 1000.0 -
				(monotonic_ms() - stats.start_ms);
			struct timespec timeout;

			if (remaining_ms <= 0.0)
				break;
			timeout.tv_sec = (time_t)(remaining_ms / 1000.0);
			timeout.tv_nsec = (long)((remaining_ms - timeout.tv_sec * 1000.0) * 1000000.0);
			(void)pselect(0, NULL, NULL, NULL, &timeout, &previous_mask);
		} else {
			while (running && !sigio_received)
				sigsuspend(&previous_mask);
		}
	}

	printf("\nSummary\n");
	printf("  events:          %lu\n", stats.events);
	printf("  duplicate masks: %lu\n", stats.duplicate_masks);
	if (stats.events > 1) {
		printf("  gap min/avg/max: %.3f / %.3f / %.3f ms\n",
		       stats.min_gap_ms,
		       stats.total_gap_ms / (double)(stats.events - 1),
		       stats.max_gap_ms);
	}
	if (expected_events > 0 && stats.events < expected_events) {
		fprintf(stderr, "Expected at least %lu events, received %lu\n",
			expected_events, stats.events);
		goto cleanup;
	}
	if (stats.duplicate_masks != 0) {
		fprintf(stderr, "Duplicate masks indicate the driver delivered an unchanged state\n");
		goto cleanup;
	}
	exit_code = 0;

cleanup:
	if (fd >= 0)
		close(fd);
	sigprocmask(SIG_SETMASK, &previous_mask, NULL);
	return exit_code;
}
