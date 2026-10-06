/* SPDX-License-Identifier: GPL-3.0-only */
/* Motion (motion.c) in flash, next to its project: each project / autosave sector (storage.c) has its 32-byte
 * commit header at 0 and the payload from 256: bytes 32..255 are erased and unused. After st_save committed a
 * project, its store goes there (MF_OFF, 208 bytes: a magic, the 200-byte store, a CRC). NOR programs erased
 * bytes at any time, so the project's own commit stays as it is: a power cut between the two leaves the bytes
 * erased (no motion), never a damaged project. Read back only when its CRC holds and its psum is the sum of the
 * project loaded with it. A build without the switch never reads or writes there; its next save of that slot
 * erases the sector (the motion goes with it). Included by project.c (and tests/backports_test.c). */
#define MF_OFF 32u
#define MF_MAGIC 0x4E544F4Du                   /* "MOTN" */
typedef struct {
    uint32_t magic;
    motion_store_t m;
    uint32_t crc;
} motion_rec_t;
_Static_assert(sizeof(motion_rec_t) <= ST_PAYLOAD_OFF - MF_OFF, "motion: in the sector's unused bytes");
_Static_assert(sizeof(st_hdr_t) <= MF_OFF, "motion: after the commit header");
static motion_rec_t mf_rec __attribute__((aligned(4)));   /* (the flash driver wants a RAM source) */

/* after a successful st_save of project p as object obj: its store beside it (nothing to write: none) */
static void motion_flash_write(uint32_t obj, const project_t *p)
{
    const motion_store_t *m = motion_for(p, 0);
    st_hdr_t h;
    int copy;
    if (!m || m->psum != p->sum || (!m->count && !m->on))
        return;
    copy = st_current(obj, &h);
    if (copy < 0)
        return;
    mf_rec.magic = MF_MAGIC;
    mf_rec.m = *m;
    mf_rec.crc = st_crc32(&mf_rec, sizeof mf_rec - 4u);
    (void)st_prog(st_sector(obj, (uint32_t)copy) + MF_OFF, &mf_rec, sizeof mf_rec);
}

/* after project p was read from object obj: its store, if the flash holds this project's */
static void motion_flash_read(uint32_t obj, const project_t *p)
{
    motion_store_t *m = motion_for(p, 1);
    st_hdr_t h;
    int copy;
    if (!m)
        return;
    m->psum = 0;
    m->count = m->on = 0;
    copy = st_current(obj, &h);
    if (copy < 0 || st_read(st_sector(obj, (uint32_t)copy) + MF_OFF, &mf_rec, sizeof mf_rec))
        return;
    if (mf_rec.magic != MF_MAGIC || mf_rec.crc != st_crc32(&mf_rec, sizeof mf_rec - 4u) ||
        mf_rec.m.psum != p->sum || !motion_valid(&mf_rec.m))
        return;
    *m = mf_rec.m;
}
