//
// rrserver/backend.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if !defined(__rrserver_backend_h)
#define __rrserver_backend_h

#include <stdbool.h>
#include <stdint.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/rig.vfo.h>

struct rr_server_rig;
typedef struct rr_backend rr_backend_t;
typedef struct rr_backend_type rr_backend_type_t;

/*
 * Every callback receives the allocated backend instance. Immutable callback tables describe a backend TYPE; all radio-specific mutable data belongs in
 * rr_backend_t::data and is allocated by create().
 */
typedef struct rr_backend_funcs {
   bool (*create)(rr_backend_t *backend);
   void (*destroy)(rr_backend_t *backend);
   rr_vfo_data_t *(*poll_state)(rr_backend_t *backend, rr_server_vfo_t *vfo);
   bool (*vfo_supported)(rr_backend_t *backend, rr_server_vfo_t *vfo);

   bool (*ptt_set)(rr_backend_t *backend, rr_server_vfo_t *vfo, bool state);
   bool (*ptt_get)(rr_backend_t *backend, rr_server_vfo_t *vfo);
   bool (*split_mode)(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *args);
   bool (*tuner_control)(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *args);
   bool (*power_set)(rr_backend_t *backend, rr_server_vfo_t *vfo, float power);
   float (*power_get)(rr_backend_t *backend, rr_server_vfo_t *vfo);
   rr_mode_t (*mode_get)(rr_backend_t *backend, rr_server_vfo_t *vfo);
   bool (*mode_set)(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_mode_t mode);
   const char *(*mode_get_str)(rr_backend_t *backend, rr_server_vfo_t *vfo);
   bool (*freq_set)(rr_backend_t *backend, rr_server_vfo_t *vfo, int freq);
   float (*freq_get)(rr_backend_t *backend, rr_server_vfo_t *vfo);
   uint16_t (*width_get)(rr_backend_t *backend, rr_server_vfo_t *vfo);
   bool (*width_set)(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *width);
   int (*widths_get)(rr_backend_t *backend, rr_server_vfo_t *vfo, int *widths, int max);
} rr_backend_funcs_t;

/* Immutable implementation metadata. */
struct rr_backend_type {
   const char *name;
   const char *description;
   bool uses_property_state;
   const rr_backend_funcs_t *api;
};

/* One allocated backend attached to one runtime rig. */
struct rr_backend {
   const rr_backend_type_t *type;
   struct rr_server_rig *owner;
   char *config_alias;
   void *data;
   rr_vfo_t active_vfo;
};

extern rr_backend_t *rr_backend_instance_new(const rr_backend_type_t *type, struct rr_server_rig *owner, const char *config_alias);
extern void rr_backend_instance_free(rr_backend_t *backend);
extern bool rr_backend_type_register(const rr_backend_type_t *type);
extern const rr_backend_type_t *rr_backend_type_find(const char *name);
extern const char *rr_backend_instance_alias(const rr_backend_t *backend);
extern void *rr_backend_instance_data(const rr_backend_t *backend);
extern void rr_backend_instance_set_data(rr_backend_t *backend, void *data);
extern const char *rr_backend_config_get(const rr_backend_t *backend, const char *key);
extern char *rr_backend_config_get_exp(const rr_backend_t *backend, const char *key);
extern int rr_backend_config_get_int(const rr_backend_t *backend, const char *key, int default_value);
extern bool rr_backend_config_get_bool(const rr_backend_t *backend, const char *key, bool default_value);
extern void rr_backend_register_builtin_types(void);

extern bool rr_backend_init(void);
extern bool rr_backend_fini(void);
extern bool rr_backend_poll_all(void);
extern bool rr_backend_poll_rig(struct rr_server_rig *radio, rr_server_vfo_t *vfo);
extern bool rr_backend_power_set_rig(struct rr_server_rig *radio, rr_server_vfo_t *vfo, float power);
extern bool rr_backend_vfo_supported(struct rr_server_rig *radio, rr_server_vfo_t *vfo);

/* Default single-rig entry points. They always target the explicit default rig. */
extern bool rr_be_get_ptt(rrconn_t *cptr, rr_vfo_t vfo);
extern bool rr_ptt_apply(rr_vfo_t vfo, bool state);
extern bool rr_get_ptt(rrconn_t *cptr, rr_vfo_t vfo);
extern bool rr_set_ptt(rrconn_t *cptr, rr_vfo_t vfo, bool state);
extern float rr_get_power(rr_vfo_t vfo);
extern bool rr_set_power(rr_vfo_t vfo, float power);
extern float rr_freq_get(rr_vfo_t vfo);
extern bool rr_freq_set(rr_vfo_t vfo, int freq);
extern bool rr_be_poll(rr_vfo_t vfo);
extern bool rr_be_vfo_supported(rr_vfo_t vfo);
extern uint16_t rr_get_width(rr_vfo_t vfo);
extern bool rr_set_width(rr_vfo_t vfo, const char *width);
extern int rr_widths_get(rr_vfo_t vfo, int *widths, int max);
extern bool rr_set_mode(rr_vfo_t vfo, rr_mode_t mode);
extern rr_mode_t rr_get_mode(rr_vfo_t vfo);
extern const char *rr_get_mode_str(rr_vfo_t vfo);
extern bool rr_cat_state_send(rrconn_t *cptr);

#include <rrserver/backend.hamlib.h>
#include <rrserver/backend.internal.h>

#endif // !defined(__rrserver_backend_h)
