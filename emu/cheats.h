#pragma once
/* Hidden pilots. On the cabinet the game unlocks them itself (Gasgano after 700 plays, Ben Quadinaros after
 * finishing the Expert course 50 times) by setting flags in the EEPROM game area; emu_unlock sets them. */
enum { CHEAT_BEN, CHEAT_GASGANO, CHEAT_N };
typedef struct { const char *name_es, *name_en; } CheatDef;
extern const CheatDef g_cheats[CHEAT_N];
