// Si5351A three-output clock synthesis. Part of RustyRig; MIT license.
#ifndef RRSERVER_SI5351_H
#define RRSERVER_SI5351_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rrserver/hardware.h>

typedef struct {
   uint32_t crystal_hz;         // 25 or 27 MHz crystal
   int32_t correction_ppb;      // Positive means crystal runs fast
   unsigned load_pf, drive_ma;  // 6/8/10 pF; 2/4/6/8 mA
} rr_si5351_config_t;
typedef struct {
   uint8_t pll[8], multisynth[8], control;
   unsigned r_divider;
} rr_si5351_plan_t;
/* Pure integer planner, CLK0..2, 8 kHz..100 MHz. All outputs use fixed PLLA. Frequency is the requested clock pin frequency, not the radio dial. */
bool rr_si5351_plan(const rr_si5351_config_t *, uint32_t hz, rr_si5351_plan_t *);

/* Transport callbacks return true on success, errno on failure. */
typedef struct {
   void *context;
   bool (*read)(void *, uint8_t reg, uint8_t *value);
   bool (*write)(void *, uint8_t reg, const uint8_t *, size_t);
   void (*wait)(void *);        // Wait approximately 1 ms between status polls
} rr_si5351_io_t;
typedef struct {
   rr_si5351_config_t config;
   rr_si5351_io_t io;
   uint8_t disabled;
   bool ready;
} rr_si5351_t;
/* Initialization disables ALL chip outputs. On I/O error, best-effort disable ALL outputs and require reinitialization; never report a failed tune as ready. */
bool rr_si5351_start(rr_si5351_t *, const rr_si5351_config_t *, const rr_si5351_io_t *);
bool rr_si5351_set(rr_si5351_t *, unsigned output, uint32_t hz);  // zero disables
bool rr_si5351_stop(rr_si5351_t *);

/* Linux adapter: registration does no I/O. Claim initializes with outputs off. The owning worker may tune after claim; release disables before closing. */
bool rr_si5351_register(rr_hardware_t *, const char *id, const char *bus, unsigned address, const rr_si5351_config_t *);
bool rr_si5351_tune(rr_hardware_device_t *, unsigned output, uint32_t hz);
bool rr_si5351_config_init(void);
bool rr_si5351_configure(rr_hardware_t *);
#endif
