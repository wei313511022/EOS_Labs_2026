#ifndef OVERBUG_WORLD_H
#define OVERBUG_WORLD_H

/* Fixed Overbug facility layout shared by the Lab 4 server implementation. */

enum component {
	COMP_NONE = -1,
	COMP_RESISTOR = 0,
	COMP_CAPACITOR = 1,
	COMP_INDUCTOR = 2,
	COMP_COUNT = 3
};

enum station_type {
	ST_MATERIAL_BIN = 0,
	ST_INTAKE,
	ST_TABLE,
	ST_WELDER,
	ST_VERIFIER,
	ST_SHIPPING,
	ST_TRASH
};

#define OB_MAP_X 70.0f
#define OB_MAP_Y 96.0f
#define OB_MAP_W 1140.0f
#define OB_MAP_H 540.0f

#define OB_FACILITY_COUNT 16

/* X(id, type, x, y, w, h, component) */
#define OB_FACILITY_LAYOUT(X) \
	X("bin_resistor", ST_MATERIAL_BIN, 92.0f, 160.0f, 92.0f, 62.0f, COMP_RESISTOR) \
	X("bin_capacitor", ST_MATERIAL_BIN, 92.0f, 246.0f, 92.0f, 62.0f, COMP_CAPACITOR) \
	X("bin_inductor", ST_MATERIAL_BIN, 92.0f, 332.0f, 92.0f, 62.0f, COMP_INDUCTOR) \
	X("trash", ST_TRASH, 92.0f, 542.0f, 112.0f, 68.0f, COMP_NONE) \
	X("intake", ST_INTAKE, 1058.0f, 542.0f, 112.0f, 68.0f, COMP_NONE) \
	X("shipping", ST_SHIPPING, 1092.0f, 286.0f, 92.0f, 84.0f, COMP_NONE) \
	X("welder_1", ST_WELDER, 304.0f, 542.0f, 132.0f, 68.0f, COMP_NONE) \
	X("welder_2", ST_WELDER, 474.0f, 542.0f, 132.0f, 68.0f, COMP_NONE) \
	X("welder_3", ST_WELDER, 574.0f, 118.0f, 132.0f, 68.0f, COMP_NONE) \
	X("verifier_1", ST_VERIFIER, 786.0f, 118.0f, 136.0f, 68.0f, COMP_NONE) \
	X("verifier_2", ST_VERIFIER, 954.0f, 118.0f, 136.0f, 68.0f, COMP_NONE) \
	X("table_1", ST_TABLE, 230.0f, 118.0f, 100.0f, 60.0f, COMP_NONE) \
	X("table_2", ST_TABLE, 354.0f, 118.0f, 100.0f, 60.0f, COMP_NONE) \
	X("table_3", ST_TABLE, 646.0f, 542.0f, 100.0f, 68.0f, COMP_NONE) \
	X("table_4", ST_TABLE, 776.0f, 542.0f, 100.0f, 68.0f, COMP_NONE) \
	X("table_5", ST_TABLE, 906.0f, 542.0f, 100.0f, 68.0f, COMP_NONE)

#endif
