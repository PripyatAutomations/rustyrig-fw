// rrserver/rig.config.h: scoped configuration for named rig instances
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if !defined(__rrserver_rig_config_h)
#define	__rrserver_rig_config_h

#include <stdbool.h>
#include <stdint.h>

extern bool rr_rig_config_init(void);
extern bool rr_rig_config_section_cb(const char *path, int line, const char *section, const char *buf);
extern bool rr_rig_config_alias_valid(const char *alias);
extern const char *rr_rig_config_get(const char *alias, const char *key);
/* Returns an allocated expanded value, following cfg_get_exp(). */
extern char *rr_rig_config_get_exp(const char *alias, const char *key);
extern int rr_rig_config_get_int(const char *alias, const char *key, int default_value);
extern bool rr_rig_config_get_bool(const char *alias, const char *key, bool default_value);
/* Compatibility mask for the configured default rig's traditional A-Z VFOs. */
extern uint32_t rr_rig_config_default_vfo_mask(void);

#endif // !defined(__rrserver_rig_config_h)
