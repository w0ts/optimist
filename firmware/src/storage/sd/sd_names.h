/* SPDX-License-Identifier: GPL-3.0-only */
/* The engines' folder names on the card (the user, 2026-11-04): Optimist/Presets/Factory/<Engine>/ (the SD card zip's,
 * tools/sd_factory.c) and Optimist/Presets/User/<Engine>/ (the presets saved on the FM-1, sd_mode.c). One table for
 * both: sd_factory.c builds the zip from these very names. Capitalised, a name with a digit or a known acronym kept
 * upper case (FM6, CS80, CZ, SID, VPS, WSEQ); the registry's name (FORMANT, DRAWBAR), not the list's (VOICE, WHEEL).
 * Every engine of registry.h ENGINE_LIST needs its SDN_<NAME>: one missing does not compile (SDN_ENGINE below);
 * tests/sd_zip_test.py checks the rule on every entry and the zip's folders against it.
 *   sdn_engine(uid)    the folder of the engine with that UID (built or not: an old preset of an engine this build
 *                      leaves out still goes to its own folder), "Other" for a UID no engine has
 * Needs registry.h (ENGINE_LIST, ENG_UID_N). */
#define SDN_ANALOG "Analog"
#define SDN_DIGITAL "Digital"
#define SDN_PHASE "Phase"
#define SDN_LOFI "Lofi"
#define SDN_SAMPLE "Sample"
#define SDN_FORMANT "Formant"
#define SDN_TRIO "Trio"
#define SDN_DRAWBAR "Drawbar"
#define SDN_GRAIN "Grain"
#define SDN_SUPER "Super"
#define SDN_FM6 "FM6"
#define SDN_SLICE "Slice"
#define SDN_PHYS "Phys"
#define SDN_ACID "Acid"
#define SDN_CZ "CZ"
#define SDN_JUNO "Juno"
#define SDN_NOISE "Noise"
#define SDN_DRONE "Drone"
#define SDN_PLAITS "Plaits"
#define SDN_STRINGS "Strings"
#define SDN_VPS "VPS"
#define SDN_TERRAIN "Terrain"
#define SDN_WSEQ "WSEQ"
#define SDN_VECTOR "Vector"
#define SDN_SID "SID"
#define SDN_PROPHET "Prophet"
#define SDN_WAVE "Wave"
#define SDN_CS80 "CS80"
#define SDN_STRETCH "Stretch"
#define SDN_SQUARE "Square"                          /* (the contributors' example: CONTRIBUTING.md) */

#define SDN_UID_(u, N, fb, s) [u] = SDN_##N,
static const char *const SDN_ENGINE[ENG_UID_N] = {ENGINE_LIST(SDN_UID_)};
#undef SDN_UID_

static const char *sdn_engine(uint32_t uid) { return uid < ENG_UID_N && SDN_ENGINE[uid] ? SDN_ENGINE[uid] : "Other"; }
