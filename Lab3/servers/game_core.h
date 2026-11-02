#ifndef OVERBUG_GAME_CORE_H
#define OVERBUG_GAME_CORE_H

/* Four-player Overbug rules shared by both servers.
 * No networking or synchronization here: only the thread that owns a
 * struct game may call these functions on it. */
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../overbug_world.h"

#define PLAYER_RADIUS 18.0f
#define PLAYER_SPEED 185.0f
/* Movement is integrated in steps of at most 1/60 s (3 px), so a slow server
 * moves as far as a 60 Hz one and still cannot skip past a station. */
#define MOVE_STEP_SECONDS (1.0f / 60.0f)
/* Only guards against long stalls (debugger, SIGSTOP), not normal slow ticks. */
#define MAX_UPDATE_SECONDS 0.5f
#define INTERACT_DISTANCE 54.0f
#define ROUND_SECONDS 180.0f
#define SOLDER_SECONDS 2.0f
#define INSPECTION_SECONDS 4.0f
#define ORDER_COUNT 3
#define MAX_STATIONS OB_FACILITY_COUNT
#define OBST_BYTES 32768

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
	uint8_t input_mask;	/* buttons held now; only the direction bits are used here */
	struct item held;
};

struct game {
	struct player players[4];
	int players_count;
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

	const float spawn_x[4] = {560.0f, 620.0f, 500.0f, 680.0f};
	game->players_count = 4;
	for (i = 0; i < 4; ++i) {
		game->players[i].id = i + 1;
		game->players[i].x = spawn_x[i];
		game->players[i].y = 370.0f;
		game->players[i].held = item_none();
	}

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

static void move_player(struct game *game, struct player *player, float dt)
{
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

/* Moves one player by dt along the held direction buttons. */
static void update_player(struct game *game, struct player *player, float dt)
{
	float step;

	while (dt > 0.0f) {
		step = dt < MOVE_STEP_SECONDS ? dt : MOVE_STEP_SECONDS;
		move_player(game, player, step);
		dt -= step;
	}
}

/* Carries out one button press: ACTION picks up or puts down at the nearest
 * station, SOLDER solders at the nearest welder. buttons holds the buttons
 * that were just pressed, not the ones still held. */
static void press_buttons(struct game *game, struct player *player, uint8_t buttons)
{
	if (buttons & OB_BTN_ACTION)
		interact(game, player);
	if (buttons & OB_BTN_SOLDER)
		solder_nearest_welder(game, player);
}

/* Moves every player by dt. Button presses are not handled here: the server
 * calls press_buttons for them in the order their packets arrived. */
static void update_game(struct game *game, float dt)
{
	if (dt > MAX_UPDATE_SECONDS)
		dt = MAX_UPDATE_SECONDS;
	for (int i = 0; i < game->players_count; ++i)
		update_player(game, &game->players[i], dt);
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

static size_t build_obst_json(const struct game *game, char *buf, size_t cap)
{
	size_t len = 0;
	bool first_station = true;
	int i;

	json_append(buf, cap, &len,
		    "{\"packet_id\":\"OBST\","
		    "\"players_count\":%d,\"remaining_ms\":%d,\"score\":%d,"
		    "\"completed\":%d,\"recycled\":%d,\"game_over\":%s,",
		    game->players_count,
		    (int)(game->remaining_seconds * 1000.0f),
		    game->score,
		    game->completed,
		    game->recycled,
		    game->game_over ? "true" : "false");

	json_append(buf, cap, &len, "\"players\":[");
	for (i = 0; i < game->players_count; ++i) {
		const struct player *player = &game->players[i];
		if (i > 0)
			json_append(buf, cap, &len, ",");
		json_append(buf, cap, &len,
			    "{\"id\":%d,\"x\":%.1f,\"y\":%.1f,\"held\":",
			    player->id, player->x, player->y);
		json_item(buf, cap, &len, &player->held);
		json_append(buf, cap, &len, "}");
	}
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

#endif
