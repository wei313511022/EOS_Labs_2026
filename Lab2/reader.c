#define _POSIX_C_SOURCE 200809L

/*
 * Overbug Lab 2 reader.
 *
 * Runs on Raspberry Pi:
 *   ./reader --server 192.168.1.20 --player 1
 *
 * Reads /dev/overbug_buttons and sends UDP packets to server.c.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "overbug_protocol.h"

#define DEFAULT_DEVICE "/dev/overbug_buttons"
#define DEFAULT_PORT 17666
#define DEFAULT_HZ 60

#define OB_BTN_UP      0x01
#define OB_BTN_DOWN    0x02
#define OB_BTN_LEFT    0x04
#define OB_BTN_RIGHT   0x08
#define OB_BTN_ACTION  0x10
#define OB_BTN_SOLDER  0x20

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s --server PC_IP [options]\n"
		"\n"
		"Options:\n"
		"  --device PATH     default: /dev/overbug_buttons\n"
		"  --port PORT       default: 17666\n"
		"  --player ID       default: 1\n"
		"  --hz HZ           default: 60\n"
		"\n"
		"Driver read format: OBTN XX\\n\n",
		argv0);
}

static void sleep_for_hz(int hz)
{
	struct timespec ts;

	if (hz <= 0)
		hz = DEFAULT_HZ;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000000000L / hz;
	nanosleep(&ts, NULL);
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
	return parse_button_mask(line, mask);
}

int main(int argc, char **argv)
{
	const char *device_path = DEFAULT_DEVICE;
	const char *server_ip = NULL;
	int port = DEFAULT_PORT;
	int player_id = 1;
	int hz = DEFAULT_HZ;
	int fd;
	int sock;
	struct sockaddr_in server_addr;
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
		} else if (strcmp(argv[i], "--hz") == 0 && i + 1 < argc) {
			hz = atoi(argv[++i]);
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	if (server_ip == NULL || player_id < 1 || player_id > 4 || port <= 0) {
		usage(argv[0]);
		return 2;
	}

	fd = open(device_path, O_RDONLY);
	if (fd < 0) {
		perror("open device");
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

	printf("Sending player %d device states from %s to %s:%d at %d Hz\n",
	       player_id, device_path, server_ip, port, hz);

	for (;;) {
		uint8_t mask = 0;
		struct ob_input_packet packet;

		if (read_button_mask(fd, &mask) != 0) {
			perror("read/parse buttons");
			sleep_for_hz(5);
			continue;
		}

		memcpy(packet.magic, "OBIN", 4);
		packet.player_id = (uint8_t)player_id;
		packet.button_mask = mask;

		if (sendto(sock, &packet, sizeof(packet), 0,
			   (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
			perror("sendto");
		}

		sleep_for_hz(hz);
	}

	close(sock);
	close(fd);
	return 0;
}
