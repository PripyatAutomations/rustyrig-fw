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
 * Here we implement the Yaesu 891/991a style CAT protocol for control of the rig ft891 has complete CAT enough for all uses, so it's our milestone goal
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
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cat.h>
#include <rrclient/vfo.h>
#include <rrclient/rooms.h>
#include <rrclient/sercom.h>

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

   // Commands without a selector use this endpoint's configured VFO.
   // Non-serial callers retain the shared native-client selector.
   buf[0] = vfo ? vfo : rr_cat_active_vfo();
   buf[1] = '\0';

   return buf;
}

// Map the server-echoed mode string (cat.state.mode) to a Yaesu mode digit.
// Yaesu digits: 1=LSB 2=USB 3=CW 4=FM 6=RTTY-L 7=RTTY-U 8=PKT-L 9=PKT-U
static int cat_mode_digit(void) {
   const char *mode = rr_cat_property("A", "mode", NULL);

   if (!mode) {
      return 2;                 // USB is a sane default for HF digital
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
      case '1': {
         mode = "LSB";
         break;
      }
      case '2': {
         mode = "USB";
         break;
      }
      case '3': {
         mode = "CW";
         break;
      }
      case '4': {
         mode = "FM";
         break;
      }
      case '6': {
         mode = "D-L";
         break;
      }
      case '7': {
         mode = "D-U";
         break;
      }
      case '8': {
         mode = "D-L";
         break;
      }
      case '9': {
         mode = "D-U";
         break;
      }
      default: {
         Log(LOG_WARN, "cat.yaesu", "Unknown MODE digit %c", digit[0]);

         return;
      }
   }

   Log(LOG_CRAZY, "cat.yaesu", "Set MODE to %s", mode);
   ws_send_mode_cmd_in_room(ws_conn, cat_vfo_str(0), mode, rr_cat_room());
}

/* Copy a complete known VFO state without selecting either bank. The destination's frequency and mode are applied through the usual room-scoped CAT control
 * path. Never overwrite with guessed values. */
static void cat_copy_vfo(const char *source, const char *destination) {
   const char *room = rr_cat_room();
   long frequency = rr_cat_property_long(source, "frequency", 0);
   const char *mode = rr_cat_property(source, "mode", NULL);

   if (!room || !*room || frequency <= 0 || frequency > INT_MAX || !mode || !*mode) {
      Log(LOG_WARN, "cat.yaesu", "Cannot copy VFO %s to %s: frequency/mode state unavailable", source, destination);

      return;
   }
   char saved_mode[32];
   snprintf(saved_mode, sizeof(saved_mode), "%s", mode);
   ws_send_freq_cmd_in_room(ws_conn, destination, frequency, room);
   ws_send_mode_cmd_in_room(ws_conn, destination, saved_mode, room);
   Log(LOG_INFO, "cat.yaesu", "Copied VFO %s to %s in %s", source, destination, room);
}

// Function stubs for Yaesu New CAT commands
void rr_cat_yaesu_vfo_a_to_b(const char *args) {
   cat_copy_vfo("A", "B");
}                               // VFO A to VFO B

void rr_cat_yaesu_tuner_control(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_tuner_control: args='%s' (no reply/action)", args ? args : "");
}                               // Antenna Tuner Control

void rr_cat_yaesu_af_gain(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_af_gain: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set AF gain

// Auto Information mode: hamlib queries "AI;" during connect and expects a
// reply; we always report it off (we don't push unsolicited CAT frames).
void rr_cat_yaesu_auto_info(const char *args) {
   if (!args || !args[0]) {
      rr_cat_printf("AI0;");
      Log(LOG_CRAZY, "cat.yaesu", "AI query -> 0");

      return;
   }
   Log(LOG_CRAZY, "cat.yaesu", "Set AI to %c", args[0]);
}                               // Enable/Disable Auto Information mode

void rr_cat_yaesu_vfo_a_to_mem(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_vfo_a_to_mem: args='%s' (no reply/action)", args ? args : "");
}                               // Store VFO A to memory channel

void rr_cat_yaesu_vfo_b_to_a(const char *args) {
   cat_copy_vfo("B", "A");
}                               // VFO B to VFO A

void rr_cat_yaesu_ant_select(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_ant_select: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set Antenna selection

void rr_cat_yaesu_auto_notch(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_auto_notch: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set Auto Notch

void rr_cat_yaesu_band_down(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_band_down: args='%s' (no reply/action)", args ? args : "");
}                               // Band Down

void rr_cat_yaesu_band_select(const char *args) {
   /* BS selects a band, not an exact dial frequency. Hamlib/WSJT-X subsequently uses FA to tune; do not invent a frequency here. */
   static const char *bands[] = {
      "1.8", "3.5", "5", "7", "10", "14", "18", "21",
      "24.5", "28", "50", "GEN", "MW", NULL, "AIR", "144", "430"
   };

   if (!args || strlen(args) != 2 || args[0] < '0' || args[0] > '1' || args[1] < '0' || args[1] > '9') {
      Log(LOG_WARN, "cat.yaesu", "Invalid FT-991A BS band code '%s'", args ? args : "");

      return;
   }
   int index = (args[0] - '0') * 10 + (args[1] - '0');

   if (index >= (int)(sizeof(bands) / sizeof(bands[0])) || !bands[index]) {
      Log(LOG_WARN, "cat.yaesu", "Unsupported FT-991A BS band '%s'", args);

      return;
   }
   Log(LOG_DEBUG, "cat.yaesu", "FT-991A band select %s MHz (%s): waiting for explicit FA tune", bands[index], args);
}                               // Band Select

void rr_cat_yaesu_band_up(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_band_up: args='%s' (no reply/action)", args ? args : "");
}                               // band up

void rr_cat_yaesu_break_in(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_break_in: args='%s' (no reply/action)", args ? args : "");
}                               // Break-in

void rr_cat_yaesu_manual_notch(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_manual_notch: args='%s' (no reply/action)", args ? args : "");
}                               // Manual Notch

void rr_cat_yaesu_busy(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_busy: args='%s' (no reply/action)", args ? args : "");
}                               // Busy

void rr_cat_yaesu_clarifier(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_clarifier: args='%s' (no reply/action)", args ? args : "");
}                               // Clarifier

void rr_cat_yaesu_channel(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_channel: args='%s' (no reply/action)", args ? args : "");
}                               // Channel

void rr_cat_yaesu_ctcss_number(const char *arg) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_ctcss_number: args='%s' (no reply/action)", arg ? arg : "");
}                               // CTCSS Number

void rr_cat_yaesu_contour(const char *arg) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_contour: args='%s' (no reply/action)", arg ? arg : "");
}                               // Contour

void rr_cat_yaesu_cw_spot(const char *arg) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_cw_spot: args='%s' (no reply/action)", arg ? arg : "");
}                               // CW Spot

void rr_cat_yaesu_ctcss(const char *arg) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_ctcss: args='%s' (no reply/action)", arg ? arg : "");
}                               // CTCSS

void rr_cat_yaesu_dimmer(const char *arg) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_dimmer: args='%s' (no reply/action)", arg ? arg : "");
}                               // Dimmer

void rr_cat_yaesu_down(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_down: args='%s' (no reply/action)", args ? args : "");
}                               // Down

void rr_cat_yaesu_encoder_down(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_encoder_down: args='%s' (no reply/action)", args ? args : "");
}                               // Encoder Down

void rr_cat_yaesu_enter(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_enter: args='%s' (no reply/action)", args ? args : "");
}                               // Enter key

void rr_cat_yaesu_encoder_up(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_encoder_up: args='%s' (no reply/action)", args ? args : "");
}                               // Encoder Up

/* FT-991A EX032 is CAT TOT (CAT input timeout); values 0..3 denote 10, 100, 1000 and 3000 ms. This is a virtual serial port, so retain the setting solely for
 * CAT readback, not as a backend timeout. */

void rr_cat_yaesu_menu(const char *args) {
   if (args && !strcmp(args, "032")) {
      rr_cat_printf("EX032%c;", rr_cat_timeout_get());

      return;
   }

   if (args && strlen(args) == 4 && !strncmp(args, "032", 3) && args[3] >= '0' && args[3] <= '3') {
      rr_cat_timeout_set(args[3]);
      Log(LOG_DEBUG, "cat.yaesu", "Virtual FT-991A CAT timeout setting: %c", rr_cat_timeout_get());

      return;
   }
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A EX menu: args='%s' (no reply/action)", args ? args : "");
}                               // Extended menu commands

void rr_cat_yaesu_set_vfo_a(const char *args) {
   // Read ("FA;") replies with the current frequency; set applies via backend
   if (!args || !args[0]) {
      long freq = rr_cat_property_long("A", "frequency", 0);
      // FT-991A New CAT uses 9-digit frequency fields; the 11-digit
      // form is the FTDX101/5000 family. Wrong width shifts the parsed value.
      rr_cat_printf("FA%09ld;", freq);
      Log(LOG_CRAZY, "cat.yaesu", "VFO A read -> %ld", freq);

      return;
   }

   for (const unsigned char *p = (const unsigned char *)args ; *p ; p++) {
      if (!isdigit(*p)) {
         return;
      }
   }

   char *end = NULL;
   errno = 0;
   long freq = strtol(args, &end, 10);

   if (errno || end == args || *end || freq <= 0 || freq > INT_MAX) {
      return;
   }

   Log(LOG_CRAZY, "cat.yaesu", "Set VFO A freq to %ld", freq);
   ws_send_freq_cmd_in_room(ws_conn, cat_vfo_str('A'), freq, rr_cat_room());
}                               // Read/Set VFO A frequency

void rr_cat_yaesu_set_vfo_b(const char *args) {
   if (!args || !args[0]) {
      long freq = rr_cat_property_long("B", "frequency", 0);
      rr_cat_printf("FB%09ld;", freq);
      Log(LOG_CRAZY, "cat.yaesu", "VFO B read -> %ld", freq);

      return;
   }

   for (const unsigned char *p = (const unsigned char *)args ; *p ; p++) {
      if (!isdigit(*p)) {
         return;
      }
   }

   char *end = NULL;
   errno = 0;
   long freq = strtol(args, &end, 10);

   if (errno || end == args || *end || freq <= 0 || freq > INT_MAX) {
      return;
   }

   Log(LOG_CRAZY, "cat.yaesu", "Set VFO B freq to %ld", freq);
   ws_send_freq_cmd_in_room(ws_conn, cat_vfo_str('B'), freq, rr_cat_room());
}                               // Read/Set VFO B frequency

void rr_cat_yaesu_fast_step(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_fast_step: args='%s' (no reply/action)", args ? args : "");
}                               // Fast Step

void rr_cat_yaesu_agc_func(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_agc_func: args='%s' (no reply/action)", args ? args : "");
}                               // AGC Function

void rr_cat_yaesu_id(const char *args) {
   /* Advertise the model configured for this CAT endpoint. */
   const char *model = rr_cat_model();

   if (!strcmp(model, "ft891")) {
      rr_cat_printf("ID0911;");
   } else {
      rr_cat_printf("ID0670;");
   }
   Log(LOG_CRAZY, "cat.yaesu", "ID query -> %s", model);
}                               // IDentification

void rr_cat_yaesu_info(const char *args) {
   /* FT-991A IF answer: 3-digit channel, 9-digit VFO-A frequency, clarifier sign/offset, CLAR flag, fixed zero, mode, VFO/memory, CTCSS, two fixed digits and
    * simplex/shift. 28 bytes in total. */
   if (args && *args) {
      return;                   /* IF is read-only. */
   }
   long freq = rr_cat_property_long("A", "frequency", 0);

   if (freq < 0 || freq > 999999999L) {
      freq = 0;
   }
   int mode = cat_mode_digit();
   rr_cat_printf("IF%03d%09ld+%04d%c%c%c%c%c%02d%c;", 0, freq, 0, '0', '0', "0123456789ABCD"[mode >= 0 && mode <= 13 ? mode : 2], '0', '0', 0, '0');
   Log(LOG_CRAZY, "cat.yaesu", "IF status: VFO A=%ld mode=%d", freq, mode);
}                               // Get receiver status

void rr_cat_yaesu_if_shift(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_if_shift: args='%s' (no reply/action)", args ? args : "");
}                               // IF shift

void rr_cat_yaesu_cw_key(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_cw_key: args='%s' (no reply/action)", args ? args : "");
}                               // Send CW Keying

void rr_cat_yaesu_lock(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_lock: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set lock status

void rr_cat_yaesu_mem_to_vfo_a(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_mem_to_vfo_a: args='%s' (no reply/action)", args ? args : "");
}                               // Memory to VFO A

void rr_cat_yaesu_mem_channel(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_mem_channel: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set Memory Channel data

void rr_cat_yaesu_mic_gain(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_mic_gain: args='%s' (no reply/action)", args ? args : "");
}                               // Mic Gain

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
      Log(LOG_CRAZY, "cat.yaesu", "MODE read -> %d", digit);

      return;
   }

   // Band-qualified form: MD0 [; | <mode digit>]. NB: the digit is the
   // VFO/band selector (0=A, 1=B) and hamlib expects it ECHOED in the reply
   // ("MD1;" -> "MD1<n>;"); replying with the wrong selector makes
   // newcat_get_mode fail with -RIG_EPROTO ("Protocol error").
   if (args[0] == '0' || args[0] == '1') {
      // band index (we only have main)
      if (!args[1]) {
         // "MD0;" -- query: reply MD<n-selector><mode>;
         int digit = cat_mode_digit();
         rr_cat_printf("MD%c%d;", args[0], digit);
         Log(LOG_CRAZY, "cat.yaesu", "MODE read (band %c) -> %d", args[0], digit);

         return;
      }
      // "MD0<n>;" -- set mode <n> on the main band
      char digit[2] = {
         args[1], 0
      };
      cat_mode_set(digit);

      return;
   }
   // Legacy set form: "MD<n>;"
   char digit[2] = {
      args[0], 0
   };
   cat_mode_set(digit);
}                               // Read/Set operating mode

void rr_cat_yaesu_monitor_level(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_monitor_level: args='%s' (no reply/action)", args ? args : "");
}                               // Monitor Level

void rr_cat_yaesu_memory_read(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_memory_read: args='%s' (no reply/action)", args ? args : "");
}                               // Memory Read

void rr_cat_yaesu_meter_switch(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_meter_switch: args='%s' (no reply/action)", args ? args : "");
}                               // Meter Switch

void rr_cat_yaesu_mem_write_tag(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_mem_write_tag: args='%s' (no reply/action)", args ? args : "");
}                               // Memory write & tag

void rr_cat_yaesu_mem_write(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_mem_write: args='%s' (no reply/action)", args ? args : "");
}                               // Memory write

void rr_cat_yaesu_mox(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_mox: args='%s' (no reply/action)", args ? args : "");
}                               // MOX

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
      Log(LOG_CRAZY, "cat.yaesu", "NARROW query -> 0");

      return;
   }

   if ((args[0] == '0' || args[0] == '1') && !args[1]) {
      // band-qualified query ("NA0;" or "NA1;") - echo the selector back
      rr_cat_printf("NA%c0;", args[0]);
      Log(LOG_CRAZY, "cat.yaesu", "NARROW query (band %c) -> 0", args[0]);

      return;
   }
   // Set: args[0] is 0/1. Map onto the width command (narrow/normal) so the
   // server applies a real filter change.
   Log(LOG_CRAZY, "cat.yaesu", "Set NARROW to %c", args[0]);
   ws_send_width_cmd_in_room(ws_conn, cat_vfo_str(0), (args[0] == '1' ? "narrow" : "normal"), rr_cat_room());
}                               // Narrow

void rr_cat_yaesu_noise_blanker(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_noise_blanker: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set Noise Blanker

void rr_cat_yaesu_nb_level(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_nb_level: args='%s' (no reply/action)", args ? args : "");
}                               // NB Level

void rr_cat_yaesu_noise_reduction(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_noise_reduction: args='%s' (no reply/action)", args ? args : "");
}                               // Noise Reduction

void rr_cat_yaesu_obi(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_obi: args='%s' (no reply/action)", args ? args : "");
}                               // Opposite Band Info??

void rr_cat_yaesu_offset(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_offset: args='%s' (no reply/action)", args ? args : "");
}                               // Offset

void rr_cat_yaesu_preamp(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_preamp: args='%s' (no reply/action)", args ? args : "");
}                               // Preamp

void rr_cat_yaesu_playback(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_playback: args='%s' (no reply/action)", args ? args : "");
}                               // Playback

void rr_cat_yaesu_preproc(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_preproc: args='%s' (no reply/action)", args ? args : "");
}                               // Preprocessor

void rr_cat_yaesu_preproc_level(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_preproc_level: args='%s' (no reply/action)", args ? args : "");
}                               // Preprocessor level

void rr_cat_yaesu_tx_power(const char *args) {
   // FT-891: PC0xxx sets power, PC0; queries. We have no server protocol
   // command for TX power, so keep a local value and echo it back.
   static int32_t pwr = 100;    // default 100W

   if (args && args[0] == '0' && args[1]) {
      // PC0xxx - set
      pwr = atoi(args + 1);
   }
   rr_cat_printf("PC0%03d;", pwr);
}

// Power switch: hamlib probes the rig with "PS;" at connect; a real rig on
// reports PS1, so we always claim to be powered on.
void rr_cat_yaesu_power(const char *args) {
   if (!args || !args[0]) {
      rr_cat_printf("PS1;");
      Log(LOG_CRAZY, "cat.yaesu", "PS query -> 1");

      return;
   }
   Log(LOG_CRAZY, "cat.yaesu", "Set PS to %c", args[0]);
}                               // Read/Toggle power

void rr_cat_yaesu_qmb_store(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_qmb_store: args='%s' (no reply/action)", args ? args : "");
}                               // QMB Store

void rr_cat_yaesu_width(const char *args) {
   // Newcat band-qualified form, like MD: "SH0;" queries the main band
   // passband width (reply "SH0<nnnn>;"), "SH0<nnnn>;" sets it.  The digit
   // after SH is the band index, not a width value.
   if (!args || !args[0]) {
      // Legacy "SH;" query
      int w = (int)rr_cat_property_long("A", "width", 2400);
      rr_cat_printf("SH%04d;", w);
      Log(LOG_CRAZY, "cat.yaesu", "WIDTH read -> %d", w);

      return;
   }

   if (args[0] == '0' || args[0] == '1') {
      if (!args[1]) {
         // "SH0;" -- query
         /* Hamlib 4.6.2 NewCAT accepts SH replies of length 6 or 7 (including ';'), not a raw four-digit Hz bandwidth. A zero three-digit filter code means
          * default bandwidth; this is safer than inventing a model-specific SH code from Hz. */
         rr_cat_printf("SH000;");
         Log(LOG_CRAZY, "cat.yaesu", "WIDTH read (band %c) -> default filter code 000", args[0]);

         return;
      }
      // "SH0<nnnn>;" -- set width on the main band
      Log(LOG_CRAZY, "cat.yaesu", "Set WIDTH to %s", args + 1);
      ws_send_width_cmd_in_room(ws_conn, cat_vfo_str(0), args + 1, rr_cat_room());

      return;
   }
   // Legacy "SH<nnnn>;" set form
   Log(LOG_CRAZY, "cat.yaesu", "Set WIDTH to %s", args);
   ws_send_width_cmd_in_room(ws_conn, cat_vfo_str(0), args, rr_cat_room());
}                               // Width

void rr_cat_yaesu_s_meter(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_s_meter: args='%s' (no reply/action)", args ? args : "");
}                               // Read S-meter/SWR/ALC/COMP

void rr_cat_yaesu_squelch(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_squelch: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set squelch

void rr_cat_yaesu_split(const char *args) {
   const char *room = rr_cat_room();

   if (!room || !*room) {
      return;
   }

   if (!args || !args[0]) {
      /* Do not invent split state. Real readback requires a confirmed server-side Hamlib observation. */
      long state = rr_cat_property_long(cat_vfo_str(0), "split", -1);

      if (state == 0 || state == 1) {
         rr_cat_printf("ST%ld;", state);
      } else {
         Log(LOG_DEBUG, "cat.yaesu", "ST readback unavailable for %s", room);
      }

      return;
   }

   if ((args[0] != '0' && args[0] != '1') || args[1]) {
      Log(LOG_WARN, "cat.yaesu", "Unsupported FT-891 ST value %s", args);

      return;
   }
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "msg.type", "cat");
   dict_add(d, "cat.cmd", "split");
   dict_add(d, "cat.room", room);
   dict_add(d, "cat.vfo", cat_vfo_str(0));
   dict_add_bool(d, "cat.split", args[0] == '1');
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
}                               // Split

void rr_cat_yaesu_swap_vfo(const char *args) {
   /* FT-891 SV; is an A/B exchange. In RustyRig the server's active-VFO transition handles it through Hamlib's XCHG fallback, preserving the logical VFO bank
    * mapping. */
   const char *room = rr_cat_room();

   if (!room || !*room) {
      Log(LOG_WARN, "cat.yaesu", "SV requires a rig-room CAT binding");

      return;
   }
   char current = rrclient_room_active_vfo(room);
   char target = current == 'B' ? 'A' : 'B';
   char command[16];
   snprintf(command, sizeof(command), "!vfo %c", target);

   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "msg.type", "talk");
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.msg_type", "pub");
   dict_add(d, "talk.target", room);
   dict_add(d, "talk.data", command);

   if (!ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT)) {
      Log(LOG_WARN, "cat.yaesu", "SV unable to request VFO %c in %s", target, room);
   }
   dict_free(d);
}                               // Swap VFOs

void rr_cat_yaesu_txw(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_txw: args='%s' (no reply/action)", args ? args : "");
}                               // Read/Set Tuning step

// PTT set/query: "TX;" (no args) is a read -- hamlib polls TX state to sync;
// report from the server-echoed cat.state.ptt.
void rr_cat_yaesu_ptt(const char *args) {
   if (!args || !args[0]) {
      bool ptt = rr_cat_property_bool("ptt", false);
      rr_cat_printf("TX%d;", ptt ? 1 : 0);
      Log(LOG_CRAZY, "cat.yaesu", "PTT query -> %d", ptt ? 1 : 0);

      return;
   }

   bool ptt = (args[0] == '1');

   Log(LOG_CRAZY, "cat.yaesu", "Set PTT to %s", (ptt ? "ON" : "OFF"));
   extern void rrclient_send_ptt_command(rrconn_t *, const char *, bool, const char *);
   rrclient_send_ptt_command(ws_conn, cat_vfo_str(0), ptt, rr_cat_room());
}

void rr_cat_yaesu_unlock(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_unlock: args='%s' (no reply/action)", args ? args : "");
}                               // Unlock

void rr_cat_yaesu_up(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_up: args='%s' (no reply/action)", args ? args : "");
}                               // UP

void rr_cat_yaesu_vfo_mem(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_vfo_mem: args='%s' (no reply/action)", args ? args : "");
}                               // VFO/Memory button

void rr_cat_yaesu_vox_gain(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_vox_gain: args='%s' (no reply/action)", args ? args : "");
}                               // VOX Gain

void rr_cat_yaesu_vox(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_vox: args='%s' (no reply/action)", args ? args : "");
}                               // VOX

// Keying speed (CW WPM): hamlib's FT-891 backend queries "KS;" at connect;
// report a sane default and store whatever the client sets.
void rr_cat_yaesu_key_speed(const char *args) {
   if (!args || !args[0]) {
      long speed = rr_cat_property_long("A", "key_speed", 450);
      rr_cat_printf("KS%03ld;", speed);
      Log(LOG_CRAZY, "cat.yaesu", "KS query -> %ld", speed);

      return;
   }

   long speed = atol(args);
   Log(LOG_CRAZY, "cat.yaesu", "Set KS to %ld", speed);
}

void rr_cat_yaesu_zero_in(const char *args) {
   Log(LOG_WARN, "cat.yaesu", "UNSUPPORTED FT-991A CAT handler rr_cat_yaesu_zero_in: args='%s' (no reply/action)", args ? args : "");
}                               // Zero In

/* FT reports/selects the transmit VFO. The virtual interface tracks the requested selector for Hamlib readback; this does not key a transmitter. */
static void rr_cat_yaesu_tx_vfo(const char *args) {
   if (!args || !*args) {
      rr_cat_printf("FT%d;", rr_cat_tx_vfo_get());

      return;
   }

   if (args[1] || (args[0] != '0' && args[0] != '1' && args[0] != '2' && args[0] != '3')) {
      Log(LOG_WARN, "cat.yaesu", "Unsupported FT-991A FT selector '%s'", args);

      return;
   }
   /* Hamlib uses FT2/FT3 when setting up split, corresponding to A/B. */
   rr_cat_tx_vfo_set(args[0] == '1' || args[0] == '3' ? 1 : 0);
   Log(LOG_DEBUG, "cat.yaesu", "Virtual transmit VFO %c selected", rr_cat_tx_vfo_get() ? 'B' : 'A');
}

CATcmdTable rr_cat_yaesu_commands[] = {
   // cmd, min, max, callback
   {
      "AB", 0, 0, rr_cat_yaesu_vfo_a_to_b
   },                                        // VFO A to VFO B
   {
      "AC", 1, 1, rr_cat_yaesu_tuner_control
   },                                           // Antenna Tuner Control
   {
      "AG", 1, 1, rr_cat_yaesu_af_gain
   },                                     // AF gain
   {
      "AI", 1, 1, rr_cat_yaesu_auto_info
   },                                        // Auto Information
   {
      "AM", 1, 1, rr_cat_yaesu_vfo_a_to_mem
   },                                           // Store VFO A to memory
   {
      "AN", 1, 1, rr_cat_yaesu_ant_select
   },                                        // ANtenna selection
   // (not on ft891)
   {
      "BA", 0, 0, rr_cat_yaesu_vfo_b_to_a
   },                                        // VFO B to VFO A
   {
      "BC", 1, 1, rr_cat_yaesu_auto_notch
   },                                        // Auto Notch
   {
      "BI", 1, 1, rr_cat_yaesu_break_in
   },                                        // Break-in
   {
      "BD", 1, 1, rr_cat_yaesu_band_down
   },                                        // Band Down
   {
      "BP", 1, 1, rr_cat_yaesu_manual_notch
   },                                           // Manual notch
   {
      "BS", 1, 1, rr_cat_yaesu_band_select
   },                                           // Band Select
   {
      "BU", 1, 1, rr_cat_yaesu_band_up
   },                                     // Band up
   {
      "BY", 1, 1, rr_cat_yaesu_busy
   },                                  // Busy
   {
      "CF", 1, 1, rr_cat_yaesu_clarifier
   },                                        // Clarifier
   {
      "CH", 0, 1, rr_cat_yaesu_channel
   },                                     // Channel Up/Down
   {
      "CN", 0, 1, rr_cat_yaesu_ctcss_number
   },                                           // CTCSS Number
   {
      "CO", 0, 1, rr_cat_yaesu_contour
   },                                     // Contour
   {
      "CS", 0, 1, rr_cat_yaesu_cw_spot
   },                                     // CW Spot
   {
      "CT", 0, 1, rr_cat_yaesu_ctcss
   },                                     // CTCSS
   {
      "DA", 0, 1, rr_cat_yaesu_dimmer
   },                                     // Dimmer
   {
      "DN", 0, 0, rr_cat_yaesu_down
   },                                  // DOWN
   {
      "ED", 0, 1, rr_cat_yaesu_encoder_down
   },                                           // Encoder Down
   {
      "EK", 0, 1, rr_cat_yaesu_enter
   },                                     // Enter key
   {
      "EU", 0, 1, rr_cat_yaesu_encoder_up
   },                                        // Encoder Up
   {
      "EX", 1, 1, rr_cat_yaesu_menu
   },                                  // Menu
   {
      "FA", 1, 1, rr_cat_yaesu_set_vfo_a
   },                                        // Set VFO A freq
   {
      "FB", 1, 1, rr_cat_yaesu_set_vfo_b
   },                                        // Set VFO B freq
   {
      "FS", 0, 1, rr_cat_yaesu_fast_step
   },                                        // Fast Step
   {
      "GT", 0, 1, rr_cat_yaesu_agc_func
   },                                        // AGC Function
   {
      "FT", 0, 1, rr_cat_yaesu_tx_vfo
   },                                     // Transmit VFO
   {
      "ID", 0, 1, rr_cat_yaesu_id
   },                                  // IDentification
   {
      "IF", 0, 0, rr_cat_yaesu_info
   },                                  // Information
   {
      "IS", 1, 1, rr_cat_yaesu_if_shift
   },                                        // IF shift
   {
      "KY", 1, 1, rr_cat_yaesu_cw_key
   },                                     // CW keying
   {
      "LK", 1, 1, rr_cat_yaesu_lock
   },                                  // Lock
   {
      "MA", 1, 1, rr_cat_yaesu_mem_to_vfo_a
   },                                           // Memory to VFO A
   {
      "MC", 1, 1, rr_cat_yaesu_mem_channel
   },                                           // Memory Channel
   {
      "MD", 1, 1, rr_cat_yaesu_mode
   },                                  // Mode
   {
      "MG", 0, 1, rr_cat_yaesu_mic_gain
   },                                        // Mic Gain
   {
      "ML", 0, 1, rr_cat_yaesu_monitor_level
   },                                           // Monitor Level
   {
      "MR", 0, 1, rr_cat_yaesu_memory_read
   },                                           // Memory Read
   {
      "MS", 0, 1, rr_cat_yaesu_meter_switch
   },                                           // Meter Switch
   {
      "MT", 0, 1, rr_cat_yaesu_mem_write_tag
   },                                           // Memory Write and Tag
   {
      "MW", 0, 1, rr_cat_yaesu_mem_write
   },                                        // Memory Write
   {
      "MX", 0, 1, rr_cat_yaesu_mox
   },                                  // MOX
   {
      "NA", 0, 1, rr_cat_yaesu_narrow
   },                                     // Narrow
   {
      "NB", 0, 1, rr_cat_yaesu_noise_blanker
   },                                           // Noise Blanker
   {
      "NL", 0, 1, rr_cat_yaesu_nb_level
   },                                        // NB Level
   {
      "NR", 1, 1, rr_cat_yaesu_noise_reduction
   },                                              // Noise Reduction
   {
      "OI", 0, 1, rr_cat_yaesu_obi
   },                                  // Other Band Info?
   {
      "OS", 0, 1, rr_cat_yaesu_offset
   },                                     // Offset
   {
      "PA", 0, 1, rr_cat_yaesu_preamp
   },                                     // Preamp
   {
      "PB", 0, 1, rr_cat_yaesu_playback
   },                                        // Playback
   {
      "PC", 1, 1, rr_cat_yaesu_tx_power
   },                                        // TX power level
   {
      "PL", 0, 1, rr_cat_yaesu_preproc_level
   },                                           // Speech preprocessor
   // level
   {
      "PR", 0, 1, rr_cat_yaesu_preproc
   },                                     // Speech preprocessor
   {
      "PS", 0, 0, rr_cat_yaesu_power
   },                                     // Power Switch
   {
      "QI", 0, 1, rr_cat_yaesu_qmb_store
   },                                        // QMB Store
   {
      "SH", 0, 1, rr_cat_yaesu_width
   },                                     // Width
   {
      "SM", 1, 1, rr_cat_yaesu_s_meter
   },                                     // S-Meter
   {
      "SQ", 1, 1, rr_cat_yaesu_squelch
   },                                     // Squelch
   {
      "ST", 0, 1, rr_cat_yaesu_split
   },                                     // Split
   {
      "SV", 0, 0, rr_cat_yaesu_swap_vfo
   },                                        // Swap VFO
   {
      "TS", 1, 1, rr_cat_yaesu_txw
   },                                  // TXW?
   {
      "TX", 0, 1, rr_cat_yaesu_ptt
   },                                  // TX Set (PTT)
   {
      "UP", 0, 0, rr_cat_yaesu_down
   },                                  // UP
   {
      "UL", 0, 1, rr_cat_yaesu_unlock
   },                                     // Unlock
   {
      "VG", 0, 1, rr_cat_yaesu_vox_gain
   },                                        // VOX Gain
   {
      "VM", 0, 1, rr_cat_yaesu_vfo_mem
   },                                     // VFO/Memory button
   {
      "VX", 0, 1, rr_cat_yaesu_vox
   },                                  // VOX
   {
      "KS", 0, 1, rr_cat_yaesu_key_speed
   },                                        // Keying Speed
   {
      "ZI", 0, 1, rr_cat_yaesu_zero_in
   },                                     // Zero in
   {
      NULL, -1, -1, NULL
   }                            // Terminator
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
