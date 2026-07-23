#define _POSIX_C_SOURCE 200809L

/*
 * Overbug Lab 3 starter server.
 *
 * This is the Lab 2 single-player baseline. Refactor it into a Linux/POSIX
 * server for two to four concurrent players. Choose and justify an
 * architecture that safely coordinates input, game updates, and display
 * output. Keep the OBIN input-packet contract unchanged.
 */

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "overbug_protocol.h"
#include "overbug_world.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET ob_socket_t;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int ob_socket_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSE_SOCKET close
#endif

#define INPUT_PORT_DEFAULT 17666
#define DISPLAY_PORT_DEFAULT 17667
#define DISPLAY_HOST_DEFAULT "127.0.0.1"

#define PLAYER_RADIUS 18.0f
#define PLAYER_SPEED 185.0f
#define INTERACT_DISTANCE 54.0f
#define ROUND_SECONDS 180.0f
#define SOLDER_SECONDS 2.0f
#define INSPECTION_SECONDS 4.0f
#define ORDER_COUNT 3
#define MAX_STATIONS OB_FACILITY_COUNT
#define SNAPSHOT_BYTES 32768

#define OB_BTN_UP      0x01
#define OB_BTN_DOWN    0x02
#define OB_BTN_LEFT    0x04
#define OB_BTN_RIGHT   0x08
#define OB_BTN_ACTION  0x10
#define OB_BTN_SOLDER  0x20

enum item_kind {
	ITEM_NONE = 0,
	ITEM_MATERIAL,
	ITEM_BOARD
};

enum board_state {
	BOARD_RAW = 0,
	BOARD_SOLDERING,
	BOARD_READY_TO_TEST,
	BOARD_PASSED,
	BOARD_FAILED
};

struct board {
	int id;
	int counts[COMP_COUNT];
	enum board_state state;
	int locked_order_id;
};

struct item {
	enum item_kind kind;
	enum component component;
	struct board board;
};

struct order {
	int id;
	int req[COMP_COUNT];
	int locked_board_id;
};

struct station {
	const char *id;
	enum station_type type;
	float x;
	float y;
	float w;
	float h;
	enum component component;
	struct item item;
	struct item board_item;
	struct item pending_material;
	float progress;
};

struct player {
	int id;
	float x;
	float y;
	uint8_t input_mask;
	uint8_t prev_input_mask;
	struct item held;
};

struct game {
	/*
	 * Lab 3 starting point: replace this field with storage for two to four
	 * active players, coordinated by the design you choose.
	 */
	struct player player;
	struct station stations[MAX_STATIONS];
	int station_count;
	struct order orders[ORDER_COUNT];
	int next_order_id;
	int next_board_id;
	int score;
	int completed;
	int recycled;
	float remaining_seconds;
	bool game_over;
};

static volatile sig_atomic_t running = 1;

static void handle_signal(int signum)
{
	(void)signum;
	running = 0;
}

static const char *component_name(enum component component)
{
	switch (component) {
	case COMP_RESISTOR:
		return "resistor";
	case COMP_CAPACITOR:
		return "capacitor";
	case COMP_INDUCTOR:
		return "inductor";
	default:
		return "unknown";
	}
}

static const char *board_state_name(enum board_state state)
{
	switch (state) {
	case BOARD_RAW:
		return "raw";
	case BOARD_SOLDERING:
		return "soldering";
	case BOARD_READY_TO_TEST:
		return "ready_to_test";
	case BOARD_PASSED:
		return "passed";
	case BOARD_FAILED:
		return "failed";
	default:
		return "unknown";
	}
}

static struct item item_none(void)
{
	struct item item;

	memset(&item, 0, sizeof(item));
	item.kind = ITEM_NONE;
	return item;
}

static struct item make_material(enum component component)
{
	struct item item = item_none();

	item.kind = ITEM_MATERIAL;
	item.component = component;
	return item;
}

static struct item make_board(struct game *game)
{
	struct item item = item_none();

	item.kind = ITEM_BOARD;
	item.board.id = game->next_board_id++;
	item.board.state = BOARD_RAW;
	item.board.locked_order_id = 0;
	return item;
}

static bool item_is_board(const struct item *item)
{
	return item->kind == ITEM_BOARD;
}

static bool item_is_empty(const struct item *item)
{
	return item->kind == ITEM_NONE;
}

static void clear_item(struct item *item)
{
	*item = item_none();
}

static float clampf(float value, float low, float high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static float distance_to_rect(float px, float py, const struct station *station)
{
	float closest_x = clampf(px, station->x, station->x + station->w);
	float closest_y = clampf(py, station->y, station->y + station->h);
	float dx = px - closest_x;
	float dy = py - closest_y;

	return sqrtf(dx * dx + dy * dy);
}

static bool player_hits_station(const struct player *player, const struct game *game)
{
	int i;

	for (i = 0; i < game->station_count; ++i) {
		if (distance_to_rect(player->x, player->y, &game->stations[i]) < PLAYER_RADIUS)
			return true;
	}
	return false;
}

static struct order generate_order(struct game *game)
{
	struct order order;
	int total;

	memset(&order, 0, sizeof(order));
	order.id = game->next_order_id++;
	do {
		order.req[0] = rand() % 3;
		order.req[1] = rand() % 3;
		order.req[2] = rand() % 3;
		total = order.req[0] + order.req[1] + order.req[2];
	} while (total < 1 || total > 4);
	return order;
}

static void add_station(
	struct game *game,
	const char *id,
	enum station_type type,
	float x,
	float y,
	float w,
	float h,
	enum component component)
{
	struct station *station;

	if (game->station_count >= MAX_STATIONS)
		return;
	station = &game->stations[game->station_count++];
	memset(station, 0, sizeof(*station));
	station->id = id;
	station->type = type;
	station->x = x;
	station->y = y;
	station->w = w;
	station->h = h;
	station->component = component;
	station->item = item_none();
	station->board_item = item_none();
	station->pending_material = item_none();
}

static void init_game(struct game *game)
{
	int i;

	memset(game, 0, sizeof(*game));
	game->next_order_id = 1;
	game->next_board_id = 1;
	game->remaining_seconds = ROUND_SECONDS;

	game->player.id = 1;
	game->player.x = 560.0f;
	game->player.y = 370.0f;
	game->player.held = item_none();

#define OVERBUG_ADD_FACILITY(id, type, x, y, w, h, component) \
	add_station(game, id, type, x, y, w, h, component);
	OB_FACILITY_LAYOUT(OVERBUG_ADD_FACILITY)
#undef OVERBUG_ADD_FACILITY

	srand(7);
	for (i = 0; i < ORDER_COUNT; ++i)
		game->orders[i] = generate_order(game);
}

static struct station *nearest_station(struct game *game, const struct player *player)
{
	struct station *best = NULL;
	float best_distance = INTERACT_DISTANCE;
	int i;

	for (i = 0; i < game->station_count; ++i) {
		float distance = distance_to_rect(player->x, player->y, &game->stations[i]);

		if (distance < best_distance) {
			best_distance = distance;
			best = &game->stations[i];
		}
	}
	return best;
}

static struct item pick_from_station(struct game *game, struct station *station)
{
	struct item picked = item_none();

	switch (station->type) {
	case ST_MATERIAL_BIN:
		return make_material(station->component);
	case ST_INTAKE:
		return make_board(game);
	case ST_TABLE:
		picked = station->item;
		clear_item(&station->item);
		return picked;
	case ST_WELDER:
		if (item_is_empty(&station->pending_material) && item_is_board(&station->board_item)) {
			picked = station->board_item;
			clear_item(&station->board_item);
			station->progress = 0.0f;
			return picked;
		}
		break;
	case ST_VERIFIER:
		if (item_is_board(&station->item) &&
		    (station->item.board.state == BOARD_PASSED || station->item.board.state == BOARD_FAILED)) {
			picked = station->item;
			clear_item(&station->item);
			station->progress = 0.0f;
			return picked;
		}
		break;
	default:
		break;
	}
	return item_none();
}

static bool board_can_enter_welder(enum board_state state)
{
	return state == BOARD_RAW || state == BOARD_SOLDERING || state == BOARD_READY_TO_TEST;
}

static int find_matching_order(const struct game *game, const struct board *board);

static bool drop_to_welder(struct station *station, struct item *item)
{
	if (item->kind == ITEM_BOARD) {
		if (item_is_empty(&station->board_item) && board_can_enter_welder(item->board.state)) {
			item->board.state = BOARD_SOLDERING;
			station->board_item = *item;
			station->progress = 0.0f;
			return true;
		}
		return false;
	}
	if (item->kind == ITEM_MATERIAL) {
		if (item_is_board(&station->board_item) && item_is_empty(&station->pending_material)) {
			station->pending_material = *item;
			station->progress = 0.0f;
			return true;
		}
	}
	return false;
}

static bool drop_to_verifier(struct game *game, struct station *station, struct item *item)
{
	int order_index;

	if (item->kind != ITEM_BOARD || !item_is_empty(&station->item))
		return false;
	if (!board_can_enter_welder(item->board.state))
		return false;
	station->item = *item;
	station->progress = 0.0f;
	order_index = find_matching_order(game, &station->item.board);
	if (order_index >= 0) {
		station->item.board.state = BOARD_PASSED;
		station->item.board.locked_order_id = game->orders[order_index].id;
		game->orders[order_index].locked_board_id = station->item.board.id;
	} else {
		station->item.board.state = BOARD_FAILED;
	}
	return true;
}

static int order_score(const struct order *order)
{
	return 10 + (order->req[0] + order->req[1] + order->req[2]) * 5;
}

static int find_order_by_id(const struct game *game, int order_id)
{
	int i;

	for (i = 0; i < ORDER_COUNT; ++i) {
		if (game->orders[i].id == order_id)
			return i;
	}
	return -1;
}

static bool ship_item(struct game *game, struct item *item)
{
	int index;

	if (item->kind != ITEM_BOARD || item->board.state != BOARD_PASSED ||
	    item->board.locked_order_id == 0)
		return false;

	index = find_order_by_id(game, item->board.locked_order_id);
	if (index < 0)
		return false;
	if (game->orders[index].locked_board_id != item->board.id)
		return false;

	game->score += order_score(&game->orders[index]);
	game->completed += 1;
	game->orders[index] = generate_order(game);
	return true;
}

static bool drop_to_station(struct game *game, struct station *station, struct item *item)
{
	switch (station->type) {
	case ST_TABLE:
		if (item_is_empty(&station->item)) {
			station->item = *item;
			return true;
		}
		return false;
	case ST_WELDER:
		return drop_to_welder(station, item);
	case ST_VERIFIER:
		return drop_to_verifier(game, station, item);
	case ST_SHIPPING:
		return ship_item(game, item);
	case ST_TRASH:
		if (item->kind == ITEM_BOARD && item->board.locked_order_id != 0)
			return false;
		game->recycled += 1;
		return true;
	default:
		return false;
	}
}

static void interact(struct game *game, struct player *player)
{
	struct station *station = nearest_station(game, player);

	if (station == NULL)
		return;

	if (item_is_empty(&player->held)) {
		player->held = pick_from_station(game, station);
	} else if (drop_to_station(game, station, &player->held)) {
		clear_item(&player->held);
	}
}

static void move_player(struct game *game, float dt)
{
	struct player *player = &game->player;
	float old_x = player->x;
	float old_y = player->y;
	float dx = 0.0f;
	float dy = 0.0f;
	float length;

	if (player->input_mask & OB_BTN_LEFT)
		dx -= 1.0f;
	if (player->input_mask & OB_BTN_RIGHT)
		dx += 1.0f;
	if (player->input_mask & OB_BTN_UP)
		dy -= 1.0f;
	if (player->input_mask & OB_BTN_DOWN)
		dy += 1.0f;

	length = sqrtf(dx * dx + dy * dy);
	if (length > 0.0f) {
		dx /= length;
		dy /= length;
	}

	player->x += dx * PLAYER_SPEED * dt;
	player->y += dy * PLAYER_SPEED * dt;
	player->x = clampf(player->x, OB_MAP_X + PLAYER_RADIUS, OB_MAP_X + OB_MAP_W - PLAYER_RADIUS);
	player->y = clampf(player->y, OB_MAP_Y + PLAYER_RADIUS, OB_MAP_Y + OB_MAP_H - PLAYER_RADIUS);

	if (player_hits_station(player, game)) {
		player->x = old_x;
		player->y = old_y;
	}
}

static int find_matching_order(const struct game *game, const struct board *board)
{
	int i;

	for (i = 0; i < ORDER_COUNT; ++i) {
		if (game->orders[i].locked_board_id != 0)
			continue;
		if (game->orders[i].req[0] == board->counts[0] &&
		    game->orders[i].req[1] == board->counts[1] &&
		    game->orders[i].req[2] == board->counts[2])
			return i;
	}
	return -1;
}

static bool solder_nearest_welder(struct game *game, struct player *player)
{
	struct station *station = nearest_station(game, player);
	enum component component;

	if (station == NULL || station->type != ST_WELDER ||
	    !item_is_board(&station->board_item) ||
	    station->pending_material.kind != ITEM_MATERIAL)
		return false;

	component = station->pending_material.component;
	station->board_item.board.counts[component] += 1;
	station->board_item.board.state = BOARD_READY_TO_TEST;
	clear_item(&station->pending_material);
	station->progress = 0.0f;
	return true;
}

static void update_game(struct game *game, float dt)
{
	struct player *player = &game->player;
	bool action_now;
	bool action_before;
	bool solder_now;
	bool solder_before;

	move_player(game, dt);

	action_now = (player->input_mask & OB_BTN_ACTION) != 0;
	action_before = (player->prev_input_mask & OB_BTN_ACTION) != 0;
	if (action_now && !action_before)
		interact(game, player);

	solder_now = (player->input_mask & OB_BTN_SOLDER) != 0;
	solder_before = (player->prev_input_mask & OB_BTN_SOLDER) != 0;
	if (solder_now && !solder_before)
		solder_nearest_welder(game, player);

	player->prev_input_mask = player->input_mask;
}

static void json_append(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
	va_list ap;
	int written;

	if (*len >= cap)
		return;

	va_start(ap, fmt);
	written = vsnprintf(buf + *len, cap - *len, fmt, ap);
	va_end(ap);

	if (written < 0)
		return;
	if ((size_t)written >= cap - *len)
		*len = cap - 1;
	else
		*len += (size_t)written;
}

static void json_item(char *buf, size_t cap, size_t *len, const struct item *item)
{
	if (item->kind == ITEM_NONE) {
		json_append(buf, cap, len, "null");
	} else if (item->kind == ITEM_MATERIAL) {
		json_append(buf, cap, len,
			    "{\"kind\":\"material\",\"component\":\"%s\"}",
			    component_name(item->component));
	} else {
		json_append(buf, cap, len,
			    "{\"kind\":\"board\",\"board_id\":%d,\"state\":\"%s\","
			    "\"counts\":[%d,%d,%d],\"locked_order_id\":%d}",
			    item->board.id,
			    board_state_name(item->board.state),
			    item->board.counts[0],
			    item->board.counts[1],
			    item->board.counts[2],
			    item->board.locked_order_id);
	}
}

static size_t build_snapshot_json(const struct game *game, char *buf, size_t cap)
{
	size_t len = 0;
	bool first_station = true;
	int i;

	json_append(buf, cap, &len,
		    "{\"packet_id\":\"OBST\","
		    "\"players_count\":1,\"remaining_ms\":%d,\"score\":%d,"
		    "\"completed\":%d,\"recycled\":%d,\"game_over\":%s,",
		    (int)(game->remaining_seconds * 1000.0f),
		    game->score,
		    game->completed,
		    game->recycled,
		    game->game_over ? "true" : "false");

	json_append(buf, cap, &len, "\"players\":[");
	json_append(buf, cap, &len,
		    "{\"id\":%d,\"x\":%.1f,\"y\":%.1f,\"held\":",
		    game->player.id,
		    game->player.x,
		    game->player.y);
	json_item(buf, cap, &len, &game->player.held);
	json_append(buf, cap, &len, "}");
	json_append(buf, cap, &len, "],");

	json_append(buf, cap, &len, "\"orders\":[");
	for (i = 0; i < ORDER_COUNT; ++i) {
		const struct order *order = &game->orders[i];

		if (i > 0)
			json_append(buf, cap, &len, ",");
		json_append(buf, cap, &len,
			    "{\"id\":%d,\"req\":[%d,%d,%d],\"locked_board_id\":%d}",
			    order->id,
			    order->req[0],
			    order->req[1],
			    order->req[2],
			    order->locked_board_id);
	}
	json_append(buf, cap, &len, "],");

	json_append(buf, cap, &len, "\"stations\":[");
	for (i = 0; i < game->station_count; ++i) {
		const struct station *station = &game->stations[i];

		if (station->type != ST_TABLE && station->type != ST_WELDER &&
		    station->type != ST_VERIFIER)
			continue;
		if (!first_station)
			json_append(buf, cap, &len, ",");
		first_station = false;
		json_append(buf, cap, &len,
		    "{\"id\":\"%s\"", station->id);
		if (station->type == ST_TABLE) {
			json_append(buf, cap, &len, ",\"item\":");
			json_item(buf, cap, &len, &station->item);
		} else if (station->type == ST_WELDER) {
			json_append(buf, cap, &len, ",\"board\":");
			json_item(buf, cap, &len, &station->board_item);
			json_append(buf, cap, &len, ",\"pending_material\":");
			json_item(buf, cap, &len, &station->pending_material);
			json_append(buf, cap, &len, ",\"progress\":%.3f", station->progress / SOLDER_SECONDS);
		} else if (station->type == ST_VERIFIER) {
			json_append(buf, cap, &len, ",\"item\":");
			json_item(buf, cap, &len, &station->item);
			json_append(buf, cap, &len, ",\"progress\":%.3f", station->progress / INSPECTION_SECONDS);
		}
		json_append(buf, cap, &len, "}");
	}
	json_append(buf, cap, &len, "]}");

	if (len >= cap)
		len = cap - 1;
	buf[len] = '\0';
	return len;
}

#ifdef _WIN32
static void socket_startup(void)
{
	WSADATA data;

	WSAStartup(MAKEWORD(2, 2), &data);
}

static void socket_cleanup(void)
{
	WSACleanup();
}

static int set_nonblocking(ob_socket_t sock)
{
	u_long mode = 1;
	return ioctlsocket(sock, FIONBIO, &mode);
}

static double now_ms(void)
{
	static LARGE_INTEGER freq;
	LARGE_INTEGER counter;

	if (freq.QuadPart == 0)
		QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void sleep_ms(int ms)
{
	Sleep((DWORD)ms);
}
#else
static void socket_startup(void)
{
}

static void socket_cleanup(void)
{
}

static int set_nonblocking(ob_socket_t sock)
{
	int flags = fcntl(sock, F_GETFL, 0);

	if (flags < 0)
		return -1;
	return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}
#endif

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  This Lab 2 server supports player 1 only.\n"
		"  --input-port PORT      default: 17666\n"
		"  --display-host IP      default: 127.0.0.1\n"
		"  --display-port PORT    default: 17667\n",
		argv0);
}

static void receive_inputs(ob_socket_t sock, struct game *game)
{
	for (;;) {
		struct ob_input_packet packet;
		struct sockaddr_in from;
#ifdef _WIN32
		int from_len = sizeof(from);
		int n = recvfrom(sock, (char *)&packet, sizeof(packet), 0,
				 (struct sockaddr *)&from, &from_len);
#else
		socklen_t from_len = sizeof(from);
		ssize_t n = recvfrom(sock, &packet, sizeof(packet), 0,
				     (struct sockaddr *)&from, &from_len);
#endif
		(void)from;
		(void)from_len;

		if (n < 0) {
#ifdef _WIN32
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
				return;
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
#endif
			return;
		}
		if ((size_t)n != sizeof(packet))
			continue;
		if (memcmp(packet.magic, "OBIN", 4) != 0)
			continue;
		if (packet.player_id != 1)
			continue;

		game->player.input_mask = packet.button_mask & 0x3f;
	}
}

int main(int argc, char **argv)
{
	int input_port = INPUT_PORT_DEFAULT;
	int display_port = DISPLAY_PORT_DEFAULT;
	const char *display_host = DISPLAY_HOST_DEFAULT;
	ob_socket_t input_sock;
	ob_socket_t display_sock;
	struct sockaddr_in input_addr;
	struct sockaddr_in display_addr;
	struct game game;
	double last_ms;
	double next_snapshot_ms;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--input-port") == 0 && i + 1 < argc) {
			input_port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--display-host") == 0 && i + 1 < argc) {
			display_host = argv[++i];
		} else if (strcmp(argv[i], "--display-port") == 0 && i + 1 < argc) {
			display_port = atoi(argv[++i]);
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	socket_startup();

	input_sock = socket(AF_INET, SOCK_DGRAM, 0);
	display_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (input_sock == INVALID_SOCKET || display_sock == INVALID_SOCKET) {
		fprintf(stderr, "socket creation failed\n");
		return 1;
	}

	memset(&input_addr, 0, sizeof(input_addr));
	input_addr.sin_family = AF_INET;
	input_addr.sin_port = htons((uint16_t)input_port);
	input_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(input_sock, (struct sockaddr *)&input_addr, sizeof(input_addr)) == SOCKET_ERROR) {
		fprintf(stderr, "bind input port %d failed\n", input_port);
		return 1;
	}
	set_nonblocking(input_sock);

	memset(&display_addr, 0, sizeof(display_addr));
	display_addr.sin_family = AF_INET;
	display_addr.sin_port = htons((uint16_t)display_port);
	if (inet_pton(AF_INET, display_host, &display_addr.sin_addr) != 1) {
		fprintf(stderr, "Invalid display host: %s\n", display_host);
		return 1;
	}

	init_game(&game);
	last_ms = now_ms();
	next_snapshot_ms = last_ms;

	printf("Overbug server listening on UDP %d, sending display snapshots to %s:%d\n",
	       input_port, display_host, display_port);

	while (running) {
		double current_ms;
		float dt;

		receive_inputs(input_sock, &game);

		current_ms = now_ms();
		dt = (float)((current_ms - last_ms) / 1000.0);
		if (dt > 0.1f)
			dt = 0.1f;
		last_ms = current_ms;

		update_game(&game, dt);

		if (current_ms >= next_snapshot_ms) {
			char json[SNAPSHOT_BYTES];
			size_t len = build_snapshot_json(&game, json, sizeof(json));

			sendto(display_sock, json, (int)len, 0,
			       (struct sockaddr *)&display_addr, sizeof(display_addr));
			next_snapshot_ms = current_ms + 33.0;
		}

		sleep_ms(5);
	}

	CLOSE_SOCKET(input_sock);
	CLOSE_SOCKET(display_sock);
	socket_cleanup();
	return 0;
}
