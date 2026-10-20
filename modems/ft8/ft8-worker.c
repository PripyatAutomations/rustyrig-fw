// Receive-only FT8 worker for RustyRig. No TX, rig control or GUI required.
// Copyright RustyRig contributors, MIT license when built without Mongoose.
// External dependency: Karlis Goba's MIT licensed ft8_lib.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <common/monitor.h>
#include <ft8/decode.h>
#include <ft8/message.h>

#define RADIO_PCM_FD 4
#define RADIO_PCM_RATE 16000
#define FT8_PCM_RATE 12000
#define SLOT_SECONDS 15
#define MAX_CANDIDATES 140
#define MAX_DECODES 50

static bool raw_output;
static char command[64];
static size_t command_used;

static bool hash_lookup(ftx_callsign_hash_type_t type, uint32_t hash, char *call) {
   (void)type;
   (void)hash;
   call[0] = 0;                 // Unknown hashed calls stay unresolved until a hash cache exists.

   return false;
}
static void hash_save(const char *call, uint32_t hash) {
   (void)call;
   (void)hash;
}

static ftx_callsign_hash_interface_t hash_interface = {
   .lookup_hash = hash_lookup, .save_hash = hash_save
};

/* Escape protocol content: always emit precisely one JSON object per line. */
static void json_string(const char *value) {
   putchar('"');

   for (const unsigned char *p = (const unsigned char *)value ; *p ; p++) {
      if (*p == '"' || *p == '\\') {
         putchar('\\');
         putchar(*p);
      } else if (*p < 32) {
         printf("\\u%04x", *p);
      } else {
         putchar(*p);
      }
   }

   putchar('"');
}
static void emit_decode(time_t slot, float audio_hz, float dt, int score, const char *message) {
   if (raw_output) {
      puts(message);
   } else {
      printf("{\"type\":\"ft8.decode\",\"slot\":%lld," "\"audio_hz\":%.1f,\"dt\":%.2f,\"sync_score\":%d,\"text\":", (long long)slot, audio_hz, dt, score);
      json_string(message);
      puts("}");
   }
   fflush(stdout);
}
static void decode_slot(const monitor_t *monitor, time_t slot) {
   const ftx_waterfall_t *wf = &monitor->wf;
   ftx_candidate_t candidates[MAX_CANDIDATES];
   int count = ftx_find_candidates(wf, MAX_CANDIDATES, candidates, 10);
   uint16_t seen[MAX_DECODES];
   unsigned nseen = 0;

   for (int i = 0 ; i < count ; i++) {
      const ftx_candidate_t *candidate = &candidates[i];
      ftx_message_t message;
      ftx_decode_status_t status;

      if (!ftx_decode_candidate(wf, candidate, 25, &message, &status)) {
         continue;
      }
      bool duplicate = false;

      for (unsigned k = 0 ; k < nseen ; k++) {
         if (seen[k] == message.hash) {
            duplicate = true;
         }
      }

      if (duplicate) {
         continue;
      }

      if (nseen < MAX_DECODES) {
         seen[nseen++] = message.hash;
      }

      char decoded[FTX_MAX_MESSAGE_LENGTH];
      ftx_message_offsets_t offsets;

      if (ftx_message_decode(&message, &hash_interface, decoded, &offsets) != FTX_MESSAGE_RC_OK) {
         continue;
      }

      float hz = (monitor->min_bin + candidate->freq_offset + (float)candidate->freq_sub / wf->freq_osr) / monitor->symbol_period;
      float dt = (candidate->time_offset + (float)candidate->time_sub / wf->time_osr) * monitor->symbol_period;
      emit_decode(slot, hz, dt, candidate->score, decoded);
   }
}
static void handle_command(const char *line) {
   if (!strcmp(line, "RAW")) {
      raw_output = true;
   } else if (!strcmp(line, "JSON")) {
      raw_output = false;
   } else if (!strcmp(line, "HELP")) {
      fputs("RAW - plain decode lines; JSON - JSON lines (default); HELP\n", stderr);
   }
}
static void read_commands(void) {
   char input[128];
   ssize_t length = read(STDIN_FILENO, input, sizeof(input));

   if (length <= 0) {
      return;
   }

   for (ssize_t i = 0 ; i < length ; i++) {
      if (input[i] == '\n' || input[i] == '\r') {
         command[command_used] = 0;
         handle_command(command);
         command_used = 0;
      } else if (command_used < sizeof(command) - 1) {
         command[command_used++] = input[i];
      } else {
         command_used = 0;
      }
   }
}
int main(void) {
   // The server currently feeds raw mono S16LE at 16 kHz to FD 4.
   // The FT8 monitor runs at 12 kHz. Rational 3:4 resampling is kept
   // local until negotiated PCM sources become available.
   monitor_t monitor;
   monitor_config_t config = {
      .f_min = 200, .f_max = 3000, .sample_rate = FT8_PCM_RATE,
      .time_osr = 2, .freq_osr = 2, .protocol = FTX_PROTOCOL_FT8
   };
   monitor_init(&monitor, &config);
   float *block = calloc((size_t)monitor.block_size, sizeof(float));

   if (!block) {
      monitor_free(&monitor);

      return 1;
   }
   size_t used = 0;
   unsigned phase = 0;
   float previous = 0;
   uint8_t partial = 0;
   bool have_partial = false;
   time_t slot = -1;
   struct pollfd fds[2] = {
      {
         .fd = RADIO_PCM_FD, .events = POLLIN
      },
      {
         .fd = STDIN_FILENO, .events = POLLIN
      }
   };

   for ( ;; ) {
      int ready = poll(fds, 2, 250);

      if (ready < 0) {
         if (errno == EINTR) {
            continue;
         }
         break;
      }

      if (fds[1].revents & POLLIN) {
         read_commands();
      }

      if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
         break;
      }

      if (!(fds[0].revents & POLLIN)) {
         continue;
      }
      uint8_t bytes[4096];
      ssize_t got = read(RADIO_PCM_FD, bytes, sizeof(bytes));

      if (got <= 0) {
         break;
      }
      time_t now = time(NULL);
      time_t this_slot = now - now % SLOT_SECONDS;

      if (slot != this_slot) {
         if (slot >= 0 && used) {
            decode_slot(&monitor, slot);
         }
         monitor_reset(&monitor);
         used = 0;
         phase = 0;
         slot = this_slot;
      }

      for (ssize_t i = 0 ; i < got ; i++) {
         if (!have_partial) {
            partial = bytes[i];
            have_partial = true;
            continue;
         }
         int16_t value = (int16_t) ((uint16_t) partial | ((uint16_t) bytes[i] << 8));
         have_partial = false;
         float sample = value / 32768.0f;
         // At input sample j, produce at output times j, j+4/3, j+8/3.
         // Linear interpolation is a bring-up resampler; proper band-limited
         // resampling should replace it through source PCM negotiation.
         phase += 3;
         while (phase >= 4) {
            phase -= 4;
            block[used++] = (3.0f * sample + previous) * 0.25f;

            if (used == (size_t)monitor.block_size) {
               monitor_process(&monitor, block);
               used = 0;
            }
         }
         previous = sample;
      }
   }

   free(block);
   monitor_free(&monitor);

   return 0;
}
