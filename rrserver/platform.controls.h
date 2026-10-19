#ifndef RRSERVER_PLATFORM_CONTROLS_H
#define RRSERVER_PLATFORM_CONTROLS_H
#include <stdbool.h>
#include <stddef.h>

/* Exported, typed node controls. Names are stable protocol addresses.
 * Values are integers for now; descriptor types allow future extensions
 * without attaching server-wide settings to an imaginary rig. */
typedef struct rr_platform_control {
   const char *name;
   const char *label;
   const char *unit;
   const char *privilege;
   int minimum, maximum, step;
   bool (*read)(void *context, int *value, int *minimum, int *maximum);
   bool (*write)(void *context, int value);
   void *context;
} rr_platform_control_t;

typedef struct rr_platform_control_state {
   char name[64], label[128], unit[32], privilege[64];
   int current, minimum, maximum, step;
   bool writable;
} rr_platform_control_state_t;
typedef bool (*rr_platform_control_visit_fn)(const rr_platform_control_state_t *, void *);

/* Register only trusted, local implementations at startup. Never paths or
 * callbacks received from a remote request. Names cannot be replaced. */
bool rrserver_platform_control_register(const rr_platform_control_t *definition);
bool rrserver_platform_control_get(const char *name, rr_platform_control_state_t *state);
bool rrserver_platform_control_set(const char *name, int value, rr_platform_control_state_t *state);
void rrserver_platform_control_foreach(rr_platform_control_visit_fn visit, void *user);
#endif
