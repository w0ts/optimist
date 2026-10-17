/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: DESC's body, one copy for every command that sends it (DESC 5, FXP and DLAN op 2, STEPLANE op 1):
 * fmt, min, max, def (v14 each; ed_fmd, also SCRN's and SCRS's cells), label, unit, then an enum's names. On ed_b,
 * ed_v and ed_str (editor.c's, or a host test's own); included by editor.c and each of those files. */
#ifndef ED_DESC_H
#define ED_DESC_H
static void ed_fmd(const param_desc_t *d)
{
    ed_b(d->fmt);
    ed_v(d->min);
    ed_v(d->max);
    ed_v(d->def);
}
static void ed_desc_body(const param_desc_t *d)
{
    uint32_t i;
    ed_fmd(d);
    ed_str(d->label, 8);
    ed_str(d->unit, 8);
    if (d->fmt == F_ENUM && d->names)
        for (i = 0; i <= (uint32_t)(d->max - d->min) && i < 64u; i++)   /* (the 34 drum kits) */
            ed_str(d->names[i], 8);
}
#endif
