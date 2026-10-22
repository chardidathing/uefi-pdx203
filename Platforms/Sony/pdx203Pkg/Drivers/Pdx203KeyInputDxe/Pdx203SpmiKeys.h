/** @file
  Hardware keys of the Xperia 1 II through the SPMI PMIC arbiter, as pure register-pointer code (host-testable).

  facts, all read back on the phone by the ios-sweet shim and kernel (bsd/dev/sweet_keys.c, tools/boards/pdx203.py):
    power key    pm8150 sid 0, pon peripheral pid 0x08 (reg 0x800), PON_RT_STS at +0x10, KPDPWR_N_SET = bit 0, set = pressed
    volume up    same register, RESIN_N_SET = bit 1, set = pressed (sony wires resin to volume up)
    volume down  pm8150 sid 0 gpio1, peripheral pid 0xc0 (reg 0xc000), RT_STS at +0x10 bit 0, active low (gpio_keys flags 1)
  the arbiter is version 5 (core 0xc440000 reads 0x50010000, 512 apids), the apid map lives at core + 0x900, the observer
  channel of an apid at obsrvr 0xe600000 + 0x10000 * ee + 0x80 * apid (ee 0), the owner of a channel at cnfg 0xc40a000 + 0x700.
  one ppid can be mapped more than once on v5, the apps-owned entry wins. a register byte is read with an extended-read command
  on the observer channel, that is a read-only path, nothing is configured, acknowledged or reset in the pmic.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef PDX203_SPMI_KEYS_H_
#define PDX203_SPMI_KEYS_H_

#include <stdint.h>

// the hardware clears the channel status when a command is written, a host model has no way to do that behind the reader's back,
// so the test defines this to serve the command synchronously. nothing on the device.
#ifndef SK_CMD_ISSUED
#define SK_CMD_ISSUED(channel) ((void) 0)
#endif

enum {
  SK_VERSION_V5_MIN = 0x50000000u, SK_VERSION_V7_MIN = 0x70000000u,
  SK_FEATURES = 0x04, SK_FEATURES_APIDS_MASK = 0x7ffu, SK_MAX_APIDS = 512,
  SK_APID_MAP = 0x900, SK_APID_OWNER = 0x700,
  SK_OBS_EE_STRIDE = 0x10000, SK_OBS_APID_STRIDE = 0x80,
  SK_CMD = 0x00, SK_STATUS = 0x08, SK_RDATA0 = 0x18,
  SK_OP_EXT_READL = 1,
  SK_STATUS_DONE = 1u << 0, SK_STATUS_ERRORS = 0xeu,
  SK_POLLS = 100000,
  SK_PPID_PON = 0x008, SK_PPID_GPIO1 = 0x0c0, SK_REG_RT_STS = 0x10,
  SK_REG_GPIO_SUBTYPE = 0x05, SK_REG_GPIO_MODE = 0x40, SK_REG_GPIO_EN = 0x46,
  SK_PON_KPDPWR = 1u << 0, SK_PON_RESIN = 1u << 1,
  SK_KEY_POWER = 1u << 0, SK_KEY_VOLDOWN = 1u << 1, SK_KEY_VOLUP = 1u << 2
};

struct sk_arbiter {
  volatile uint32_t *core;   // 0x1100 bytes, the apid map at 0x900
  volatile uint32_t *cnfg;   // channel owners at 0x700
  volatile uint32_t *obs;    // observer channels of this ee and above (ee 0 starts at the base)
  uint8_t ee;
};

// 0 and *apid on success. 1 unknown layout, 2 no apids, 3 ppid not mapped.
static inline int sk_find_apid (const struct sk_arbiter *a, uint32_t ppid, int *apid)
{
  uint32_t version = a->core[0], apids = a->core[SK_FEATURES / 4] & SK_FEATURES_APIDS_MASK;
  *apid = -1;
  if (version < SK_VERSION_V5_MIN || version >= SK_VERSION_V7_MIN) return 1;
  if (!apids || apids > SK_MAX_APIDS) return 2;
  for (uint32_t i = 0; i < apids; ++i) {
    uint32_t map = a->core[(SK_APID_MAP + 4 * i) / 4];
    if (!map || ((map >> 8) & 0xfff) != ppid) continue;
    uint32_t owner = a->cnfg[(SK_APID_OWNER + 4 * i) / 4] & 7;
    if (*apid < 0 || owner == a->ee) *apid = (int) i;   // the apps-owned channel wins when the ppid is mapped twice
  }
  return *apid < 0 ? 3 : 0;
}

// one register byte through the observer channel. 0 ok, 1 timeout, 2 the arbiter reported an error.
static inline int sk_read8 (const struct sk_arbiter *a, int apid, uint8_t reg, uint8_t *value)
{
  volatile uint32_t *ch = a->obs + (SK_OBS_EE_STRIDE * a->ee + SK_OBS_APID_STRIDE * (uint32_t) apid) / 4;
  ch[SK_CMD / 4] = ((uint32_t) SK_OP_EXT_READL << 27) | ((uint32_t) reg << 4);
  SK_CMD_ISSUED (ch);
  for (unsigned n = 0; n < SK_POLLS; ++n) {
    uint32_t s = ch[SK_STATUS / 4];
    if (s & SK_STATUS_DONE) {
      if (s & SK_STATUS_ERRORS) return 2;
      *value = (uint8_t) ch[SK_RDATA0 / 4];
      return 0;
    }
  }
  return 1;
}

struct sk_keys { int pon, gpio; };   // apids, -1 = not available

// the gpio belongs to the loader's configuration, it is only used when it reads back as an enabled input (the kernel does the same
// check). 1 when usable.
static inline int sk_gpio_is_input (const struct sk_arbiter *a, int apid)
{
  uint8_t mode = 0, en = 0, subtype = 0;
  if (sk_read8 (a, apid, SK_REG_GPIO_MODE, &mode) || sk_read8 (a, apid, SK_REG_GPIO_EN, &en) ||
      sk_read8 (a, apid, SK_REG_GPIO_SUBTYPE, &subtype)) return 0;
  unsigned dir = (subtype >= 0x10 && subtype <= 0x13) ? (mode & 3) : ((mode >> 4) & 7);
  return (en & 0x80) && (dir == 0 || dir == 2);
}

static inline int sk_open (const struct sk_arbiter *a, struct sk_keys *k)
{
  int rc = sk_find_apid (a, SK_PPID_PON, &k->pon);
  if (rc) return rc;
  if (sk_find_apid (a, SK_PPID_GPIO1, &k->gpio) || !sk_gpio_is_input (a, k->gpio)) k->gpio = -1;   // power and volume up still work
  return 0;
}

// pressed keys as SK_KEY_* bits. 0 ok, non-zero when a read failed (nothing is reported pressed then).
static inline int sk_sample (const struct sk_arbiter *a, const struct sk_keys *k, uint8_t *pressed)
{
  uint8_t v = 0;
  *pressed = 0;
  int rc = sk_read8 (a, k->pon, SK_REG_RT_STS, &v);
  if (rc) return rc;
  if (v & SK_PON_KPDPWR) *pressed |= SK_KEY_POWER;
  if (v & SK_PON_RESIN) *pressed |= SK_KEY_VOLUP;
  if (k->gpio >= 0) {
    rc = sk_read8 (a, k->gpio, SK_REG_RT_STS, &v);
    if (rc) { *pressed = 0; return rc; }
    if (!(v & 1)) *pressed |= SK_KEY_VOLDOWN;      // active low
  }
  return 0;
}

// debounce and auto-repeat. a key counts as pressed after two identical samples. volume keys repeat while held, first after
// `delay` polls, then every `period` polls. step() returns the keys to report now (new presses plus repeats).
struct sk_filter { uint8_t last, stable; uint16_t held[3]; };

static inline uint8_t sk_step (struct sk_filter *f, uint8_t sample, uint16_t delay, uint16_t period)
{
  uint8_t out = 0;
  if (sample == f->last) {
    uint8_t fresh = (uint8_t) (sample & ~f->stable);
    f->stable = sample;
    out = fresh;
    for (unsigned i = 0; i < 3; ++i) {
      uint8_t bit = (uint8_t) (1u << i);
      if (!(sample & bit)) { f->held[i] = 0; continue; }
      if (fresh & bit) { f->held[i] = 0; continue; }
      ++f->held[i];
      if (bit != SK_KEY_POWER && f->held[i] >= delay && (f->held[i] - delay) % period == 0) out |= bit;
    }
  }
  f->last = sample;
  return out;
}

#endif
