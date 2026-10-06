// rrserver/rig.properties.h: backend-neutral per-rig property state
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if !defined(__rrserver_rig_properties_h)
#define	__rrserver_rig_properties_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <librustyaxe/dict.h>

#define	RR_PROPERTY_CHANGED_EVENT "rig.property.changed"
#define	RR_PROPERTY_NAME_MAX 64

/* Canonical rig-level property names. */
#define	RR_PROP_RX_VFO "rx.vfo"
#define	RR_PROP_TX_VFO "tx.vfo"
#define	RR_PROP_PTT "ptt"
#define	RR_PROP_SIGNAL "signal"

/* Canonical suffixes used with rr_property_vfo_name(). */
#define	RR_PROP_VFO_FREQUENCY "frequency"
#define	RR_PROP_VFO_MODE "mode"
#define	RR_PROP_VFO_WIDTH "width"

struct rr_backend;
typedef struct rr_server_rig rr_server_rig_t;
struct rr_server_vfo;
typedef struct rr_server_vfo rr_server_vfo_t;

typedef struct rr_property_descriptor {
   const char *name;
   val_type_t type;
   bool readable;
   bool writable;
   const char *unit;
   // Optional schema constraints, in the property's native value type.
   bool has_min, has_max, has_step;
   dict_value_t minimum, maximum, step;
   const char *enum_values; // Space-separated string choices; copied on define.
} rr_property_descriptor_t;

typedef bool (*rr_property_iter_fn)(const rr_property_descriptor_t *, void *);
bool rr_rig_property_foreach(const rr_server_rig_t *, rr_property_iter_fn, void *);
bool rr_vfo_property_foreach(const rr_server_vfo_t *, rr_property_iter_fn, void *);

/*
 * Values returned in a snapshot are borrowed from the rig and remain valid only until
 * that property is changed or the rig is freed. String observations are copied into
 * rig-owned storage, so they do not borrow the caller's input.
 */
typedef struct rr_property_snapshot {
   val_type_t value_type;
   dict_value_t value;
   bool observed;
   bool known;
   bool available;
   uint64_t version;
   time_t changed_at;
} rr_property_snapshot_t;

typedef enum rr_property_update {
   RR_PROPERTY_ERROR = -1,
   RR_PROPERTY_UNCHANGED = 0,
   RR_PROPERTY_CHANGED = 1,
} rr_property_update_t;

typedef enum rr_control_result {
   RR_CONTROL_OK = 0,
   RR_CONTROL_INVALID,
   RR_CONTROL_NOT_FOUND,
   RR_CONTROL_READ_ONLY,
   RR_CONTROL_TYPE_MISMATCH,
   RR_CONTROL_UNSUPPORTED,
   RR_CONTROL_BACKEND_FAILED,
} rr_control_result_t;

typedef struct rr_control_request {
   rr_server_rig_t *rig;
   /* Canonical VFO target. When present, property is local to this VFO. Legacy callers
    * may leave this NULL and use vfo.A.frequency paths. */
   rr_server_vfo_t *vfo;
   const char *property;
   val_type_t value_type;
   dict_value_t value;
   const char *source;
   void *context;
} rr_control_request_t;

typedef rr_control_result_t (*rr_rig_control_handler_t)(const rr_control_request_t *request, void *user);

extern rr_server_rig_t *rr_server_rig_new(const char *id, const char *name);
extern void rr_server_rig_free(rr_server_rig_t *rig);
extern const char *rr_server_rig_id(const rr_server_rig_t *rig);
extern const char *rr_server_rig_name(const rr_server_rig_t *rig);
extern void rr_server_rig_set_backend(rr_server_rig_t *rig, struct rr_backend *backend);
extern struct rr_backend *rr_server_rig_backend(const rr_server_rig_t *rig);

/* Returns true on error, matching existing rrserver setup conventions. */
extern bool rr_rig_property_define(rr_server_rig_t *rig, const rr_property_descriptor_t *descriptor);
extern bool rr_rig_define_vfo_properties(rr_server_rig_t *rig, char vfo_id);
extern bool rr_rig_property_describe(const rr_server_rig_t *rig, const char *name,
                                     rr_property_descriptor_t *descriptor);

extern rr_property_update_t rr_rig_property_observe(rr_server_rig_t *rig, const char *name, val_type_t type,
                                                    const dict_value_t *value);
extern rr_property_update_t rr_rig_property_unavailable(rr_server_rig_t *rig, const char *name);
extern bool rr_rig_property_read(const rr_server_rig_t *rig, const char *name, rr_property_snapshot_t *snapshot);

extern bool rr_vfo_property_define(rr_server_vfo_t *vfo, const rr_property_descriptor_t *descriptor);
extern bool rr_vfo_property_describe(const rr_server_vfo_t *vfo, const char *name,
                                     rr_property_descriptor_t *descriptor);
extern rr_property_update_t rr_vfo_property_observe(rr_server_vfo_t *vfo, const char *name, val_type_t type,
                                                    const dict_value_t *value);
extern rr_property_update_t rr_vfo_property_unavailable(rr_server_vfo_t *vfo, const char *name);
extern bool rr_vfo_property_read(const rr_server_vfo_t *vfo, const char *name, rr_property_snapshot_t *snapshot);

extern bool rr_property_vfo_name(char *buf, size_t len, char vfo_id, const char *field);
extern bool rr_property_parse_vfo(const char *name, char *vfo_id, const char **field);

extern void rr_server_rig_set_control_handler(rr_server_rig_t *rig, rr_rig_control_handler_t handler, void *user);
extern rr_control_result_t rr_rig_control(const rr_control_request_t *request);

#endif // !defined(__rrserver_rig_properties_h)
