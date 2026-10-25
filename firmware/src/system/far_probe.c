/* SPDX-License-Identifier: GPL-3.0-only */
/* The far probe (FELUCCA_LAYOUT 1, docs/SD-MODE-DESIGN.md): a constant and a function linked at app.bin offset
 * 0x90000 (flash 0x94120), past the normal layout's end, in their own section (tools/build.py FAR_OFF: appended after
 * everything the boot needs, so a chip that cannot reach there still boots and updates). Nothing at boot touches
 * them: the console's far reads the constant through XIP (decrypted by the SFC with the chip key, as all code) and
 * only when it reads right calls the function.
 *   far_check()   0 both right; 1 the constant reads wrong (the function not called); 2 the call returns wrong */
#define FAR_K0 0x4641524Fu
#define FAR_K1 0x50544D53u
#define FAR_K2 0x1234ABCDu
#define FAR_K3 0x0BADF00Du
static const uint32_t far_magic[4] __attribute__((section(".text_far.magic"), used, aligned(16))) =
    {FAR_K0, FAR_K1, FAR_K2, FAR_K3};
__attribute__((section(".text_far.fn"), noinline, used)) static uint32_t far_fn(uint32_t x)
{
    return x * 2654435761u + 0x5A5A5A5Au;
}
static uint32_t far_seen[4];                         /* what the constant read as (the console prints it) */

static uint32_t far_check(void)
{
    uint32_t (*volatile f)(uint32_t) = far_fn;
    uint32_t i;
    for (i = 0; i < 4u; i++)
        far_seen[i] = fm1_xip_word(&far_magic[i]);   /* (read from the flash, not folded) */
    if (far_seen[0] != FAR_K0 || far_seen[1] != FAR_K1 || far_seen[2] != FAR_K2 || far_seen[3] != FAR_K3)
        return 1;
    return f(12345u) == 12345u * 2654435761u + 0x5A5A5A5Au ? 0 : 2;
}
