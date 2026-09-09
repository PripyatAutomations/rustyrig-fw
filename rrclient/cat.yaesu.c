//
// cat.yaesu.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Here we implement the Yaesu 891/991a style CAT protocol for control of the rig ft891
 * has complete CAT enough for all uses, so it's our milestone goal
 *
 * We have two entry points here
 * - rr_cat_parse_line(): Parses a line from io (sock|net|pipe)
 * - rr_cat_parse_ws(): Parses a websocket message containing a CAT command
 *
 * We respond via rr_cat_reply() with enum rr_cat_req_type as first arg
 */
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cat.h>
#include <rrclient/vfo.h>

#if     defined(CAT_YAESU)
// The rig-control commands are applied by the SERVER's selected backend
// (rrserver/backend.internal or backend.hamlib); we send cat.cmd messages
// over the websocket and the server's rigctl handler does the work.
// NB: ws_conn is the client's active connection (rrclient/connman.c)
extern rrconn_t *ws_conn;
extern bool ws_send_freq_cmd(rrconn_t *cptr, const char *vfo, long freq);
extern bool ws_send_mode_cmd(rrconn_t *cptr, const char *vfo, const char *mode);
extern bool ws_send_width_cmd(rrconn_t *cptr, const char *vfo, const char *width);
extern bool ws_send_ptt_cmd(rrconn_t *cptr, const char *vfo, bool ptt);

// Active VFO letter for sends, e.g. "A"
static const char *cat_vfo_str(char vfo) {
   static char buf[2];

   buf[0] = vfo ? vfo : 'A';
   buf[1] = '\0';
   return buf;
}

// Map the server-echoed mode string (cat.state.mode) to a Yaesu mode digit.
// Yaesu digits: 1=LSB 2=USB 3=CW 4=FM 6=RTTY-L 7=RTTY-U 8=PKT-L 9=PKT-U
static int cat_mode_digit(void) {
   const char *mode = vfo_state_get("A", "cat.state.mode", NULL);

   if (!mode) {
      return 2;                    // USB is a sane default for HF digital
   }
   if (strcmp(mode, "LSB") == 0) {
      return 1;
   }
   if (strcmp(mode, "USB") == 0) {
      return 2;
   }
   if (strcmp(mode, "CW") == 0) {
      return 3;
   }
   if (strcmp(mode, "FM") == 0) {
      return 4;
   }
   if (strcmp(mode, "D-L") == 0) {
      return 8;
   }
   if (strcmp(mode, "D-U") == 0) {
      return 9;
   }
   return 2;
}

// Apply a Yaesu mode digit: tell the server via ws, which echoes the new
// mode back in cat.state for later queries.
static void cat_mode_set(const char *digit) {
   const char *mode = NULL;

   switch (digit[0]) {
      case '0':
      case '1': mode = "LSB"; break;
      case '2': mode = "USB"; break;
      case '3': mode = "CW"; break;
      case '4': mode = "FM"; break;
      case '6': mode = "D-L"; break;
      case '7': mode = "D-U"; break;
      case '8': mode = "D-L"; break;
      case '9': mode = "D-U"; break;
      default:
         Log(LOG_WARN, "cat.yaesu", "Unknown MODE digit %c", digit[0]);
         return;
   }

   Log(LOG_INFO, "cat.yaesu", "Set MODE to %s", mode);
   ws_send_mode_cmd(ws_conn, cat_vfo_str(0), mode);
}

// Function stubs for all FT-891 CAT commands
void rr_cat_yaesu_vfo_a_to_b(const char *args) {
   Log(LOG_INFO, "cat.yaesu", "VFO A -> VFO B (NYI)");
}               // VFO A to VFO B

void rr_cat_yaesu_tuner_control(const char *args) {
}       // Antenna Tuner Control

void rr_cat_yaesu_af_gain(const char *args) {
}               // Read/Set AF gain

// Auto Information mode: hamlib queries "AI;" during connect and expects a
// reply; we always report it off (we don't push unsolicited CAT frames).
void rr_cat_yaesu_auto_info(const char *args) {
   if (!args || !args[0]) {
      rr_cat_printf("AI0;");
      Log(LOG_DEBUG, "cat.yaesu", "AI query -> 0");
      return;
   }
   Log(LOG_DEBUG, "cat.yaesu", "Set AI to %c", args[0]);
}               // Enable/Disable Auto Information mode

void rr_cat_yaesu_vfo_a_to_mem(const char *args) {
}       // Store VFO A to memory channel

void rr_cat_yaesu_vfo_b_to_a(const char *args) {
}               // VFO B to VFO A

void rr_cat_yaesu_ant_select(const char *args) {
}               // Read/Set Antenna selection

void rr_cat_yaesu_auto_notch(const char *args) {
}               // Read/Set Auto Notch

void rr_cat_yaesu_band_down(const char *args) {
}               // Band Down

void rr_cat_yaesu_band_select(const char *args) {
}       // Band Select

void rr_cat_yaesu_band_up(const char *args) {
}               // band up

void rr_cat_yaesu_break_in(const char *args) {
}               // Break-in

void rr_cat_yaesu_manual_notch(const char *args) {
}       // Manual Notch

void rr_cat_yaesu_busy(const char *args) {
}               // Busy

void rr_cat_yaesu_clarifier(const char *args) {
}               // Clarifier

void rr_cat_yaesu_channel(const char *args) {
}               // Channel

void rr_cat_yaesu_ctcss_number(const char *arg) {
}       // CTCSS Number

void rr_cat_yaesu_contour(const char *arg) {
}               // Contour

void rr_cat_yaesu_cw_spot(const char *arg) {
}               // CW Spot

void rr_cat_yaesu_ctcss(const char *arg) {
}               // CTCSS

void rr_cat_yaesu_dimmer(const char *arg) {
}               // Dimmer

void rr_cat_yaesu_down(const char *args) {
}               // Down

void rr_cat_yaesu_encoder_down(const char *args) {
}       // Encoder Down

void rr_cat_yaesu_enter(const char *args) {
}               // Enter key

void rr_cat_yaesu_encoder_up(const char *args) {
}               // Encoder Up

void rr_cat_yaesu_menu(const char *args) {
}               // Extended menu commands

void rr_cat_yaesu_set_vfo_a(const char *args) {
   // Read ("FA;") replies with the current frequency; set applies via backend
   if (!args || !args[0]) {
      long freq = vfo_state_get_long("A", "cat.state.freq", 0);
      // FT-891/FT-991 (newcat) use 9-digit frequency fields; the 11-digit
      // form is the FTDX101/5000 family. Wrong width shifts the parsed value.
      rr_cat_printf("FA%09ld;", freq);
      Log(LOG_DEBUG, "cat.yaesu", "VFO A read -> %ld", freq);
      return;
   }

   long freq = atol(args);

   Log(LOG_INFO, "cat.yaesu", "Set VFO A freq to %ld", freq);
   ws_send_freq_cmd(ws_conn, cat_vfo_str('A'), freq);
}               // Read/Set VFO A frequency

void rr_cat_yaesu_set_vfo_b(const char *args) {
   if (!args || !args[0]) {
      long freq = vfo_state_get_long("B", "cat.state.freq", 0);
      rr_cat_printf("FB%09ld;", freq);
      Log(LOG_DEBUG, "cat.yaesu", "VFO B read -> %ld", freq);
      return;
   }

   long freq = atol(args);

   Log(LOG_INFO, "cat.yaesu", "Set VFO B freq to %ld", freq);
   ws_send_freq_cmd(ws_conn, cat_vfo_str('B'), freq);
}               // Read/Set VFO B frequency

void rr_cat_yaesu_fast_step(const char *args) {
}               // Fast Step

void rr_cat_yaesu_agc_func(const char *args) {
}               // AGC Function

void rr_cat_yaesu_id(const char *args) {
   // FT-891 replies with its model code; hamlib uses this to identify the rig
   Log(LOG_DEBUG, "cat.yaesu", "ID query -> 0911");
   rr_cat_printf("ID0911;");
}                       // IDentification

void rr_cat_yaesu_info(const char *args) {
}               // Get receiver status

void rr_cat_yaesu_if_shift(const char *args) {
}               // IF shift

void rr_cat_yaesu_cw_key(const char *args) {
}               // Send CW Keying

void rr_cat_yaesu_lock(const char *args) {
}               // Read/Set lock status

void rr_cat_yaesu_mem_to_vfo_a(const char *args) {
}       // Memory to VFO A

void rr_cat_yaesu_mem_channel(const char *args) {
}       // Read/Set Memory Channel data

void rr_cat_yaesu_mic_gain(const char *args) {
}               // Mic Gain

void rr_cat_yaesu_mode(const char *args) {
   // Yaesu newcat (FT-991A-family, which hamlib drives FT-891 as) sends
   // "MD0;" as the QUERY for main-band mode and "MD0<n>;" to set it; the
   // leading 0 is the band index, not a mode digit.  A reply of "MD0<n>;"
   // is expected.  Legacy single-digit forms ("MD;" -> "MD<n>;", "MD<n>;")
   // are also accepted.
   if (!args || !args[0]) {
      // "MD;" legacy query
      int digit = cat_mode_digit();
      rr_cat_printf("MD%d;", digit);
      Log(LOG_DEBUG, "cat.yaesu", "MODE read -> %d", digit);
      return;
   }

   // Band-qualified form: MD0 [; | <mode digit>]. NB: the digit is the
   // VFO/band selector (0=A, 1=B) and hamlib expects it ECHOED in the reply
   // ("MD1;" -> "MD1<n>;"); replying with the wrong selector makes
   // newcat_get_mode fail with -RIG_EPROTO ("Protocol error").
   if (args[0] == '0' || args[0] == '1') {   // band index (we only have main)
      if (!args[1]) {
         // "MD0;" -- query: reply MD<n-selector><mode>;
         int digit = cat_mode_digit();
         rr_cat_printf("MD%c%d;", args[0], digit);
         Log(LOG_DEBUG, "cat.yaesu", "MODE read (band %c) -> %d", args[0], digit);
         return;
      }

      // "MD0<n>;" -- set mode <n> on the main band
      char digit[2] = { args[1], 0 };
      cat_mode_set(digit);
      return;
   }

   // Legacy set form: "MD<n>;"
   char digit[2] = { args[0], 0 };
   cat_mode_set(digit);
}               // Read/Set operating mode

void rr_cat_yaesu_monitor_level(const char *args) {
}       // Monitor Level

void rr_cat_yaesu_memory_read(const char *args) {
}       // Memory Read

void rr_cat_yaesu_meter_switch(const char *args) {
}       // Meter Switch

void rr_cat_yaesu_mem_write_tag(const char *args) {
}       // Memory write & tag

void rr_cat_yaesu_mem_write(const char *args) {
}               // Memory write

void rr_cat_yaesu_mox(const char *args) {
}               // MOX

void rr_cat_yaesu_narrow(const char *args) {
   // FT-891: NA0<x> where x is 0 (off) or 1 (narrow IF filter).
   // MUST answer queries - a silent handler hangs the client (e.g. WSJT-X
   // waits for the response and eventually drops the connection).
   if (!args || !args[0]) {
      // Query: report narrow status. We don't track a narrow flag yet, so
      // report off (0). Compare width against the mode's normal passband if
      // we ever want this to reflect the actual filter.
      // NB: Yaesu newcat queries are band-qualified ("NA0;"/"NA1;") and
      // clients expect the selector echoed in the reply.
      rr_cat_printf("NA00;");
      Log(LOG_DEBUG, "cat.yaesu", "NARROW query -> 0");
      return;
   }

   if ( (args[0] == '0' || args[0] == '1') && !args[1] ) {
      // band-qualified query ("NA0;" or "NA1;") - echo the selector back
      rr_cat_printf("NA%c0;", args[0]);
      Log(LOG_DEBUG, "cat.yaesu", "NARROW query (band %c) -> 0", args[0]);
      return;
   }

   // Set: args[0] is 0/1. Map onto the width command (narrow/normal) so the
   // server applies a real filter change.
   Log(LOG_INFO, "cat.yaesu", "Set NARROW to %c", args[0]);
   ws_send_width_cmd(ws_conn, cat_vfo_str(0), (args[0] == '1' ? "narrow" : "normal") );
}               // Narrow

void rr_cat_yaesu_noise_blanker(const char *args) {
}       // Read/Set Noise Blanker

void rr_cat_yaesu_nb_level(const char *args) {
}               // NB Level

void rr_cat_yaesu_noise_reduction(const char *args) {
}       // Noise Reduction

void rr_cat_yaesu_obi(const char *args) {
}               // Opposite Band Info??

void rr_cat_yaesu_offset(const char *args) {
}               // Offset

void rr_cat_yaesu_preamp(const char *args) {
}               // Preamp

void rr_cat_yaesu_playback(const char *args) {
}               // Playback

void rr_cat_yaesu_preproc(const char *args) {
}               // Preprocessor

void rr_cat_yaesu_preproc_level(const char *args) {
}       // Preprocessor level

void rr_cat_yaesu_tx_power(const char *args) {
   // FT-891: PC0xxx sets power, PC0; queries. We have no server protocol
   // command for TX power, so keep a local value and echo it back.
   static int32_t pwr = 100;       // default 100W

   if (args && args[0] == '0' && args[1]) {      // PC0xxx - set
      pwr = atoi(args + 1);
   }
   rr_cat_printf("PC0%03d;", pwr);
}

// Power switch: hamlib probes the rig with "PS;" at connect; a real rig on
// reports PS1, so we always claim to be powered on.
void rr_cat_yaesu_power(const char *args) {
   if (!args || !args[0]) {
      rr_cat_printf("PS1;");
      Log(LOG_DEBUG, "cat.yaesu", "PS query -> 1");
      return;
   }
   Log(LOG_DEBUG, "cat.yaesu", "Set PS to %c", args[0]);
}               // Read/Toggle power

void rr_cat_yaesu_qmb_store(const char *args) {
}               // QMB Store

void rr_cat_yaesu_width(const char *args) {
   // Newcat band-qualified form, like MD: "SH0;" queries the main band
   // passband width (reply "SH0<nnnn>;"), "SH0<nnnn>;" sets it.  The digit
   // after SH is the band index, not a width value.
   if (!args || !args[0]) {
      // Legacy "SH;" query
      int w = (int)vfo_state_get_long("A", "cat.state.width", 2400);
      rr_cat_printf("SH%04d;", w);
      Log(LOG_DEBUG, "cat.yaesu", "WIDTH read -> %d", w);
      return;
   }

   if (args[0] == '0' || args[0] == '1') {
      if (!args[1]) {
         // "SH0;" -- query
         int w = (int)vfo_state_get_long("A", "cat.state.width", 2400);
         rr_cat_printf("SH0%04d;", w);
         Log(LOG_DEBUG, "cat.yaesu", "WIDTH read (band %c) -> %d", args[0], w);
         return;
      }

      // "SH0<nnnn>;" -- set width on the main band
      Log(LOG_INFO, "cat.yaesu", "Set WIDTH to %s", args + 1);
      ws_send_width_cmd(ws_conn, cat_vfo_str(0), args + 1);
      return;
   }

   // Legacy "SH<nnnn>;" set form
   Log(LOG_INFO, "cat.yaesu", "Set WIDTH to %s", args);
   ws_send_width_cmd(ws_conn, cat_vfo_str(0), args);
}               // Width

void rr_cat_yaesu_s_meter(const char *args) {
}               // Read S-meter/SWR/ALC/COMP

void rr_cat_yaesu_squelch(const char *args) {
}               // Read/Set squelch

void rr_cat_yaesu_split(const char *args) {
}               // Split

void rr_cat_yaesu_swap_vfo(const char *args) {
}               // Swap VFOs

void rr_cat_yaesu_txw(const char *args) {
}               // Read/Set Tuning step

// PTT set/query: "TX;" (no args) is a read -- hamlib polls TX state to sync;
// report from the server-echoed cat.state.ptt.
void rr_cat_yaesu_ptt(const char *args) {
   if (!args || !args[0]) {
      bool ptt = vfo_state_get_bool("A", "cat.state.ptt", false);
      rr_cat_printf("TX%d;", ptt ? 1 : 0);
      Log(LOG_DEBUG, "cat.yaesu", "PTT query -> %d", ptt ? 1 : 0);
      return;
   }

   bool ptt = (args[0] == '1');

   Log(LOG_INFO, "cat.yaesu", "Set PTT to %s", (ptt ? "ON" : "OFF") );
   ws_send_ptt_cmd(ws_conn, cat_vfo_str(0), ptt);
}

void rr_cat_yaesu_unlock(const char *args) {
}               // Unlock

void rr_cat_yaesu_up(const char *args) {
}                       // UP

void rr_cat_yaesu_vfo_mem(const char *args) {
}               // VFO/Memory button

void rr_cat_yaesu_vox_gain(const char *args) {
}               // VOX Gain

void rr_cat_yaesu_vox(const char *args) {
}                       // VOX

// Keying speed (CW WPM): hamlib's FT-891 backend queries "KS;" at connect;
// report a sane default and store whatever the client sets.
void rr_cat_yaesu_key_speed(const char *args) {
   if (!args || !args[0]) {
      long speed = vfo_state_get_long("A", "cat.state.key_speed", 450);
      rr_cat_printf("KS%03ld;", speed);
      Log(LOG_DEBUG, "cat.yaesu", "KS query -> %ld", speed);
      return;
   }

   long speed = atol(args);
   Log(LOG_DEBUG, "cat.yaesu", "Set KS to %ld", speed);
}

void rr_cat_yaesu_zero_in(const char *args) {
}               // Zero In

CATcmdTable rr_cat_yaesu_commands[] = {
   // cmd, min, max, callback
   {
      "AB", 0, 1, rr_cat_yaesu_vfo_a_to_b
   },                                                   // VFO A to VFO B
   {
      "AC", 1, 1, rr_cat_yaesu_tuner_control
   },                                                   // Antenna Tuner Control
   {
      "AG", 1, 1, rr_cat_yaesu_af_gain
   },                                                   // AF gain
   {
      "AI", 1, 1, rr_cat_yaesu_auto_info
   },                                                   // Auto Information
   {
      "AM", 1, 1, rr_cat_yaesu_vfo_a_to_mem
   },                                                   // Store VFO A to memory
   {
      "AN", 1, 1, rr_cat_yaesu_ant_select
   },                                                   // ANtenna selection
                                                        // (not on ft891)
   {
      "BA", 0, 1, rr_cat_yaesu_vfo_b_to_a
   },                                                   // VFO B to VFO A
   {
      "BC", 1, 1, rr_cat_yaesu_auto_notch
   },                                                   // Auto Notch
   {
      "BI", 1, 1, rr_cat_yaesu_break_in
   },                                                   // Break-in
   {
      "BD", 1, 1, rr_cat_yaesu_band_down
   },                                                   // Band Down
   {
      "BP", 1, 1, rr_cat_yaesu_manual_notch
   },                                                   // Manual notch
   {
      "BS", 1, 1, rr_cat_yaesu_band_select
   },                                                   // Band Select
   {
      "BU", 1, 1, rr_cat_yaesu_band_up
   },                                                   // Band up
   {
      "BY", 1, 1, rr_cat_yaesu_busy
   },                                                   // Busy
   {
      "CF", 1, 1, rr_cat_yaesu_clarifier
   },                                                   // Clarifier
   {
      "CH", 0, 1, rr_cat_yaesu_channel
   },                                                   // Channel Up/Down
   {
      "CN", 0, 1, rr_cat_yaesu_ctcss_number
   },                                                   // CTCSS Number
   {
      "CO", 0, 1, rr_cat_yaesu_contour
   },                                                   // Contour
   {
      "CS", 0, 1, rr_cat_yaesu_cw_spot
   },                                                   // CW Spot
   {
      "CT", 0, 1, rr_cat_yaesu_ctcss
   },                                                   // CTCSS
   {
      "DA", 0, 1, rr_cat_yaesu_dimmer
   },                                                   // Dimmer
   {
      "DN", 0, 0, rr_cat_yaesu_down
   },                                                   // DOWN
   {
      "ED", 0, 1, rr_cat_yaesu_encoder_down
   },                                                   // Encoder Down
   {
      "EK", 0, 1, rr_cat_yaesu_enter
   },                                                   // Enter key
   {
      "EU", 0, 1, rr_cat_yaesu_encoder_up
   },                                                   // Encoder Up
   {
      "EX", 1, 1, rr_cat_yaesu_menu
   },                                                   // Menu
   {
      "FA", 1, 1, rr_cat_yaesu_set_vfo_a
   },                                                   // Set VFO A freq
   {
      "FB", 1, 1, rr_cat_yaesu_set_vfo_b
   },                                                   // Set VFO B freq
   {
      "FS", 0, 1, rr_cat_yaesu_fast_step
   },                                                   // Fast Step
   {
      "GT", 0, 1, rr_cat_yaesu_agc_func
   },                                                   // AGC Function
   {
      "ID", 0, 1, rr_cat_yaesu_id
   },                                                   // IDentification
   {
      "IF", 0, 0, rr_cat_yaesu_info
   },                                                   // Information
   {
      "IS", 1, 1, rr_cat_yaesu_if_shift
   },                                                   // IF shift
   {
      "KY", 1, 1, rr_cat_yaesu_cw_key
   },                                                   // CW keying
   {
      "LK", 1, 1, rr_cat_yaesu_lock
   },                                                   // Lock
   {
      "MA", 1, 1, rr_cat_yaesu_mem_to_vfo_a
   },                                                   // Memory to VFO A
   {
      "MC", 1, 1, rr_cat_yaesu_mem_channel
   },                                                   // Memory Channel
   {
      "MD", 1, 1, rr_cat_yaesu_mode
   },                                                   // Mode
   {
      "MG", 0, 1, rr_cat_yaesu_mic_gain
   },                                                   // Mic Gain
   {
      "ML", 0, 1, rr_cat_yaesu_monitor_level
   },                                                   // Monitor Level
   {
      "MR", 0, 1, rr_cat_yaesu_memory_read
   },                                                   // Memory Read
   {
      "MS", 0, 1, rr_cat_yaesu_meter_switch
   },                                                   // Meter Switch
   {
      "MT", 0, 1, rr_cat_yaesu_mem_write_tag
   },                                                   // Memory Write and Tag
   {
      "MW", 0, 1, rr_cat_yaesu_mem_write
   },                                                   // Memory Write
   {
      "MX", 0, 1, rr_cat_yaesu_mox
   },                                                   // MOX
   {
      "NA", 0, 1, rr_cat_yaesu_narrow
   },                                                   // Narrow
   {
      "NB", 0, 1, rr_cat_yaesu_noise_blanker
   },                                                   // Noise Blanker
   {
      "NL", 0, 1, rr_cat_yaesu_nb_level
   },                                                   // NB Level
   {
      "NR", 1, 1, rr_cat_yaesu_noise_reduction
   },                                                   // Noise Reduction
   {
      "OI", 0, 1, rr_cat_yaesu_obi
   },                                                   // Other Band Info?
   {
      "OS", 0, 1, rr_cat_yaesu_offset
   },                                                   // Offset
   {
      "PA", 0, 1, rr_cat_yaesu_preamp
   },                                                   // Preamp
   {
      "PB", 0, 1, rr_cat_yaesu_playback
   },                                                   // Playback
   {
      "PC", 1, 1, rr_cat_yaesu_tx_power
   },                                                   // TX power level
   {
      "PL", 0, 1, rr_cat_yaesu_preproc_level
   },                                                   // Speech preprocessor
                                                        // level
   {
      "PR", 0, 1, rr_cat_yaesu_preproc
   },                                                   // Speech preprocessor
   {
      "PS", 0, 0, rr_cat_yaesu_power
   },                                                   // Power Switch
   {
      "QI", 0, 1, rr_cat_yaesu_qmb_store
   },                                                   // QMB Store
   {
      "SH", 0, 1, rr_cat_yaesu_width
   },                                                   // Width
   {
      "SM", 1, 1, rr_cat_yaesu_s_meter
   },                                                   // S-Meter
   {
      "SQ", 1, 1, rr_cat_yaesu_squelch
   },                                                   // Squelch
   {
      "ST", 1, 1, rr_cat_yaesu_split
   },                                                   // Split
   {
      "SV", 1, 1, rr_cat_yaesu_swap_vfo
   },                                                   // Swap VFO
   {
      "TS", 1, 1, rr_cat_yaesu_txw
   },                                                   // TXW?
   {
         "TX", 0, 1, rr_cat_yaesu_ptt
      },                                                   // TX Set (PTT)
   {
      "UP", 0, 0, rr_cat_yaesu_down
   },                                                   // UP
   {
      "UL", 0, 1, rr_cat_yaesu_unlock
   },                                                   // Unlock
   {
      "VG", 0, 1, rr_cat_yaesu_vox_gain
   },                                                   // VOX Gain
   {
      "VM", 0, 1, rr_cat_yaesu_vfo_mem
   },                                                   // VFO/Memory button
   {
      "VX", 0, 1, rr_cat_yaesu_vox
   },                                                   // VOX
   {
         "KS", 0, 1, rr_cat_yaesu_key_speed
      },                                                   // Keying Speed
      {
         "ZI", 0, 1, rr_cat_yaesu_zero_in
      },                                                   // Zero in
   {
      NULL, -1, -1, NULL
   }                                                     // Terminator
};

bool rr_cat_yaesu_init(void) {
   Log(LOG_INFO, "cat", "Rig CAT control (yaesu emu) initialized");

   return false;
}

bool rr_cat_yaesu_parse(const char *msg) {
   if (!msg) {
      return true;
   }

   return false;
}
#endif
