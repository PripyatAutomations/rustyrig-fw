//
// main.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librustyaxe/cat.h>
#include <rrserver/faults.h>
#include <rrserver/help.h>
#include <rrserver/ptt.h>
#include <rrserver/thermal.h>
#include <rrserver/timer.h>
#include <rrserver/database.h>
#include <rrserver/backend.h>
#include <rrserver/gpio.h>
#include <rrserver/network.h>
#include <rrserver/amp.h>
#include <rrserver/atu.h>
#include <rrserver/filters.h>
#include <rrserver/protection.h>

#ifdef	USE_MONGOOSE
struct mg_mgr mg_mgr;
#endif

#ifdef	USE_MQTT
#include <rrserver/mqtt.h>
#endif

bool dying = 0;                  // Are we shutting down?
bool restarting = 0;             // Are we restarting?
struct GlobalState rig;          // Global state
time_t now = -1;                 // time() at 1hz timer
time_t started = -1;             // time() when started
int auto_block_ptt = 0;          // Auto block PTT at boot?
struct timespec loop_start = {
   .tv_sec = 0, .tv_nsec = 0
};
char *rig_name = NULL;

// How often we will poll the backend in ms (default:1000)
int cfg_backend_poll_interval = 1000;

// These are used for restarting ourself using exec()
int my_argc = -1;
char **my_argv = NULL;

// Things that probably should be in headers... ;)
extern char *config_file;        // from defconfig.c
extern defconfig_t defcfg[];     // From defconfig.c
extern const char *configs[];
extern const int num_configs;
extern void timer_clock_tick_fn(void *arg);     // timer.clocktick.c

// Set minimum defaults, til we have EEPROM available
static uint32_t load_defaults(void) {
   rig.faultbeep = 1;
   rig.bc_standby = 1;
   rig.tr_delay = 50;

   return 0;
}

void shutdown_rig(uint32_t signum) {
   Log(LOG_CRIT, "core", "Shutting down by signal %d", signum);
   dying = 1;
   rr_ptt_set_all_off();
}

void restart_rig(void) {
   Log(LOG_CRIT, "core", "Restarting process...");
   host_cleanup();

   // Ensure argv is NULL-terminated (it should be, but defensively set it so...)
   my_argv[my_argc] = NULL;
   execv(my_argv[0], my_argv);

   // If execv fails
   Log(LOG_CRIT, "core", "restart_rig failed in execve(): %d: %s", errno, strerror(errno));
   exit(EXIT_FAILURE);
}

static void timer_check_faults_fn(void *arg) {
   if ( check_faults() ) {
      Log(LOG_CRIT, "core", "Fault detected, see log above. Rig haltered!");
      // XXX: Stop PTT and halt here?
   }
}

static struct timespec last_backend_poll = {
   .tv_sec = 0, .tv_nsec = 0
};

static void timer_backend_poll_fn(void *arg) {
   // Poll the rig
   rr_be_poll(VFO_A);
//   rr_be_poll(VFO_B);

   last_backend_poll.tv_sec = loop_start.tv_sec;
   last_backend_poll.tv_nsec = loop_start.tv_nsec;
}

int main(int argc, char **argv) {
   // save for restarting later
   my_argc = argc;
   my_argv = argv;

   // Initialize some early state
   now = started = time(NULL);

   int opt;
   while ( ( opt = getopt(argc, argv, "f:hr:") ) != -1 ) {
      switch (opt) {
         case 'f': {
            config_file = strdup(optarg);
            break;
         }
         case 'r': {
            rig_name = strdup(optarg);
            break;
         }
         case 'h':
         default: {
            fprintf(stderr, "Usage: %s [-f config file] [-r rigname]\n", argv[0]);
            fprintf(stderr, "  -f\t\t\tFile name of config\n");
            fprintf(stderr, "  -r\t\t\tRig name (for finding config file\n");
            exit(EXIT_FAILURE);
         }
      }
   }
   // load config (posix hosts)
   char *fullpath = NULL;

   if (config_file) {
      if ( !( cfg = cfg_load(config_file) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", config_file);
      }
   } else if ( ( fullpath = find_file_by_list(configs, num_configs) ) ) {
      config_file = strdup(fullpath);

      if ( !( cfg = cfg_load(fullpath) ) ) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
      }
      free(fullpath);
   } else {
      cfg = default_cfg;
      fprintf(stderr, "No config found :(\n");
      exit(EXIT_FAILURE);
   }
   logfp = stdout;
   rig.log_level = LOG_DEBUG;            // startup in debug mode until config
                                         // loaded
   logger_init(LOG_FILE);

   srand( (unsigned int)now );
   host_init();

   Log(LOG_INFO, "core", "rustyrig radio firmware v%s starting...", VERSION);
   memset( &rig, 0, sizeof(struct GlobalState) );
   load_defaults();

// Core dump setup
#ifdef USE_COREDUMPS_SERVER
   struct rlimit rl = {
      .rlim_cur = RLIM_INFINITY,
      .rlim_max = RLIM_INFINITY
   };
   setrlimit(RLIMIT_CORE, &rl);
#else
   struct rlimit rl = {
      0, 0
   };
   setrlimit(RLIMIT_CORE, &rl);
#endif // USE_COREDUMPS_SERVER

#ifdef	USE_SQLITE
   if ( !( masterdb = db_open(MASTERDB_PATH) ) ) {
      Log(LOG_CRIT, "core", "Cant open master db at %s", MASTERDB_PATH);
      exit(EXIT_FAILURE);
   }
#endif // USE_SQLITE

   protection_init();
   timer_init();
#ifdef	USE_GPIO
   gpio_init();
#endif	// USE_GPIO

#ifdef	USE_EEPROM
   // if able to connect to EEPROM, load and apply settings
   if (eeprom_init() == 0) {
      eeprom_load_config();
   }
#endif	// USE_EEPROM

//   i2c_init();
//   gui_init();

   // Print the serial #
   const char *s = cfg_get("device.serial");
   int serial_tmp = 0;

   if (s) {
      serial_tmp = atoi(s);
   }

#ifdef	USE_EEPROM
   if (!s || serial_tmp == 0) {
      rig.serial = get_serial_number();
   }
#endif
   Log(LOG_INFO, "core", "Device serial number: %lu", rig.serial);

   // apply some configuration from the eeprom
#ifdef	USE_EEPROM
   auto_block_ptt = eeprom_get_bool("features/auto-block-ptt");
   cfg_backend_poll_interval = cfg_get_int("rig.poll-interval", 1000);
#endif

   // Initialize add-in cards
   // XXX: This should be done by enumerating the bus eventually
   filter_init_all();
   rr_amp_init_all();
   rr_atu_init_all();

   if (auto_block_ptt) {
      Log(LOG_INFO, "core",
         "*** Enabling PTT block at startup - change features/auto-block-ptt to false to disable ***");
      rr_ptt_set_blocked(true);
   }

   if ( rr_io_init() ) {
      Log(LOG_CRIT, "core", "*** Fatal error init i/o subsys ***");
      set_fault(FAULT_IO_ERROR);
      exit(EXIT_FAILURE);
   }

   if ( rr_backend_init() ) {
      Log(LOG_CRIT, "core", "*** Failed init backend ***");
      set_fault(FAULT_BACKEND_ERR);
      exit(EXIT_FAILURE);
   }

#ifdef	USE_CAT
   if ( rr_cat_init() ) {
      Log(LOG_CRIT, "core", "*** Fatal error CAT ***");
      set_fault(FAULT_CAT_ERROR);
      exit(EXIT_FAILURE);
   }
#endif

//   rr_au_init();
//   dds_init();
//   fwdsp_init();

   // Network connectivity
   show_network_info();
   show_pin_info();

// Bring up libmongoose for the websocket/mqtt servers & mqtt client
#if     defined(USE_MONGOOSE)
#if     defined(USE_HTTP)
#if     defined(HTTP_DEBUG_CRAZY)
   mg_log_set(MG_LL_DEBUG);
#else // HTTP_DEBUG_CRAZY
   mg_log_set(MG_LL_ERROR);
#endif // HTTP_DEBUG_CRAZY

   mg_mgr_init(&mg_mgr);
   http_init(&mg_mgr);
#endif // USE_HTTP
#if     defined(USE_MQTT)
   mqtt_server_init(&mg_mgr);
   mqtt_client_init();
#endif

   // Update the clock (now) once a second
   mg_timer_add(&mg_mgr, 1000, MG_TIMER_REPEAT, timer_clock_tick_fn, &mg_mgr);

   // Update the clock (now) once a second
   // Check for faults/protection every 150ms
   mg_timer_add(&mg_mgr, 150, MG_TIMER_REPEAT, timer_check_faults_fn, &mg_mgr);

   // rig polling
   mg_timer_add(&mg_mgr, cfg_backend_poll_interval, MG_TIMER_REPEAT, timer_backend_poll_fn, &mg_mgr);
#endif // USE_MONGOOSE

   Log(LOG_INFO, "core", "Radio initialization completed. Enjoy!");

   // Main loop
   while (1) {
#ifdef	USE_MONGOOSE
      // Process Mongoose HTTP and MQTT events, this should be at the end of
      // loop so all data is ready
      mg_mgr_poll(&mg_mgr, 1000);
#endif

      if (dying) {
         break;
      }
   }
   host_cleanup();

#ifdef	USE_MONGOOSE
   mg_mgr_free(&mg_mgr);
#endif

   if (restarting) {
      restart_rig();
   }

   return 0;
}
