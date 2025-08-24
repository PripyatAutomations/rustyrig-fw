//
// au.recording.c: Support for recording TX audio to a file
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
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
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include "rrserver/state.h"
#include "common/codecneg.h"
#include "common/logger.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrserver/au.h"
#include "rrserver/auth.h"

// How long should the random part of the filename be?
#define	RECORDING_ID_LEN	24

const char *au_recording_mkfilename(const char *recording_id, int channel) {
   char *rv = NULL;
   if (!recording_id || channel < 0) {
      return NULL;
   }

   const char *recdir = cfg_get_exp("path.record-dir");
   if (!recdir) {
      Log(LOG_CRAZY, "au.record", "Please set path.record-dir in config to enable recording");
      return NULL;
   }

   /// XXX: These states need to come from looking up the active fwdsp channels (pipelines) we are maintaining
   bool is_tx = false;
   const char *codec = "*";
   char tmpbuf[512];
   memset(tmpbuf, 0, 512);
   size_t tmp_len = snprintf(tmpbuf, 512, "%s/%s.%s.%s", recdir, recording_id, (is_tx ? "tx" : "rx"), codec);
   // free the returned value from cfg_get_exp (expanded variable)
   free((char *)recdir);

   if (tmp_len > 0) {
      if (!(rv = strdup(tmpbuf))) {
         Log(LOG_CRIT, "au.record", "OOM in au_recording_mkfilename");
         exit(1);
      }
   }
   if (rv) {
      Log(LOG_DEBUG, "au.record", "New recording will be saved at %s", rv);
   }

   return rv;
}

struct RecordingData {
   FILE *fp;
   const char *rec_id;
};
typedef struct RecordingData recording_data_t;

#define	MAX_RECORD_OPEN		16

struct RecordingData *active_recordings[MAX_RECORD_OPEN];

// Returns the ID of of the new recording
const char *au_recording_start(int channel) {
   char *recording_id = malloc(RECORDING_ID_LEN+1);
   generate_nonce(recording_id, sizeof(recording_id));

   const char *rec_file = au_recording_mkfilename(recording_id, channel);
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

   struct RecordingData *rd = malloc(sizeof(struct RecordingData));
   if (!rd) {
      fprintf(stderr, "OOM in au_recording_start?!\n");
      fclose(fp);
      return NULL;
   }

   memset(rd, 0, sizeof(struct RecordingData));
   rd->fp = fp;
   rd->rec_id = recording_id;

   // Store the fd somewhere (active_recordings array?)
   for (int i = 0; i < MAX_RECORD_OPEN - 1; i++) {
      if (active_recordings[i] == NULL) {
         active_recordings[i] = rd;
         break;
      }
   }
   return recording_id;
}


recording_data_t *au_recording_find(const char *id) {
   recording_data_t *rp = NULL;
   for (int i = 0; i < MAX_RECORD_OPEN - 1; i++) {
      if ((active_recordings[i] != NULL) && active_recordings[i]->rec_id == id) {
         return active_recordings[i];
      }
   }
   return NULL;
}

bool au_recording_stop(const char *id) {
   if (id == NULL) {
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

   // XXX: Find the proper tee to connect to and use shmsink/source to pass data across
   return false;
}

// XXX: Need to make a function that frees any allocated memory here for shutdown/module reload
