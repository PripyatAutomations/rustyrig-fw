// rrserver/platform.controls.c: trusted server-local exported controls.
#include <rrserver/platform.controls.h>
#include <rrserver/platform.backlight.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_PLATFORM_CONTROLS 64
static rr_platform_control_t controls[MAX_PLATFORM_CONTROLS];
static size_t controls_count;
static bool defaults_registered;

static bool valid_name(const char *name) {
   if (!name || !*name || strlen(name) > 63) return false;
   for (const unsigned char *p=(const unsigned char *)name; *p; ++p)
      if (!(isalnum(*p) || *p == '.' || *p == '_' || *p == '-')) return false;
   return true;
}
bool rrserver_platform_control_register(const rr_platform_control_t *definition) {
   if (!definition || !valid_name(definition->name) || !definition->label ||
       !definition->read || controls_count >= MAX_PLATFORM_CONTROLS ||
       (definition->step < 0) ||
       (definition->minimum > definition->maximum && definition->maximum != 0))
      return false;
   for (size_t i=0; i<controls_count; ++i)
      if (!strcmp(controls[i].name,definition->name)) return false;
   /* Definitions must have static or server-lifetime storage. */
   controls[controls_count++] = *definition;
   return true;
}
static bool backlight_read(void *context, int *value, int *minimum, int *maximum) {
   (void)context;
   rr_backlight_state_t state;
   if (!rrserver_backlight_control(-1,&state)) return false;
   *value = state.current;
   *minimum = state.minimum;
   *maximum = state.maximum;
   return true;
}
static bool backlight_write(void *context, int value) {
   (void)context;
   rr_backlight_state_t state;
   return rrserver_backlight_control(value,&state);
}
static void register_defaults(void) {
   if (defaults_registered) return;
   defaults_registered = true;
   static const rr_platform_control_t backlight = {
      .name="display.brightness", .label="LCD Brightness", .unit="level",
      .privilege="platform.display.write", .step=1,
      .read=backlight_read, .write=backlight_write
   };
   rrserver_platform_control_register(&backlight);
}
static const rr_platform_control_t *find(const char *name) {
   if (!valid_name(name)) return NULL;
   register_defaults();
   for (size_t i=0; i<controls_count; ++i)
      if (!strcmp(name,controls[i].name)) return &controls[i];
   return NULL;
}
static bool snapshot(const rr_platform_control_t *control, rr_platform_control_state_t *state) {
   if (!control || !state) return false;
   int current=0, minimum=0, maximum=0;
   if (!control->read(control->context,&current,&minimum,&maximum) ||
       maximum < minimum || current < 0) return false;
   memset(state,0,sizeof(*state));
   snprintf(state->name,sizeof(state->name),"%s",control->name);
   snprintf(state->label,sizeof(state->label),"%s",control->label);
   snprintf(state->unit,sizeof(state->unit),"%s",control->unit ? control->unit : "");
   snprintf(state->privilege,sizeof(state->privilege),"%s",
      control->privilege ? control->privilege : "owner|admin");
   state->minimum=minimum; state->maximum=maximum; state->current=current;
   state->step=control->step > 0 ? control->step : 1;
   state->writable = control->write != NULL;
   return true;
}
bool rrserver_platform_control_get(const char *name, rr_platform_control_state_t *state) {
   return snapshot(find(name),state);
}
bool rrserver_platform_control_set(const char *name, int value, rr_platform_control_state_t *state) {
   const rr_platform_control_t *control = find(name);
   rr_platform_control_state_t current;
   if (!control || !control->write || !snapshot(control,&current) ||
       value < current.minimum || value > current.maximum ||
       (value-current.minimum) % current.step != 0) return false;
   if (!control->write(control->context,value)) return false;
   return snapshot(control,state);
}
void rrserver_platform_control_foreach(rr_platform_control_visit_fn visit, void *user) {
   if (!visit) return;
   register_defaults();
   for (size_t i=0;i<controls_count;++i) {
      rr_platform_control_state_t state;
      if (snapshot(&controls[i],&state) && visit(&state,user)) break;
   }
}
