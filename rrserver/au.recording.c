//
// rrserver/au.recording.c: Support for recording TX audio to a file
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Here we wrap around our supported audio interfaces
//
// Most of the ugly bits should go in the per-backend sources
//
#include "build_config.h"
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/au.h>

// How long should the random part of the filename be?
#define	RECORDING_ID_LEN 12

const char *cfg_path_record_dir = NULL;
int cfg_recording_max = 16;

static bool f_recdir_unset = false;
 
struct RecordingData **active_recordings;

static const char *rec_mkpath(const char *recording_id, int channel) {
   char *rv = NULL;

   if (!recording_id || channel < 0) {
      return NULL;
   }

   if (!cfg_path_record_dir) {
      // have we NOT warned the user yet?
      if (!f_recdir_unset) {
         Log(LOG_WARN, "au.record", "Please set path.record-dir in config to enable recording");
         f_recdir_unset = true;
      }

      // either way, we've failed, so return NULL....
      return NULL;
   }
   /// XXX: These states need to come from looking up the active fwdsp channels
   // (pipelines) we are maintaining
   bool is_tx = false;
   const char *codec = "*";
   char tmpbuf[PATH_MAX + 1];
   memset(tmpbuf, 0, PATH_MAX + 1);
   size_t tmp_len = snprintf(tmpbuf, sizeof(tmpbuf), "%s/%s.%s.%s", cfg_path_record_dir, recording_id, (is_tx ? "tx" : "rx"), codec);
   // free the returned value from cfg_get_exp (expanded variable)
   free( (char *)cfg_path_record_dir );
   cfg_path_record_dir = NULL;

   if (tmp_len > 0) {
      if ( !( rv = strdup(tmpbuf) ) ) {
         Log(LOG_CRIT, "au.record", "OOM in rec_mkpath");
         exit(EXIT_FAILURE);
      }
   }

   if (rv) {
      Log(LOG_DEBUG, "au.record", "New recording will be saved at %s", rv);
   }

   return rv;
}

// Returns the ID of of the new recording
const char *au_recording_start(int channel) {
   if (channel < 0) {
      return NULL;
   }
   char *recording_id = malloc(RECORDING_ID_LEN + 1);
   auth_generate_nonce( recording_id, sizeof(recording_id) );

   if (!cfg_path_record_dir) {
      cfg_path_record_dir = cfg_get_exp("path.record-dir");
      cfg_recording_max = cfg_get_int("record.max", 16);
   }

   const char *rec_file = rec_mkpath(recording_id, channel);

   if (!rec_file) {
      Log(LOG_CRIT, "au.record", "Failed to generate a random filename for recording. OOM?");

      return NULL;
   }
   // Open the recording file for writing
   FILE *fp = fopen(rec_file, "w");

   if (!fp) {
      Log(LOG_CRIT, "au.record", "Failed to open file %s for recording of channel %d", rec_file, channel);

      return NULL;
   }
   struct RecordingData *rd = malloc( sizeof(struct RecordingData) );

   if (!rd) {
      fprintf(stderr, "OOM in au_recording_start?!\n");
      fclose(fp);

      return NULL;
   }
   memset( rd, 0, sizeof(struct RecordingData) );
   rd->fp = fp;
   rd->rec_id = recording_id;

   // Store the fd somewhere (active_recordings array?)
   for (int i = 0 ; i < cfg_recording_max - 1 ; i++) {
      if (!active_recordings[i]) {
         active_recordings[i] = rd;
         break;
      }
   }

   return recording_id;
}

recording_data_t *au_recording_find(const char *id) {
   if (!id) {
      return NULL;
   }
   recording_data_t *rp = NULL;

   for (int i = 0 ; i < cfg_recording_max - 1 ; i++) {
      if ( (active_recordings[i]) && active_recordings[i]->rec_id == id ) {
         return active_recordings[i];
      }
   }

   return NULL;
}

bool au_recording_stop(const char *id) {
   if (!id) {
      return true;
   }
   recording_data_t *rp = au_recording_find(id);

   // Find the location of the recording struct (active_recordings array)
   // Close the fd
   if (rp->fp) {
      fclose(rp->fp);
   }
   free(rp);

   return false;
}

bool au_attach_gst(const char *id, int channel) {
   if (channel <= 0 || !id) {
      return true;
   }

   // XXX: Find the proper tee to connect to and use shmsink/source to pass data
   // across
   return false;
}

// XXX: Need to make a function that frees any allocated memory here for
// shutdown/module reload
