//
// main.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../../ext/libmongoose/mongoose.h"
#include "rrserver/amp.h"
#include "rrserver/atu.h"
#include "rrserver/au.h"
#include "rrserver/backend.h"
#include "common/cat.h"
#include "rrserver/eeprom.h"
#include "rrserver/faults.h"
#include "rrserver/filters.h"
#include "rrserver/gpio.h"
#include "rrserver/gui.h"
#include "rrserver/help.h"
#include "rrserver/i2c.h"
#include "common/io.h"
#include "common/logger.h"
#include "common/util.file.h"
#include "rrserver/network.h"
#include "common/posix.h"
#include "rrserver/ptt.h"
#include "rrserver/state.h"
#include "rrserver/thermal.h"
#include "rrserver/timer.h"
#include "rrserver/usb.h"
#include "rrserver/dds.h"
#include "rrserver/database.h"
#include "rrserver/fwdsp-mgr.h"

//
// http ui support
//
#if	defined(FEATURE_HTTP)
#include "rrserver/http.h"
#include "rrserver/ws.h"
#endif
#if	defined(FEATURE_MQTT)
#include "rrserver/mqtt.h"
#endif
#if	defined(USE_MONGOOSE)
struct mg_mgr mg_mgr;
#endif

#define	TS_ALPHA	0.1	// Weight for the moving average
int my_argc = -1;
char **my_argv = NULL;
bool dying = 0;                 // Are we shutting down?
bool restarting = 0;		// Are we restarting?
struct GlobalState rig;         // Global state
time_t now = -1;		// time() called once a second in main loop to update
int auto_block_ptt = 0;		// Auto block PTT at boot?
struct timespec last_rig_poll = { .tv_sec = 0, .tv_nsec = 0 };
struct timespec loop_start = { .tv_sec = 0, .tv_nsec = 0 };
extern char *config_file;	// from defconfig.c
extern defconfig_t defcfg[];	// From defconfig.c
time_t ptt_tot_time = RF_TALK_TIMEOUT;

extern const char *configs[];
extern const int num_configs;

char *rig_name = NULL;

// Set minimum defaults, til we have EEPROM available
static uint32_t load_defaults(void) {
   rig.faultbeep = 1;
   rig.bc_standby = 1;
   rig.tr_delay = 50;
   return 0;
}

// Zeroize our memory structures
static uint32_t initialize_state(void) {
   memset(&rig, 0, sizeof(struct GlobalState));

   for (int i = 0; i < RR_MAX_AMPS; i++) {
       memset(&rig.amps[i], 0, sizeof(struct AmpState));
   }

   for (int i = 0; i < RR_MAX_ATUS; i++) {
      memset(&rig.atus[i], 0, sizeof(struct ATUState));
   }

   for (int i = 0; i < RR_MAX_FILTERS; i++) {
      memset(&rig.filters[i], 0, sizeof(struct FilterState));
   }

   load_defaults();
   return 0;
}

void shutdown_rig(uint32_t signum) {
    if (signum >= 0) {
       Log(LOG_CRIT, "core", "Shutting down by signal %d", signum);
    } else {
       Log(LOG_CRIT, "core", "Shutting down due to internal error: %d", -signum);
    }

    dying = 1;
    rr_ptt_set_all_off();
}

void restart_rig(void) {
   Log(LOG_CRIT, "core", "Restarting process...");

   host_cleanup();

   // Ensure argv is NULL-terminated (it should be, but defensively...)
   my_argv[my_argc] = NULL;

   execv(my_argv[0], my_argv);

   // If execv fails
   perror("execv");
   exit(127);
}

int main(int argc, char **argv) {
   // save for restarting later
   my_argc = argc;
   my_argv = argv;

   // loop time calculation

#if	defined(USE_PROFILING)
   struct timespec loop_end = { .tv_sec = 0, .tv_nsec = 0 };
   double loop_runtime = 0.0, current_time;
#endif // defined(USE_PROFILING)

   // Initialize some early state
   now = time(NULL);

   int opt;
   while ((opt = getopt(argc, argv, "f:hr:")) != -1) {
      switch (opt) {
         case 'f':
            config_file = strdup(optarg);
            break;
         case 'r':
            rig_name = strdup(optarg);
            break;
         case 'h':
         default:
            fprintf(stderr, "Usage: %s [-f config file] [-r rigname]\n", argv[0]);
            fprintf(stderr, "  -f\t\t\tFile name of config\n");
            fprintf(stderr, "  -r\t\t\tRig name (for finding config file\n");
            exit(1);
      }
   }

   // load config (posix hosts)
   char *fullpath = NULL;

   cfg_init(default_cfg, defcfg);

   if (config_file) {
      if (!(cfg = cfg_load(config_file))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
      }
   } else if ((fullpath =  find_file_by_list(configs, num_configs))) {
      config_file = strdup(fullpath);
      if (!(cfg = cfg_load(fullpath))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
      }
      free(fullpath);
   } else {
     // Use default settings and save it to ~/.config/rrclient.cfg
     cfg = default_cfg;
     fprintf(stderr, "No config found :(\n");
     exit(1);
   }

   logfp = stdout;
   rig.log_level = LOG_DEBUG;		// startup in debug mode until config loaded

   srand((unsigned int)now);
   host_init();

   Log(LOG_INFO, "core", "rustyrig radio firmware v%s starting...", VERSION);
   debug_init();			// Initialize debug	
   initialize_state();			// Load default values

#if	defined(FEATURE_SQLITE)
   if ((masterdb = db_open(MASTERDB_PATH)) == NULL) {
      Log(LOG_CRIT, "core", "Cant open master db at %s", MASTERDB_PATH);
      exit(31);
   }
#endif	// defined(FEATURE_SQLITE)
#if	defined(USE_MONGOOSE)
   mg_mgr_init(&mg_mgr);
#endif
   timer_init();
   gpio_init();

#if	defined(USE_EEPROM)
   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }
#endif

//   i2c_init();
   gui_init();
   logger_init();

   // Print the serial #
   const char *s = cfg_get("device.serial");
   int serial_tmp = 0;
   if (s) {
      serial_tmp = atoi(s);
   }
#if	defined(USE_EEPROM)
   if (!s || serial_tmp == 0) {
      rig.serial = get_serial_number();
   }
#endif
   Log(LOG_INFO, "core", "Device serial number: %lu", rig.serial);

   // Initialize add-in cards
   // XXX: This should be done by enumerating the bus eventually
   filter_init_all();
   rr_amp_init_all();
   rr_atu_init_all();

#if	defined(USE_EEPROM)
   if (!s) {
      auto_block_ptt = eeprom_get_bool("features/auto_block_ptt");
   }
#endif

   // apply some configuration from the eeprom
   auto_block_ptt = cfg_get_bool("features.auto-block-ptt", false);

   if (auto_block_ptt) {
      Log(LOG_INFO, "core", "*** Enabling PTT block at startup - change features/auto-block-ptt to false to disable ***");
      rr_ptt_set_blocked(true);
   }

   if (rr_io_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error init i/o subsys ***");
      set_fault(FAULT_IO_ERROR);
      exit(1);
   }

   if (rr_backend_init()) {
      Log(LOG_CRIT, "core", "*** Failed init backend ***");
      set_fault(FAULT_BACKEND_ERR);
      exit(1);
   }

   if (rr_cat_init()) {
      Log(LOG_CRIT, "core", "*** Fatal error CAT ***");
      set_fault(FAULT_CAT_ERROR);
      exit(1);
   }

   rr_au_init();
   dds_init();
   fwdsp_init();

   // Network connectivity
//   show_network_info();
//   show_pin_info();

// Is mongoose http server enabled?
#if	defined(FEATURE_HTTP)
// Is extra mongoose debugging enabled?
#if	defined(HTTP_DEBUG_CRAZY)
   mg_log_set(MG_LL_DEBUG);
#else
   mg_log_set(MG_LL_ERROR);
#endif
   http_init(&mg_mgr);
   ws_init(&mg_mgr);
#endif
#if	defined(FEATURE_MQTT)
//   mqtt_init(&mg_mgr);
//   mqtt_client_init();
#endif
   Log(LOG_INFO, "core", "Radio initialization completed. Enjoy!");

   // Main loop
   while(1) {
      // save the current time
      clock_gettime(CLOCK_MONOTONIC, &loop_start);
      now = time(NULL);

      char buf[512];

      // Check faults
      if (check_faults()) {
         Log(LOG_CRIT, "core", "Fault detected, see crash dump above");
         // XXX: Should we stop PTT and halt here?
      }

      // Has the TOT expired?
      if (global_tot_time > 0 && global_tot_time <= now) {
         http_client_t *talker = whos_talking();
         Log(LOG_AUDIT, "ptt", "TOT (%d) expired, halting TX!", ptt_tot_time);
         rr_ptt_set_all_off();
         char msgbuf[HTTP_WS_MAX_MSG+1];
         prepare_msg(msgbuf, sizeof(msgbuf), "TOT expired, halting TX! PTT User: %s", (talker ? talker->chatname : "**UNKNOWN***"));
         send_global_alert("***SERVER***", msgbuf);
         global_tot_time = 0;
      }

      // Check thermals
      if (are_we_on_fire()) {
         rr_ptt_set_all_off();
         rr_ptt_set_blocked(true);
         Log(LOG_CRIT, "core", "Radio is on fire?! Halted TX!");
      }

      // XXX: we need to pass io structs
      /// XXX: Determine which (pipes|devices|sockets) are needing read from
      // XXX: Iterate over them: console, amp, rig
      // We limit line length to 512
#if	defined(CAT_YAESU)
      memset(buf, 0, PARSE_LINE_LEN);
      // io_read(&cat_io, &buf, PARSE_LINE_LEN - 1);
      rr_cat_parse_line(buf);
#endif
#if	defined(CAT_KPA500)
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&amp_io, &buf, PARSE_LINE_LEN - 1);
        rr_cat_parse_amp_line(buf);
#endif
//      memset(buf, 0, PARSE_LINE_LEN);
//      io_read(&cons_io, &buf, PARSE_LINE_LEN - 1);
//      rr_cons_parse_line(buf);

      // Redraw the GUI virtual framebuffer, update nextion
      gui_update();

      // Send pings, drop dead connections, etc
      http_expire_sessions();

      // XXX: Check if an LCD/OLED is configured and update it

      // XXX: Check if any mjpeg subscribers exist and prepare a frame for them

      long ms = (loop_start.tv_sec - last_rig_poll.tv_sec) * 1000L +
                (loop_start.tv_nsec - last_rig_poll.tv_nsec) / 1000000L;

      // poll the backend (internal or hamlib), if needed
      // XXX: move to config
      if (ms >= 1000) {
         // Poll the rig
         rr_be_poll(VFO_A);
         last_rig_poll.tv_sec = loop_start.tv_sec;
         last_rig_poll.tv_nsec = loop_start.tv_nsec;
         // deal with timed out en/decoders
         fwdsp_sweep_expired();
      }

#if	defined(USE_MONGOOSE)
      // Process Mongoose HTTP and MQTT events, this should be at the end of loop so all data is ready
      mg_mgr_poll(&mg_mgr, 0);
#endif

      // If enabled, calculate loop run time
#if	defined(USE_PROFILING)
      clock_gettime(CLOCK_MONOTONIC, &loop_end);
      current_time = (loop_end.tv_sec - loop_start.tv_sec) + 
                     (loop_end.tv_nsec - loop_start.tv_nsec) / 1e9;

      if (loop_runtime == 0.0) {
         loop_runtime = current_time;
      } else {
         loop_runtime = (TS_ALPHA * current_time) + (1 - TS_ALPHA) * loop_runtime;
      }
#endif // defined(USE_PROFILING)

      const struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000 };
      nanosleep(&ts, NULL);

      if (dying) {
         break;
      }
#if	defined(USE_PROFILING)
   // XXX: Every 5 minutes we should save the loop runtime average
//      Log(LOG_INFO, "loop", "Last mainloop runtime: %.6f seconds", loop_runtime);
#endif // defined(USE_PROFILING)
   }
   host_cleanup();

#if	defined(USE_MONGOOSE)
   mg_mgr_free(&mg_mgr);
#endif

   if (restarting) {
      restart_rig();
   }
   
   return 0;
}
