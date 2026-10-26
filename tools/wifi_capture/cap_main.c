/* SPDX-License-Identifier: GPL-3.0-only */
/* The Wi-Fi start-up capture image (tools/wifi_capture/README.md): JieLi's own Wi-Fi radio start-up (the AC79 SDK's
 * wl_rf_common.a, Apache-2.0, linked from the user's download, never committed) run in the FM-1 emulator with its
 * register writes traced, as tools/ble_rf_capture.py does for the BT start-up with stock V15. This file is the rest of
 * the system the library expects: start-up, a delay, the trims, and stand-ins for the OS, logging and power calls.
 *
 * Sequence (the SDK's wifi_on order, docs/WIFI-HW-FACTS.md §2): wf_rf_common_init (clocks / reset, frame DMA, the
 * PHY / RF start-up), wifi_rf_on (the Wi-Fi RF path), the interrupt set-up (wfhw_iqr_init); then the same again as a
 * second boot: wifi_rf_off, the init flag cleared, wf_rf_common_init finding the RF trim the first pass stored
 * (wifi_set_rf_trim_data: the quick start, the path a device with VM 187 takes), wifi_rf_on; then idle. cap_phase
 * marks each step in RAM for the trace's reader (1-4 first pass, 5-7 second). Trims: the SDK demo board's (board.c
 * wifi_calibration_param); the RF trim is the emulator's own result, so only its layout means anything. */
#include <stdint.h>
#include "../../firmware/hal/fm1_time.h"

#ifndef CAP_RF_TRIM
#define CAP_RF_TRIM 0
#endif
#ifndef CAP_STA
#define CAP_STA 0                                 /* 1: the Ralink STA driver linked too: the SDK's usb_register instead */
#endif

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
void *memcpy(void *d, const void *s, unsigned n);
volatile uint32_t cap_phase;                      /* 1 rf_common_init, 2 rf_on, 3 irq init, 4 first pass done,
                                                   * 5 rf_common_init (quick), 6 rf_on, 7 done */
volatile uint32_t jiffies;
uint8_t RTDebugLevel = 0;

int wf_rf_common_init(void);
void wifi_rf_on(void);
void wfhw_iqr_init(void);
void wifi_rf_off(void);
void set_wf_phy_init_flag(uint8_t f);
int main(void);
static int cap_trim_saved;

void cap_cstart(void)
{
    uint32_t *s = _data_load, *d = _data_start;
    while (d < _data_end)
        *d++ = *s++;
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    fm1_time_init();
    main();
}

#if CAP_STA
int usb_register(void *drv);
void tasklet_initialize(void);
uint8_t *wifi_get_wifi_send_pkg_ptr(void);
int wl30_usb_control_msg(void *dev, unsigned pipe, uint8_t req, uint8_t type, uint16_t value, uint16_t index, void *data,
                         uint16_t size, int timeout);
void wifi_send_data(int len, int rate);
int main(void)                                    /* the SDK's start: radio, WLC, rtusb_probe (the driver up) */
{
    tasklet_initialize();                         /* (the driver's timer / tasklet lists; its thread never runs here) */
    wf_rf_common_init();                          /* the radio up first, as BT starts it on the FM-1: usb_register then
                                                   * finds it up and runs wlc_main (the coexistence set-up) */
    cap_phase = 10;
    usb_register(0);
    cap_phase = 11;
    {   /* one frame through JieLi's raw send (wifi_send_data, the SDK's raw_txrx example): a broadcast probe request at
         * 1 Mb/s, to capture what reaches the TX DMA (0x30F10 / 0x30F14 and the buffer, tx_s_buf) */
        static const uint8_t hdr[24] = {0x40, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
        static const uint8_t body[12] = {0x00, 0x00, 0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24};
        uint8_t *pkg = wifi_get_wifi_send_pkg_ptr();
        memcpy(pkg + 20, hdr, sizeof hdr);
        memcpy(pkg + 44, body, sizeof body);
        cap_phase = 12;
        wifi_send_data(sizeof body, 0);
        cap_phase = 13;
    }
    for (;;)
        ;
}
#else
int main(void)
{
    cap_phase = 1;
    wf_rf_common_init();
    cap_phase = 2;
    wifi_rf_on();
    cap_phase = 3;
    wfhw_iqr_init();
    cap_phase = 4;
    wifi_rf_off();
    set_wf_phy_init_flag(0);
    cap_phase = 5;
    wf_rf_common_init();
    cap_phase = 6;
    wifi_rf_on();
    cap_phase = 7;
    for (;;)
        ;
}
#endif

/* ---- the trims (board.c wifi_calibration_param of demo_wifi; the FM-1's own are VM 106 / 107 / 108 / 187) */
static const uint8_t cap_xosc[2] = {0xb, 0xb};
static const uint8_t cap_pa[7] = {1, 7, 4, 7, 11, 1, 7};
static const uint8_t cap_mcs[20] = {50, 50, 50, 50, 72, 72, 85, 80, 64, 64, 62, 52, 72, 90, 80, 64, 64, 64, 50, 43};
static uint8_t cap_rf_trim[68];
void wifi_get_xosc(uint8_t *p) { p[0] = cap_xosc[0]; p[1] = cap_xosc[1]; }
int wifi_get_pa_trim_data(uint8_t *p)
{
    int i;
    for (i = 0; i < 7; i++)
        p[i] = cap_pa[i];
    return 0;
}
void wifi_get_mcs_dgain(uint8_t *p)
{
    int i;
    for (i = 0; i < 20; i++)
        p[i] = cap_mcs[i];
}
int wifi_get_rf_trim_data(uint8_t *p, int n)
{
    int i;
    if (!CAP_RF_TRIM && !cap_trim_saved)
        return -1;                                /* (none stored yet: the full calibration) */
    for (i = 0; i < n && i < 68; i++)
        p[i] = cap_rf_trim[i];
    return 0;
}
int wifi_set_rf_trim_data(const uint8_t *p, int n)  /* the calibration's result: kept for a quick-start run */
{
    int i;
    for (i = 0; i < n && i < 68; i++)
        cap_rf_trim[i] = p[i];
    cap_trim_saved = 1;
    return 0;
}

/* ---- time */
void delay_us(uint32_t us) { fm1_delay_us(us); }
void msleep(uint32_t ms) { fm1_delay_ms(ms); }
void os_time_dly(uint32_t t) { fm1_delay_ms(t * 10u); }
uint32_t sys_timer_get_ms(void) { return fm1_ticks() / (FM1_TICKS_PER_US * 1000u); }
uint32_t OSGetTime(void) { return sys_timer_get_ms() / 10u; }
int time_lapse(uint32_t *t, uint32_t ms)
{
    uint32_t now = sys_timer_get_ms();
    if (!*t) {
        *t = now;
        return 0;
    }
    if (now - *t >= ms) {
        *t = now;
        return 1;
    }
    return 0;
}
int clk_get(const char *name) { (void)name; return 24000000; }   /* "osc": the FM-1's 24 MHz crystal */

/* ---- C library */
void *memcpy(void *d, const void *s, unsigned n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned n)
{
    uint8_t *a = d;
    while (n--)
        *a++ = (uint8_t)c;
    return d;
}
void *memmove(void *d, const void *s, unsigned n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    if (a < b)
        while (n--)
            *a++ = *b++;
    else
        while (n--)
            a[n] = b[n];
    return d;
}
unsigned strlen(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}
int memcmp(const void *x, const void *y, unsigned n)
{
    const uint8_t *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b)
            return *a - *b;
    return 0;
}
int bytecmp(const void *x, int v, unsigned n)
{
    const uint8_t *a = x;
    for (; n; n--)
        if (*a++ != (uint8_t)v)
            return 1;
    return 0;
}
static uint8_t cap_heap[360u * 1024u];           /* (the Ralink adapter and its RX / TX buffers; free is a no-op) */
volatile uint32_t cap_heap_fail;                  /* the size asked when the heap ran out (the image then stops) */
static uint32_t cap_heap_n;
void *zalloc(unsigned n)
{
    void *p;
    n = (n + 7u) & ~7u;
    if (cap_heap_n + n > sizeof cap_heap) {
        cap_heap_fail = n;
        for (;;)
            ;
    }
    p = cap_heap + cap_heap_n;
    cap_heap_n += n;
    memset(p, 0, n);
    return p;
}
void free(void *p) { (void)p; }
uint16_t CRC16(const void *p, unsigned n)
{
    const uint8_t *b = p;
    uint16_t c = 0;
    while (n--) {
        int i;
        c ^= (uint16_t)(*b++ << 8);
        for (i = 0; i < 8; i++)
            c = (uint16_t)(c & 0x8000u ? c << 1 ^ 0x1021u : c << 1);
    }
    return c;
}

/* ---- logging: silent (the trace is the output) */
int printf(const char *f, ...) { (void)f; return 0; }
int puts(const char *s) { (void)s; return 0; }
int putchar(int c) { return c; }
void put_buf(const void *p, int n) { (void)p; (void)n; }
void cpu_assert_debug(void) { for (;;) ; }
void P33_SYSTEM_RESET(void) { for (;;) ; }

/* ---- OS, interrupts, timers, power: stand-ins (the start-up runs on one thread, its timers never fire) */
static int cap_obj;
void *os_current_task_rom(void) { return &cap_obj; }
int os_sem_create(void *s, int n) { (void)s; (void)n; return 0; }
int os_sem_del(void *s, int b) { (void)s; (void)b; return 0; }
int os_sem_pend(void *s, int t) { (void)s; (void)t; return 0; }
int os_sem_post(void *s) { (void)s; return 0; }
int os_mutex_create(void *m) { (void)m; return 0; }
int os_mutex_del(void *m, int b) { (void)m; (void)b; return 0; }
int os_mutex_pend(void *m, int t) { (void)m; (void)t; return 0; }
int os_mutex_post(void *m) { (void)m; return 0; }
void vPortYield(void) {}
void local_irq_disable(void) {}
void local_irq_enable(void) {}
void request_irq(uint8_t n, uint8_t pri, void (*h)(void), uint8_t cpu) { (void)n; (void)pri; (void)h; (void)cpu; }
void unrequest_irq(uint8_t n) { (void)n; }
uint16_t sys_timer_add_to_task(const char *t, void *p, void (*f)(void *), uint32_t ms) { (void)t; (void)p; (void)f; (void)ms; return 1; }
uint16_t sys_timeout_add_to_task(const char *t, void *p, void (*f)(void *), uint32_t ms) { (void)t; (void)ms; f(p); return 1; }   /* (one-shot: run now, so tx_rx_bw_set stores the RF trim) */
void sys_timer_del(uint16_t id) { (void)id; }
uint16_t usr_timeout_add(void *p, void (*f)(void *), uint32_t ms, int pri) { (void)p; (void)f; (void)ms; (void)pri; return 1; }
void usr_timeout_del(uint16_t id) { (void)id; }
int thread_fork(const char *n, int pri, int stk, int q, int *pid, void (*f)(void *), void *p) { (void)n; (void)pri; (void)stk; (void)q; (void)pid; (void)f; (void)p; return 0; }
void thread_kill(int *pid, int how) { (void)pid; (void)how; }
#if !CAP_STA
int get_tasklet_exit_flag(void) { return 0; }
#endif
int adc_add_sample_ch(int ch) { (void)ch; return 0; }
int adc_get_value(int ch) { (void)ch; return 512; }
int gpio_direction_input(int g) { (void)g; return 0; }
uint8_t low_power_get_config(void) { return 0; }
int low_power_check_bt_exist(void) { return 0; }
void *low_power_get(void *p, void *ops) { (void)p; (void)ops; return 0; }
void low_power_put(void *p) { (void)p; }
void low_power_request(void) {}
int check_low_power_hw_if_locked(void) { return 0; }
void low_power_hw_unsleep_lock(void) {}
void low_power_hw_unsleep_unlock(void) {}
int host_set_timing(void *h, int t) { (void)h; (void)t; return 0; }
int sdio_host_send_command(uint32_t a, uint16_t c) { (void)a; (void)c; return 0; }
#ifndef CAP_WLC
#define CAP_WLC 0
#endif
#if !CAP_WLC
void wlc_main(void) {}                            /* (the coexistence set-up: CAP_WLC links the SDK's wlc.c) */
#endif

/* ---- the Ralink driver above the shim: not part of the start-up (CAP_STA links the real one) */
#if !CAP_STA
int RTUSBReadMACRegister(void *a, uint16_t r, uint32_t *v) { (void)a; (void)r; *v = 0; return 0; }
int RTUSBWriteMACRegister(void *a, uint16_t r, uint32_t v) { (void)a; (void)r; (void)v; return 0; }
int rtusb_probe(void *i, const void *id) { (void)i; (void)id; return 0; }
void rtusb_disconnect(void *i) { (void)i; }
int rt2870_suspend(void *i, int m) { (void)i; (void)m; return 0; }
int rt2870_resume(void *i) { (void)i; return 0; }
void *net_dev_find(const char *n) { (void)n; return 0; }
void netif_start_queue(void *d) { (void)d; }
int wl_get_pad(void) { return 0; }
#endif
int wifi_lowpower_mode;
#if CAP_STA                                       /* AirKiss's AES (phone set-up, not part of the bring-up) */
void clear_wpa_scan_record(void) {}               /* (wpa_supplicant's: not linked) */
/* the C library the driver uses beyond the start-up */
int errno;
void *malloc(unsigned n) { return zalloc(n); }
void *calloc(unsigned a, unsigned b) { return zalloc(a * b); }
void *kmalloc(unsigned n, int f) { (void)f; return zalloc(n); }
void *kzalloc(unsigned n, int f) { (void)f; return zalloc(n); }
void *vmalloc(unsigned n) { return zalloc(n); }
void kfree(void *p) { (void)p; }
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}
int strncmp(const char *a, const char *b, unsigned n)
{
    for (; n; n--, a++, b++)
        if (*a != *b || !*a)
            return (uint8_t)*a - (uint8_t)*b;
    return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) ; return r; }
char *strncpy(char *d, const char *s, unsigned n) { char *r = d; for (; n && *s; n--) *d++ = *s++; while (n--) *d++ = 0; return r; }
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c) { for (; *s; s++) if (*s == (char)c) return (char *)s; return c ? 0 : (char *)s; }
char *strpbrk(const char *s, const char *a) { for (; *s; s++) if (strchr(a, *s)) return (char *)s; return 0; }
unsigned strspn(const char *s, const char *a) { unsigned n = 0; while (s[n] && strchr(a, s[n])) n++; return n; }
char *strsep(char **sp, const char *d)
{
    char *s = *sp, *e;
    if (!s)
        return 0;
    e = strpbrk(s, d);
    if (e)
        *e++ = 0;
    *sp = e;
    return s;
}
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static uint32_t cap_rnd = 1;
int rand(void) { cap_rnd = cap_rnd * 1103515245u + 12345u; return (int)(cap_rnd >> 1); }
int sprintf(char *d, const char *f, ...) { (void)f; if (d) *d = 0; return 0; }
int snprintf(char *d, unsigned n, const char *f, ...) { (void)f; if (d && n) *d = 0; return 0; }
void mdelay(uint32_t ms) { fm1_delay_ms(ms); }
void udelay(uint32_t us) { fm1_delay_us(us); }
/* OS and interrupt calls the driver makes */
void *os_current_task(void) { return &cap_obj; }
int os_mutex_valid(void *m) { (void)m; return 1; }
int os_sem_set(void *s, int n) { (void)s; (void)n; return 0; }
void os_task_yield(void) {}
int thread_rpc(void *a, void *b) { (void)a; (void)b; return 0; }
void __local_irq_disable(void) {}
void __local_irq_enable(void) {}
void sys_local_irq_disable(void) {}
void sys_local_irq_enable(void) {}
void low_power_exit_request(void) {}
/* the SDK's state machine library (hsm.a), lwIP, wpa_supplicant and the AP: not part of the bring-up */
void *HSM_Create(void *a, const char *n, void *s) { (void)a; (void)n; (void)s; return &cap_obj; }
void *HSM_STATE_Create(void *a, const char *n, void *h, void *p) { (void)a; (void)n; (void)h; (void)p; return &cap_obj; }
void *HSM_GetState(void *h) { (void)h; return &cap_obj; }
void HSM_Run(void *h, int e, void *p) { (void)h; (void)e; (void)p; }
void HSM_Tran(void *h, void *s, void *a, void *p) { (void)h; (void)s; (void)a; (void)p; }
int Init_LwIP(void) { return 0; }
void lwip_netif_set_up(void) {}
void lwip_renew(void) {}
void lwip_dhcp_release_and_stop(void) {}
void dhcps_init(void) {}
void dhcps_uninit(void) {}
void eloop_run(void) {}
int wifi_recv_poll(void) { return 0; }
int wl_ap_init(void) { return 0; }
int syscfg_read(int id, void *b, int n) { (void)id; (void)b; (void)n; return -1; }   /* (nothing stored) */
int syscfg_write(int id, const void *b, int n) { (void)id; (void)b; return n; }
int get_bss_table_record(void *b, int n) { (void)b; (void)n; return -1; }
void set_bss_table_record(void *b, int n) { (void)b; (void)n; }
void AsicRxAntEvalTimeout(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; }
void BeaconUpdateExec(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; }
long simple_strtol(const char *s, char **e, unsigned base)
{
    long v = 0;
    int neg = *s == '-';
    if (neg || *s == '+')
        s++;
    while (*s >= '0' && *s <= '9')
        v = v * (long)base + (*s++ - '0');
    if (e)
        *e = (char *)s;
    return neg ? -v : v;
}
void mbedtls_aes_init(void *c) { (void)c; }
void mbedtls_aes_free(void *c) { (void)c; }
int mbedtls_aes_setkey_dec(void *c, const void *k, unsigned b) { (void)c; (void)k; (void)b; return -1; }
int mbedtls_aes_setkey_enc(void *c, const void *k, unsigned b) { (void)c; (void)k; (void)b; return -1; }
int mbedtls_aes_crypt_cbc(void *c, int m, unsigned n, void *iv, const void *in, void *out) { (void)c; (void)m; (void)n; (void)iv; (void)in; (void)out; return -1; }
#endif
