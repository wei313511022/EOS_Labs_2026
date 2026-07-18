#ifndef OVERBUG_PROTOCOL_H
#define OVERBUG_PROTOCOL_H

#include <stdint.h>

struct __attribute__((packed)) ob_input_packet {
	char magic[4];       /* "OBIN" */
	uint8_t player_id;   /* 1 for Lab 2 */
	uint8_t button_mask; /* 6-bit device state */
};

#endif
