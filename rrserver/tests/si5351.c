// Si5351 register arithmetic and fault sequencing; no physical hardware.
// Part of RustyRig; MIT license.
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <rrserver/si5351.h>

typedef struct {
   uint8_t registers[256];
   unsigned writes, fail_at;
   unsigned reset_writes, pll_writes;
   bool stuck, fail_read;
} fake_t;
static bool read_reg(void *context, uint8_t reg, uint8_t *value) {
   fake_t *f = context;

   if (f->fail_read) {
      errno = EIO;

      return false;
   }
   *value = f->stuck && reg == 0 ? 0x80 : f->registers[reg];

   return true;
}
static bool write_reg(void *context, uint8_t reg, const uint8_t *value, size_t count) {
   fake_t *f = context;
   f->writes++;

   if (f->writes == f->fail_at) {
      errno = EIO;

      return false;
   }

   if (reg == 177) {
      f->reset_writes++;
   }

   if (reg == 26) {
      f->pll_writes++;
   }
   assert((size_t)reg + count <= sizeof(f->registers));
   memcpy(f->registers + reg, value, count);

   return true;
}
static double ratio(const uint8_t *r) {
   unsigned p1 = ((r[2] & 3) << 16) | (r[3] << 8) | r[4];
   unsigned p2 = ((r[5] & 15) << 16) | (r[6] << 8) | r[7];
   unsigned p3 = ((r[5] & 0xf0) << 12) | (r[0] << 8) | r[1];
   assert(p3);

   return (p1 + 512 + (double)p2 / p3) / 128;
}
int main(void) {
   rr_si5351_config_t c = {
      25000000, 0, 10, 2
   };
   rr_si5351_plan_t p;
   const uint8_t pll[] = {
      0, 1, 0, 0x10, 0, 0, 0, 0
   };
   const uint8_t ms[] = {
      0, 1, 0, 0x2b, 0, 0, 0, 0
   };
   assert(rr_si5351_plan(&c, 10000000, &p));
   assert(!memcmp(p.pll, pll, 8) && !memcmp(p.multisynth, ms, 8) && p.control == 0x4c);
   const unsigned rates[] = {
      8000, 10000, 100000, 999999, 1000000, 3579545, 7100000, 14074000, 40035000, 70000000, 99999999, 100000000
   };
   const int corrections[] = {
      -100000, -12345, 0, 12345, 100000
   };

   for (unsigned crystal = 25000000 ; crystal <= 27000000 ; crystal += 2000000) {
      for (unsigned j = 0 ; j < sizeof(corrections) / sizeof(*corrections) ; j++) {
         for (unsigned i = 0 ; i < sizeof(rates) / sizeof(*rates) ; i++) {
            c.crystal_hz = crystal;
            c.correction_ppb = corrections[j];
            assert(rr_si5351_plan(&c, rates[i], &p));
            double actual = crystal * (1 + c.correction_ppb / 1e9) * ratio(p.pll) / ratio(p.multisynth) / p.r_divider;
            assert(fabs(actual - rates[i]) < 8.0);
            assert(ratio(p.multisynth) >= 9 && ratio(p.multisynth) <= 900);
         }
      }
   }

   assert(!rr_si5351_plan(&c, 7999, &p) && !rr_si5351_plan(&c, 100000001, &p));
   c.crystal_hz = 26000000;
   assert(!rr_si5351_plan(&c, 10000000, &p));
   c = (rr_si5351_config_t) {
      25000000, 0, 10, 2
   };
   fake_t f = {
      0
   };
   rr_si5351_io_t io = {
      &f, read_reg, write_reg, NULL
   };
   rr_si5351_t clock;
   assert(rr_si5351_start(&clock, &c, &io));
   unsigned startup_writes = f.writes;
   assert(clock.ready && f.registers[3] == 0xff && f.registers[183] == 0xd2);
   assert(rr_si5351_set(&clock, 0, 10000000));
   assert(f.registers[3] == 0xfe);
   uint8_t clk0[8];
   memcpy(clk0, f.registers + 42, 8);
   assert(rr_si5351_set(&clock, 2, 7100000));
   assert(f.registers[3] == 0xfa && !memcmp(clk0, f.registers + 42, 8));
   assert(f.pll_writes == 1 && f.reset_writes == 1);
   unsigned writes = f.writes;
   assert(!rr_si5351_set(&clock, 3, 10000000) && !rr_si5351_set(&clock, 1, 1) && f.writes == writes);
   assert(rr_si5351_set(&clock, 0, 0) && f.registers[3] == 0xfb);
   assert(rr_si5351_stop(&clock) && !clock.ready && f.registers[3] == 0xff);

   // Every initialization write failure must leave software faulted and attempt disable.
   for (unsigned fail = 1 ; fail <= startup_writes ; fail++) {
      f = (fake_t) {
         .fail_at = fail
      };
      assert(!rr_si5351_start(&clock, &c, &io));
      assert(!clock.ready && f.registers[3] == 0xff);
   }

   // Every retune write failure disables all outputs, including another active clock.
   for (unsigned fail = 1 ; fail <= 4 ; fail++) {
      f = (fake_t) {
         0
      };
      assert(rr_si5351_start(&clock, &c, &io));
      assert(rr_si5351_set(&clock, 0, 10000000));
      f.fail_at = f.writes + fail;
      assert(!rr_si5351_set(&clock, 2, 7100000));
      assert(!clock.ready && f.registers[3] == 0xff);
      assert(!rr_si5351_set(&clock, 0, 10000000));
   }

   f = (fake_t) {
      .fail_read = true
   };
   assert(!rr_si5351_start(&clock, &c, &io) && errno == EIO && !clock.ready);
   f = (fake_t) {
      .stuck = true
   };
   assert(!rr_si5351_start(&clock, &c, &io) && errno == ETIMEDOUT);
   puts("Si5351 arithmetic, independent tuning and failure sequencing passed");

   return 0;
}
