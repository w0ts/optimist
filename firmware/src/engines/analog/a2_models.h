/* SPDX-License-Identifier: GPL-3.0-only */
/* ANALOG 2's filter models (a2_models.c: float, its own unit): the models, the switches, the one entry point */
#ifndef A2_MODELS_H
#define A2_MODELS_H
#include <stdint.h>
#ifndef FELUCCA_A2_LADR
#define FELUCCA_A2_LADR 0                  /* LADR: transistor (Moog-style) 4-pole ladder */
#endif
#ifndef FELUCCA_A2_OTA
#define FELUCCA_A2_OTA 0                   /* OTA: IR3109 (Juno-style) 4-pole */
#endif
#ifndef FELUCCA_A2_DIOD
#define FELUCCA_A2_DIOD 0                  /* DIOD: diode (303-style) ladder */
#endif
#ifndef FELUCCA_A2_MS20
#define FELUCCA_A2_MS20 0                  /* MS20: Sallen-Key (Korg-35, MS-20-style) 2-pole */
#endif
#define FELUCCA_A2_MODELS (FELUCCA_A2_LADR || FELUCCA_A2_OTA || FELUCCA_A2_DIOD || FELUCCA_A2_MS20)
#define A2M_BUILT (1u | (unsigned)FELUCCA_A2_LADR << 1 | (unsigned)FELUCCA_A2_OTA << 2 | (unsigned)FELUCCA_A2_DIOD << 3 | \
                   (unsigned)FELUCCA_A2_MS20 << 4)   /* the models built, bit m: model m (SVF always) */
#ifndef A2M_MODES
#define A2M_MODES 5u                       /* FTYP's modes LP12 LP24 BP HP NOTCH; P_A2FTYP / A2M_MODES the MODEL */
#endif
enum { A2M_SVF, A2M_LADR, A2M_OTA, A2M_DIOD, A2M_MS20, A2M_N };
/* model mdl (A2M_LADR ..; one not built: nothing done) over the n samples of b (half scale, in place), FTYP mode, RES res
 * (0..127), the cutoff c0 -> cut (0..127 << 8); its states in s[0..1], s[4..5], s[7] (a voice's) */
void a2m_block(int32_t *b, int32_t *s, int32_t c0, int32_t cut, uint32_t mdl, uint32_t mode, int32_t res, uint32_t n);
#endif
