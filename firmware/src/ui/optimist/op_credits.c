/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David (hdavid), Optimist (github.com/w0ts/optimist) */
/* SYSTEM > CREDITS, the last page: whose work Optimist is built from, as film end titles (the user, 2026-10-29). Each
 * credit is the work's name in the small face (dim) over the person or project in the large face, centred; the list
 * rises by itself, slowly, and loops with a gap after the last credit. KNOB 1 scrolls it by hand, both ways; the
 * crawl resumes CR_PAUSE_MS after the last turn. Names as their licences write them (LICENSING.md has the licences,
 * commits and files; the font is ASCII only, so "Emilie" has no accent here, as it has none in the sources).
 *
 * The cost: nothing unless the page is shown (the clock, the paint and the signature are all behind gl_cred()); on the
 * page the band is painted when the offset changes, in steps of CR_STEP pixels (never while the crawl is held after
 * a turn and the offset is the same), only the credits that touch the band, no text on a filled area. */
typedef struct {
    const char *name;                                   /* the work, small and dim */
    const char *who[3];                                 /* the person or project, large (small where the large does not fit) */
} cred_t;
static const cred_t CRED[] = {
    {"VERSION", {FELUCCA_VERSION}},                     /* ("OPTIMIST 0.1"; a release: its own string) */
    {"FELUCCA", {"Leo Kuroshita"}},                     /* (the platform: Hugelton Instruments) */
    {"SLOOP", {"isod89"}},
    {"MELODEE", {"Kerem Kilic"}},
    {"X0X", {"Charles Vestal"}},
    {"FM1-BASE", {"Charles Vestal"}},                   /* (the platform layer, fm1base/CREDITS.md) */
    {"FLOWSTATE", {"Z. Chowdhury"}},
    {"JANGADA", {"zednaked"}},
#if FELUCCA_ENG_DRONE
    {"CARAVEL", {"bonszaimusic"}},                      /* (DRONE's network, eng_drone.c) */
#endif
    {"CLIPPER", {"3dSam"}},
    {"MINI-TONE", {"Stephane Schmitz"}},
    {"CS-1", {"twochannels"}},
    {"KAWA TRACKER", {"Guizmoh"}},
    {"HORTATOR", {"DeadActive"}},
    {"SLOOP FORK", {"renebohne"}},
    {"SLOOP 8 TRACKS", {"Erick Buendia Barrientos"}},
    {"FAVORITES, MIDI", {"ChanceTheMaker"}},
    {"INSTALLER", {"Chris Farrell"}},
    {"PLAITS RINGS GRIDS", {"Emilie Gillet"}},          /* (Mutable Instruments) */
    {"DAISYSP", {"Electrosmith"}},
    {"CZ PHASE (MAME)", {"Devin Acker"}},
    {"DX7 (MSFA)", {"Google"}},
    {"DX7 (DEXED)", {"Pascal Gauthier"}},
    {"AIRWINDOWS", {"Chris Johnson"}},
    {"PLATE", {"J. Dattorro"}},
    {"FDN8", {"J.-M. Jot", "S. Costello"}},
    {"VOICE (KLATTSCH)", {"Tony Gies"}},
    {"JUNO", {"fm-106", "KR-106"}},                     /* (PatttF; Kayrock Screenprinting) */
    {"ACID", {"Open303", "TB-3PO"}},                    /* (Robin Schmidt, Logarhythm: MIT) */
    {"X0X 909, 808", {"athousanddetails"}},             /* (9W9, 8W8) */
    {"909 CYMBALS (ER-99)", {"Matthew Cieplak"}},
    {"808 RIM (SC808)", {"Yoshinosuke Horiuchi"}},
    {"LINK", {"Ableton"}},
    {"COMPILER", {"Julius Vitkauskas"}},                /* (pi32v2-llvm, m-bootloader) */
    {"BT AUDIO SBC", {"Broadcom"}},                     /* (Android's Bluedroid) */
    {"BT PAIRING", {"Kenneth MacKay"}},                 /* (micro-ecc) */
    {"SD CARD", {"ChaN"}},                              /* (Petit FatFs) */
    {"RADIO FACTS", {"Zhuhai Jieli"}},                  /* (the AC79 SDK) */
    {"FM-1 V15", {"M-VAVE"}},
    {"SAMPLES", {"VSCO", "VCSL", "Sonic Pi"}},
    {"FONT", {"Terminus", "Barlow"}},
    {"THE REST", {"LICENSING.md"}},
};
#define CRED_N (sizeof CRED / sizeof CRED[0])
#define CR_BAND (OP_FOOT_Y - OH_HEAD)                  /* the band the end titles fill (gl_draw's GL_BAND) */
#define CR_STEP 2                                       /* pixels a paint moves the list by (a paint sends the band: fewer, larger steps) */
#define CR_SPEED 24                                     /* pixels a second the crawl rises (a screen of the large face in ~9 s) */
#define CR_TURN 16                                      /* pixels a detent of KNOB 1 moves it */
#define CR_PAUSE_MS 3000u                               /* the crawl waits this long after the last turn */
#define CR_GAP_ROWS 12                                  /* between two credits */
#define CR_END_GAP 96                                   /* after the last one, before the first comes round again */
#define CR_NAME_GAP 2                                   /* the name over its first line */
static struct {
    int32_t pos;                                        /* the offset, in thousandths of a pixel, 0 .. cr_len() */
    uint32_t t;                                         /* the clock at the last tick */
    uint32_t hold;                                      /* the clock until which the crawl waits */
    uint32_t draws;                                     /* paints made (the tests count them) */
    uint8_t on;                                         /* ticked in the last frame: the page is on the screen */
    int16_t px;                                         /* the offset painted, in pixels */
} cr;

static uint32_t cr_lines(uint32_t i)                    /* the large lines of credit i */
{
    return CRED[i].who[2] ? 3u : CRED[i].who[1] ? 2u : 1u;
}
static int32_t cr_h(uint32_t i)                         /* the height of credit i with the gap after it */
{
    return (int32_t)FONT_S.h + CR_NAME_GAP + (int32_t)cr_lines(i) * (int32_t)font_big()->h + CR_GAP_ROWS;
}
static int32_t cr_len(void)                             /* one loop: every credit, then the gap */
{
    uint32_t i;
    int32_t n = CR_END_GAP;
    for (i = 0; i < CRED_N; i++)
        n += cr_h(i);
    return n;
}
static void cr_wrap(void)                               /* the offset inside one loop */
{
    int32_t len = cr_len() * 1000;
    while (cr.pos >= len)
        cr.pos -= len;
    while (cr.pos < 0)
        cr.pos += len;
}
static uint32_t cred_page(void)                         /* SYSTEM's page under the cursor is the credits (the input's view; the drawing's is gl_cred) */
{
    uint32_t e;
    if (ui.scr != SCR_SYSTEM)
        return 0;
    e = gl_ent(1, gl_first(1, ui.row[SCR_SYSTEM]));
    return sys_item(e >> 2, e & 3u) == SI_CREDITS;
}
static void cred_turn(int32_t s)                        /* KNOB 1: by hand, both ways (SHIFT's reset does nothing); the crawl waits */
{
    if (s == OP_RESET)
        return;
    cr.pos += s * CR_TURN * 1000;
    cr_wrap();
    cr.hold = fm1_ms + CR_PAUSE_MS;
    cr.t = fm1_ms;
}
static void cred_tick(void)                             /* a frame with the page shown: the crawl moves by the time passed */
{
    uint32_t dt = fm1_ms - cr.t;
    if (!cr.on || dt > 1000u) {                         /* just shown (or the frames stalled): from the top, the first credit in view */
        cr.pos = (CR_BAND - 4) * 1000;   /* (the first credit's top 4 px under the band's) */
        cr.hold = fm1_ms;
        dt = 0;
    }
    cr.on = 1;
    cr.t = fm1_ms;
    if ((int32_t)(fm1_ms - cr.hold) >= 0) {
        cr.pos += (int32_t)(dt > 100u ? 100u : dt) * CR_SPEED;   /* (ms x px/s = thousandths of a pixel) */
        cr_wrap();
    }
}
static void cred_off(void) { cr.on = 0; }               /* another page: the next time starts at the top */
static int32_t cred_px(void) { return cr.pos / 1000 / CR_STEP * CR_STEP; }   /* what the screen shows: whole steps */
static void cred_block(uint32_t i, int32_t y)           /* credit i with its top at y */
{
    const felucca_font_t *fb = font_big();
    uint32_t k;
    cv_text_mid(y, &FONT_S, CRED[i].name, C_GRAY);
    y += (int32_t)FONT_S.h + CR_NAME_GAP;
    for (k = 0; k < cr_lines(i); k++, y += (int32_t)fb->h) {
        const char *s = CRED[i].who[k];
        uint16_t col = i == 0 ? C_WHITE : C_HI;
        if (text_w(fb, s) <= 232)
            cv_text_mid(y, fb, s, col);
        else
            cv_text_mid(y + ((int32_t)fb->h - (int32_t)FONT_S.h) / 2, &FONT_S, s, col);   /* (a name too long for the large face) */
    }
}
static void gl_cred_draw(void)                          /* the band, in its own coordinates: the credits that touch it; the loop's seam shows the end and the start together */
{
    uint32_t i;
    int32_t len = cr_len(), y = 0, k;
    for (i = 0; i < CRED_N; i++) {
        int32_t h = cr_h(i);
        for (k = -1; k <= 1; k++) {
            int32_t top = CR_BAND - cr.px + y + k * len;
            if (top + h > 0 && top < CR_BAND)
                cred_block(i, top);
        }
        y += h;
    }
}
