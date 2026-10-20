// Si5351A register generation and sequencing. Part of RustyRig; MIT license.
// Register equations/layout: Skyworks AN619 (10-MSOP/20-QFN), rev 0.8.
#include <errno.h>
#include <string.h>
#include <rrserver/si5351.h>
#define PLL_HZ 900000000ULL
#define DENOM 1048575ULL

static bool valid(const rr_si5351_config_t *c) {
   return c && (c->crystal_hz == 25000000 || c->crystal_hz == 27000000) &&
          c->correction_ppb >= -100000 && c->correction_ppb <= 100000 &&
          (c->load_pf == 6 || c->load_pf == 8 || c->load_pf == 10) && c->drive_ma >= 2 && c->drive_ma <= 8 && !(c->drive_ma & 1);
}

/* Encode a + b/c. Numerators are bounded so all intermediates fit uint64_t. */
static void parameters(uint64_t numerator, uint64_t denominator, uint8_t bytes[8], bool *integer) {
   uint64_t a = numerator / denominator;
   uint64_t b = ((numerator % denominator) * DENOM + denominator / 2) / denominator;

   if (b == DENOM) {
      a++;
      b = 0;
   }
   uint64_t c = b ? DENOM : 1;
   uint64_t fraction = 128 * b / c;
   uint32_t p1 = 128 * a + fraction - 512;
   uint32_t p2 = 128 * b - c * fraction;
   uint32_t p3 = c;
   bytes[0] = p3 >> 8;
   bytes[1] = p3;
   bytes[2] = (p1 >> 16) & 3;
   bytes[3] = p1 >> 8;
   bytes[4] = p1;
   bytes[5] = ((p3 >> 12) & 0xf0) | ((p2 >> 16) & 15);
   bytes[6] = p2 >> 8;
   bytes[7] = p2;
   *integer = !b && !(a & 1);
}

bool rr_si5351_plan(const rr_si5351_config_t *c, uint32_t hz, rr_si5351_plan_t *p) {
   if (!valid(c) || !p || hz < 8000 || hz > 100000000) {
      errno = EINVAL;

      return false;
   }
   memset(p, 0, sizeof(*p));
   bool integer;
   // Factor the reference correction before encoding, avoiding 128-bit arithmetic.
   parameters(PLL_HZ * 1000000000ULL / c->crystal_hz, (uint64_t) (1000000000LL + c->correction_ppb), p->pll, &integer);
   unsigned shift = 0;
   uint64_t divided = hz;
   while (PLL_HZ > 900 * divided && shift < 7) {
      divided *= 2;
      shift++;
   }
   parameters(PLL_HZ, divided, p->multisynth, &integer);
   p->multisynth[2] |= shift << 4;
   p->r_divider = 1U << shift;
   // Own MultiSynth source, PLLA, not inverted; integer mode only for even ratios.
   p->control = 0x0c | (integer ? 0x40 : 0) | (c->drive_ma / 2 - 1);

   return true;
}

static bool byte(rr_si5351_t *s, uint8_t reg, uint8_t value) {
   return s->io.write(s->io.context, reg, &value, 1);
}

static bool fault(rr_si5351_t *s) {
   int saved = errno ? errno : EIO;
   s->ready = false;
   s->disabled = 0xff;
   byte(s, 3, 0xff);
   errno = saved;

   return false;
}

static bool wait_status(rr_si5351_t *s, uint8_t mask) {
   for (unsigned i = 0 ; i < 200 ; i++) {
      uint8_t status;

      if (!s->io.read(s->io.context, 0, &status)) {
         return false;
      }

      if (!(status & mask)) {
         return true;
      }

      if (s->io.wait) {
         s->io.wait(s->io.context);
      }
   }

   errno = ETIMEDOUT;

   return false;
}

bool rr_si5351_start(rr_si5351_t *s, const rr_si5351_config_t *c, const rr_si5351_io_t *io) {
   rr_si5351_plan_t plan;

   if (!s || !io || !io->read || !io->write || !rr_si5351_plan(c, 10000000, &plan)) {
      errno = EINVAL;

      return false;
   }
   *s = (rr_si5351_t) {
      .config = *c, .io = *io, .disabled = 0xff
   };
   uint8_t off[8];
   memset(off, 0x80, sizeof(off));
   uint8_t spread;

   if (!wait_status(s, 0x80) || !byte(s, 3, 0xff) ||
      !s->io.write(s->io.context, 16, off, sizeof(off)) ||
      !byte(s, 15, 0) || !byte(s, 183, ((c->load_pf - 4) / 2 << 6) | 0x12) ||
      !s->io.read(s->io.context, 149, &spread) || !byte(s, 149, spread & 0x7f) ||
      !s->io.write(s->io.context, 26, plan.pll, 8) || !byte(s, 165, 0) || !byte(s, 166, 0) || !byte(s, 167, 0) || !byte(s, 177, 0x20) || !wait_status(s, 0xa8))
   {
      return fault(s);
   }
   s->ready = true;

   return true;
}

bool rr_si5351_set(rr_si5351_t *s, unsigned output, uint32_t hz) {
   rr_si5351_plan_t plan;

   if (!s || !s->ready || output > 2 || (hz && !rr_si5351_plan(&s->config, hz, &plan))) {
      errno = EINVAL;

      return false;
   }
   uint8_t mask = s->disabled | (1U << output);

   if (!byte(s, 3, mask)) {
      return fault(s);
   }
   s->disabled = mask;

   if (!hz) {
      if (!byte(s, 16 + output, 0x80)) {
         return fault(s);
      }

      return true;
   }

   if (!s->io.write(s->io.context, 42 + 8 * output, plan.multisynth, 8) ||
      !byte(s, 16 + output, plan.control) || !wait_status(s, 0xa8) || !byte(s, 3, mask & ~(1U << output))) {
      return fault(s);
   }
   s->disabled = mask & ~(1U << output);

   return true;
}

bool rr_si5351_stop(rr_si5351_t *s) {
   if (!s || !s->io.write) {
      errno = EINVAL;

      return false;
   }

   if (!byte(s, 3, 0xff)) {
      return fault(s);
   }
   s->disabled = 0xff;
   s->ready = false;

   return true;
}
