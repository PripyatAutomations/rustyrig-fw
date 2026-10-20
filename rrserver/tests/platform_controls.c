/* Hardware-free tests for the exported server control registry. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rrserver/platform.controls.h>
#include <rrserver/platform.backlight.h>
static int speaker = 25;
static int observed;
bool rrserver_backlight_control(int request, rr_backlight_state_t *state) {
   (void)request;
   (void)state;

   return false;                /* simulate a non-Pi: no bogus display control exported */
}
static bool read_speaker(void *ctx, int *value, int *lo, int *hi) {
   (void)ctx;
   *value = speaker;
   *lo = 0;
   *hi = 100;

   return true;
}
static bool write_speaker(void *ctx, int value) {
   (void)ctx;
   speaker = value;

   return true;
}
static bool visit(const rr_platform_control_state_t *s, void *ctx) {
   (void)ctx;
   assert(!strcmp(s->name, "audio.speaker.volume"));
   assert(s->current == speaker && s->maximum == 100);
   observed++;

   return false;
}
int main(void) {
   static const rr_platform_control_t output = {
      .name = "audio.speaker.volume", .label = "Speaker Volume", .unit = "%",
      .privilege = "owner|admin", .step = 5,
      .read = read_speaker, .write = write_speaker
   };
   assert(rrserver_platform_control_register(&output));
   assert(!rrserver_platform_control_register(&output));
   rr_platform_control_state_t state;
   assert(rrserver_platform_control_get(output.name, &state));
   assert(state.current == 25 && state.step == 5 && state.writable);
   assert(!rrserver_platform_control_get("../etc/passwd", &state));
   assert(!rrserver_platform_control_get("display.brightness", &state));
   assert(!rrserver_platform_control_set(output.name, 101, &state));
   assert(!rrserver_platform_control_set(output.name, -1, &state));
   assert(!rrserver_platform_control_set(output.name, 22, &state));
   assert(speaker == 25);
   assert(rrserver_platform_control_set(output.name, 40, &state));
   assert(speaker == 40 && state.current == 40);
   rrserver_platform_control_foreach(visit, NULL);
   assert(observed == 1);
   puts("platform control registry: OK");

   return 0;
}
