#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

/*
 * Overbug Lab 4 reader.
 *
 * The Lab 2 reader sampled /dev/overbug_buttons at a fixed rate.  This
 * version enables O_ASYNC and waits for SIGIO from the Lab 4 driver instead.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "overbug_protocol.h"

#define DEFAULT_DEVICE "/dev/overbug_buttons"
#define DEFAULT_PORT 17666

static volatile sig_atomic_t sigio_received;

static void handle_sigio(int signum)
{
	(void)signum;
	/* The handler must be async-signal-safe: record notification only. */
	sigio_received = 1;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s --server PC_IP [options]\n"
		"\n"
		"Options:\n"
		"  --device PATH     default: /dev/overbug_buttons\n"
		"  --port PORT       default: 17666\n"
		"  --player ID       default: 1\n"
		"\n"
		"The Lab 4 driver sends SIGIO for debounced button-state changes.\n"
		"Driver read format: OBTN XX\\n\n",
		argv0);
}

static int parse_button_mask(const char *line, uint8_t *mask)
{
	unsigned int value;

	if (sscanf(line, "OBTN %x", &value) != 1)
		return -1;
	if (value > 0x3f)
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

static int send_mask(int sock, const struct sockaddr_in *server_addr,
			     int player_id, uint8_t mask)
{
	struct ob_input_packet packet;

	memcpy(packet.magic, "OBIN", 4);
	packet.player_id = (uint8_t)player_id;
	packet.button_mask = mask;
	return sendto(sock, &packet, sizeof(packet), 0,
		      (const struct sockaddr *)server_addr, sizeof(*server_addr));
}

/* Drain every state the driver has made readable while SIGIO is blocked. */
static int drain_device(int fd, int sock, const struct sockaddr_in *server_addr,
			int player_id)
{
	for (;;) {
		uint8_t mask;

		if (read_button_mask(fd, &mask) != 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			return -1;
		}
		if (send_mask(sock, server_addr, player_id, mask) < 0)
			perror("sendto");
		else
			printf("player %d: OBTN %02X -> OBIN\n", player_id, mask);
	}
}

int main(int argc, char **argv)
{
	const char *device_path = DEFAULT_DEVICE;
	const char *server_ip = NULL;
	int port = DEFAULT_PORT;
	int player_id = 1;
	int fd;
	int sock;
	int flags;
	struct sockaddr_in server_addr;
	struct sigaction action;
	sigset_t sigio_set;
	sigset_t previous_mask;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
			device_path = argv[++i];
		} else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
			server_ip = argv[++i];
		} else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--player") == 0 && i + 1 < argc) {
			player_id = atoi(argv[++i]);
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	if (server_ip == NULL || player_id < 1 || player_id > 4 ||
	    port < 1 || port > 65535) {
		usage(argv[0]);
		return 2;
	}

	/* Block SIGIO before enabling O_ASYNC, so no notification is lost. */
	sigemptyset(&sigio_set);
	sigaddset(&sigio_set, SIGIO);
	if (sigprocmask(SIG_BLOCK, &sigio_set, &previous_mask) != 0) {
		perror("sigprocmask");
		return 1;
	}

	memset(&action, 0, sizeof(action));
	action.sa_handler = handle_sigio;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGIO, &action, NULL) != 0) {
		perror("sigaction SIGIO");
		return 1;
	}

	fd = open(device_path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open device");
		return 1;
	}
	if (fcntl(fd, F_SETOWN, getpid()) < 0) {
		perror("fcntl F_SETOWN");
		close(fd);
		return 1;
	}
	flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_ASYNC | O_NONBLOCK) < 0) {
		perror("fcntl O_ASYNC");
		close(fd);
		return 1;
	}

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		close(fd);
		return 1;
	}
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
		fprintf(stderr, "Invalid server IP: %s\n", server_ip);
		close(sock);
		close(fd);
		return 1;
	}

	/* Clear a stale movement state if the reader restarts during a game. */
	if (send_mask(sock, &server_addr, player_id, 0) < 0)
		perror("initial sendto");

	printf("Waiting for SIGIO button changes from %s; sending player %d to %s:%d\n",
	       device_path, player_id, server_ip, port);
	for (;;) {
		/* SIGIO stays blocked while we drain, closing the check/sleep race. */
		sigio_received = 0;
		if (drain_device(fd, sock, &server_addr, player_id) != 0) {
			perror("read/parse buttons");
			break;
		}

		while (!sigio_received)
			sigsuspend(&previous_mask);
	}

	sigprocmask(SIG_SETMASK, &previous_mask, NULL);
	close(sock);
	close(fd);
	return 1;
}
