// host test of Pdx203SpmiKeys.h against a software model of the v5 arbiter's observer channels, served synchronously
// through SK_CMD_ISSUED the way the hardware answers a command (status cleared at the command write, data and done after).
// build and run: cc -O1 -Wall keys_host.c -o keys_host && ./keys_host
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void model_serve (volatile uint32_t *ch);
#define SK_CMD_ISSUED(channel) model_serve (channel)
#include "../Pdx203SpmiKeys.h"

#define MODEL_APIDS 64

static volatile uint32_t core[0x1100 / 4];
static volatile uint32_t cnfg[0x1000 / 4];
static volatile uint32_t obs[(SK_OBS_APID_STRIDE * MODEL_APIDS) / 4];
static int fail;
static uint8_t pon_sts, gpio_sts, gpio_mode = 0x00, gpio_en = 0x80, gpio_subtype = 0x10;
static int pon_apid = 13, gpio_apid = 23, gpio_other_apid = 5;

static void model_serve (volatile uint32_t *ch)
{
  int apid = (int) ((ch - obs) * 4 / SK_OBS_APID_STRIDE);
  uint32_t cmd = ch[SK_CMD / 4];
  unsigned reg = (cmd >> 4) & 0xff;
  assert ((cmd >> 27) == SK_OP_EXT_READL && (cmd & 7) == 0);
  ch[SK_STATUS / 4] = 0;
  if (fail) { ch[SK_STATUS / 4] = SK_STATUS_DONE | 2; return; }
  uint8_t v = 0xee;
  if (apid == pon_apid && reg == SK_REG_RT_STS) v = pon_sts;
  if (apid == gpio_apid) {
    if (reg == SK_REG_RT_STS) v = gpio_sts;
    if (reg == SK_REG_GPIO_MODE) v = gpio_mode;
    if (reg == SK_REG_GPIO_EN) v = gpio_en;
    if (reg == SK_REG_GPIO_SUBTYPE) v = gpio_subtype;
  }
  ch[SK_RDATA0 / 4] = v;
  ch[SK_STATUS / 4] = SK_STATUS_DONE;
}

int main (void)
{
  struct sk_arbiter arb = { core, cnfg, obs, 0 };
  struct sk_keys k;
  uint8_t pressed;
  int apid;

  core[0] = 0x50010000u; core[SK_FEATURES / 4] = 40;
  core[(SK_APID_MAP + 4 * 3) / 4] = (0x070u << 8) | 1;
  core[(SK_APID_MAP + 4 * pon_apid) / 4] = (SK_PPID_PON << 8) | 1;
  // gpio1 is mapped twice (v5 does that), the first entry belongs to another ee, the apps one must win
  core[(SK_APID_MAP + 4 * gpio_other_apid) / 4] = ((uint32_t) SK_PPID_GPIO1 << 8) | 1;
  cnfg[(SK_APID_OWNER + 4 * gpio_other_apid) / 4] = 2;
  core[(SK_APID_MAP + 4 * gpio_apid) / 4] = ((uint32_t) SK_PPID_GPIO1 << 8) | 1;
  cnfg[(SK_APID_OWNER + 4 * gpio_apid) / 4] = 0;
  assert (sk_find_apid (&arb, SK_PPID_PON, &apid) == 0 && apid == pon_apid);
  assert (sk_find_apid (&arb, SK_PPID_GPIO1, &apid) == 0 && apid == gpio_apid);
  assert (sk_find_apid (&arb, 0x555, &apid) == 3 && apid == -1);
  core[0] = 0x40000000u; assert (sk_find_apid (&arb, SK_PPID_PON, &apid) == 1);
  core[0] = 0x70000000u; assert (sk_find_apid (&arb, SK_PPID_PON, &apid) == 1);
  core[0] = 0x50010000u; core[SK_FEATURES / 4] = 0; assert (sk_find_apid (&arb, SK_PPID_PON, &apid) == 2);
  core[SK_FEATURES / 4] = 40;

  assert (sk_open (&arb, &k) == 0 && k.pon == pon_apid && k.gpio == gpio_apid);
  struct { uint8_t pon, gpio, want; } cases[] = {
    {0x00, 0x01, 0},                                   // nothing pressed (gpio high = released)
    {0x01, 0x01, SK_KEY_POWER}, {0x02, 0x01, SK_KEY_VOLUP}, {0x03, 0x01, SK_KEY_POWER | SK_KEY_VOLUP},
    {0x00, 0x00, SK_KEY_VOLDOWN},                      // gpio low = pressed
    {0xfc, 0x01, 0},                                   // other pon status bits are not keys
    {0x01, 0x00, SK_KEY_POWER | SK_KEY_VOLDOWN},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
    pon_sts = cases[i].pon; gpio_sts = cases[i].gpio; fail = 0;
    int rc = sk_sample (&arb, &k, &pressed);
    if (rc || pressed != cases[i].want) fprintf (stderr, "case %u rc=%d got=%x want=%x\n", i, rc, pressed, cases[i].want);
    assert (rc == 0 && pressed == cases[i].want);
  }
  fail = 1; assert (sk_sample (&arb, &k, &pressed) != 0 && pressed == 0);
  fail = 0;
  struct sk_keys nogpio = { pon_apid, -1 };
  pon_sts = 0x01; gpio_sts = 0; assert (sk_sample (&arb, &nogpio, &pressed) == 0 && pressed == SK_KEY_POWER);
  // a gpio the loader left disabled or as an output is withheld, both register layouts
  gpio_en = 0x00; assert (sk_open (&arb, &k) == 0 && k.gpio == -1); gpio_en = 0x80;
  gpio_mode = 0x01; assert (sk_open (&arb, &k) == 0 && k.gpio == -1); gpio_mode = 0x00;
  gpio_subtype = 0x0b; gpio_mode = 0x20; assert (sk_open (&arb, &k) == 0 && k.gpio == gpio_apid);   // old layout, dir 2 = input
  gpio_subtype = 0x0b; gpio_mode = 0x10; assert (sk_open (&arb, &k) == 0 && k.gpio == -1);          // old layout, dir 1 = output
  // the pon apid has no stale answer from the gpio reads before it
  gpio_subtype = 0x10; gpio_mode = 0x00; pon_sts = 0x02; gpio_sts = 0x01;
  assert (sk_open (&arb, &k) == 0 && sk_sample (&arb, &k, &pressed) == 0 && pressed == SK_KEY_VOLUP);

  // debounce and repeat, two identical samples make a press, volume keys repeat after `delay` polls, then every `period`, power never
  struct sk_filter f = {0};
  assert (sk_step (&f, SK_KEY_VOLUP, 4, 2) == 0);                       // first sample alone, not yet
  assert (sk_step (&f, SK_KEY_VOLUP, 4, 2) == SK_KEY_VOLUP);            // confirmed
  unsigned repeats = 0, polls = 0;
  for (; polls < 12; ++polls) repeats += sk_step (&f, SK_KEY_VOLUP, 4, 2) == SK_KEY_VOLUP;
  assert (repeats == 5);                                               // at held = 4, 6, 8, 10, 12
  assert (sk_step (&f, 0, 4, 2) == 0 && sk_step (&f, 0, 4, 2) == 0);   // release
  assert (sk_step (&f, SK_KEY_VOLDOWN, 4, 2) == 0 && sk_step (&f, SK_KEY_VOLDOWN, 4, 2) == SK_KEY_VOLDOWN);
  assert (sk_step (&f, SK_KEY_VOLDOWN, 4, 2) == 0);                    // a glitch of one sample is ignored below
  f = (struct sk_filter){0};
  assert (sk_step (&f, SK_KEY_POWER, 4, 2) == 0 && sk_step (&f, SK_KEY_POWER, 4, 2) == SK_KEY_POWER);
  for (unsigned i = 0; i < 40; ++i) assert (sk_step (&f, SK_KEY_POWER, 4, 2) == 0);   // power does not repeat
  f = (struct sk_filter){0};
  assert (sk_step (&f, SK_KEY_VOLUP, 4, 2) == 0 && sk_step (&f, 0, 4, 2) == 0 && sk_step (&f, SK_KEY_VOLUP, 4, 2) == 0);   // bounce, no press
  puts ("PASS: arbiter lookup, owner preference, key decoding, gpio input check, read errors, debounce, repeat");
  return 0;
}
