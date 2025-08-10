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

   const char *recdir = cfg_get("audio.record-dir");
   if (!recdir) {
      Log(LOG_CRAZY, "au.record", "Please set audio.record-dir in config to enable recording");
      return NULL;
   }

   /// XXX: These states need to come from looking up the active fwdsp channels (pipelines) we are maintaining
   bool is_tx = false;
   const char *codec = "*";
   char tmpbuf[512];
   memset(tmpbuf, 0, 512);
   size_t tmp_len = snprintf(tmpbuf, 512, "%s/%s.%s.%s", recdir, recording_id, (is_tx ? "tx" : "rx"), codec);
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

// Returns the ID of of the new recording
const char *au_recording_start(int channel) {
   char *recording_id = malloc(RECORDING_ID_LEN+1);
   generate_nonce(recording_id, sizeof(recording_id));

   const char *rec_file = au_recording_mkfilename(recording_id, channel);
   if (!rec_file) {
      Log(LOG_CRIT, "au.record", "Failed to generate a random filename for recording. OOM?");
   }
   return recording_id;
}

bool au_recording_stop(const char *id) {
   if (id == NULL) {
      return true;
   }

   free((void *)id);
   return false;
}
