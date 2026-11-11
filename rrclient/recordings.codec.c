// Native FLAC and Ogg/Vorbis recording codec workers. GPL-2.0-only.
#include "recordings.codec.h"
#include <FLAC/stream_encoder.h>
#include <FLAC/stream_decoder.h>
#include <FLAC/metadata.h>
#include <vorbis/vorbisenc.h>
#include <vorbis/vorbisfile.h>
#include <ogg/ogg.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RECORD_RATE 16000
#define RECORD_CHANNELS 2
#define READ_BLOCK 4096

static const struct { const char *name; size_t offset; } tag_names[] = {
   {"TITLE", offsetof(rr_recording_tags, title)},
   {"ARTIST", offsetof(rr_recording_tags, user)},
   {"START_TIME", offsetof(rr_recording_tags, start_time)},
   {"SITE", offsetof(rr_recording_tags, site)},
   {"SERVER", offsetof(rr_recording_tags, server)},
   {"RIG", offsetof(rr_recording_tags, rig)},
   {"VFO", offsetof(rr_recording_tags, vfo)},
   {"FREQUENCY_HZ", offsetof(rr_recording_tags, frequency)},
   {"MODE", offsetof(rr_recording_tags, mode)},
   {"GRID_SQUARE", offsetof(rr_recording_tags, grid)},
   {"GPS_LATITUDE", offsetof(rr_recording_tags, latitude)},
   {"GPS_LONGITUDE", offsetof(rr_recording_tags, longitude)},
   {"LOCATION_SOURCE", offsetof(rr_recording_tags, location_source)},
   {"SOURCES", offsetof(rr_recording_tags, sources)},
   {"USER", offsetof(rr_recording_tags, user)}
};

static const char *tag_value(const rr_recording_tags *tags, size_t offset) {
   return *(const char *const *)((const char *)tags + offset);
}

struct rr_recording_encoder {
   FLAC__StreamEncoder *flac;
   FLAC__StreamMetadata *flac_comments;
   FILE *ogg_file;
   ogg_stream_state ogg;
   vorbis_info info;
   vorbis_dsp_state dsp;
   vorbis_block block;
   gboolean ogg_stream_ok, info_ok, dsp_ok, block_ok, flac_ok, failed;
};

static gboolean ogg_pages(rr_recording_encoder *e, gboolean flush) {
   ogg_page page;
   int rc;
   while ((rc = flush ? ogg_stream_flush(&e->ogg, &page) :
                        ogg_stream_pageout(&e->ogg, &page)) > 0) {
      if (fwrite(page.header, 1, (size_t)page.header_len, e->ogg_file) != (size_t)page.header_len ||
          fwrite(page.body, 1, (size_t)page.body_len, e->ogg_file) != (size_t)page.body_len)
         return FALSE;
   }
   return rc == 0;
}

static gboolean ogg_drain(rr_recording_encoder *e, gboolean flush) {
   while (vorbis_analysis_blockout(&e->dsp, &e->block) == 1) {
      if (vorbis_analysis(&e->block, NULL) || vorbis_bitrate_addblock(&e->block))
         return FALSE;
      ogg_packet packet;
      while (vorbis_bitrate_flushpacket(&e->dsp, &packet)) {
         if (ogg_stream_packetin(&e->ogg, &packet) || !ogg_pages(e, FALSE)) return FALSE;
      }
   }
   return !flush || ogg_pages(e, TRUE);
}

static gboolean vorbis_open(rr_recording_encoder *e, const char *path,
                            const rr_recording_tags *tags, const char *id) {
   e->ogg_file = g_fopen(path, "wb");
   if (!e->ogg_file) return FALSE;
   vorbis_info_init(&e->info); e->info_ok = TRUE;
   if (vorbis_encode_init_vbr(&e->info, 2, RECORD_RATE, 0.4f)) return FALSE;
   if (vorbis_analysis_init(&e->dsp, &e->info)) return FALSE;
   e->dsp_ok = TRUE;
   if (vorbis_block_init(&e->dsp, &e->block)) return FALSE;
   e->block_ok = TRUE;
   if (ogg_stream_init(&e->ogg, (int)g_random_int())) return FALSE;
   e->ogg_stream_ok = TRUE;
   vorbis_comment vc;
   vorbis_comment_init(&vc);
   vorbis_comment_add_tag(&vc, "ENCODER", "RustyRig");
   vorbis_comment_add_tag(&vc, "RECORDING_ID", (char *)id);
   vorbis_comment_add_tag(&vc, "COMMENT", "Recorded by RustyRig client from stereo monitor PCM");
   for (size_t i = 0; i < G_N_ELEMENTS(tag_names); ++i) {
      const char *value = tag_value(tags, tag_names[i].offset);
      if (value && *value) vorbis_comment_add_tag(&vc, (char *)tag_names[i].name, (char *)value);
   }
   ogg_packet header, comment, code;
   gboolean ok = !vorbis_analysis_headerout(&e->dsp, &vc, &header, &comment, &code) &&
      !ogg_stream_packetin(&e->ogg, &header) &&
      !ogg_stream_packetin(&e->ogg, &comment) &&
      !ogg_stream_packetin(&e->ogg, &code) && ogg_pages(e, TRUE);
   vorbis_comment_clear(&vc);
   return ok;
}

static gboolean flac_open(rr_recording_encoder *e, const char *path,
                          const rr_recording_tags *tags, const char *id) {
   e->flac = FLAC__stream_encoder_new();
   if (!e->flac) return FALSE;
   if (!FLAC__stream_encoder_set_channels(e->flac, 2) ||
       !FLAC__stream_encoder_set_bits_per_sample(e->flac, 16) ||
       !FLAC__stream_encoder_set_sample_rate(e->flac, RECORD_RATE) ||
       !FLAC__stream_encoder_set_compression_level(e->flac, 5))
      return FALSE;
   e->flac_comments = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
   if (!e->flac_comments) return FALSE;
   FLAC__StreamMetadata_VorbisComment_Entry entry;
   if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(
       &entry, "RECORDING_ID", id) ||
       !FLAC__metadata_object_vorbiscomment_append_comment(e->flac_comments, entry, false))
      return FALSE;
   for (size_t i = 0; i < G_N_ELEMENTS(tag_names); ++i) {
      const char *value = tag_value(tags, tag_names[i].offset);
      if (!value || !*value) continue;
      if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(
          &entry, tag_names[i].name, value) ||
          !FLAC__metadata_object_vorbiscomment_append_comment(e->flac_comments, entry, false))
         return FALSE;
   }
   FLAC__StreamMetadata *metadata[] = { e->flac_comments };
   if (!FLAC__stream_encoder_set_metadata(e->flac, metadata, 1)) return FALSE;
   e->flac_ok = FLAC__stream_encoder_init_file(e->flac, path, NULL, NULL) ==
      FLAC__STREAM_ENCODER_INIT_STATUS_OK;
   return e->flac_ok;
}

rr_recording_encoder *rr_recording_encoder_open(const char *ogg_path,
   const char *flac_path, const rr_recording_tags *tags, const char *id) {
   if ((!ogg_path && !flac_path) || !tags || !id) return NULL;
   rr_recording_encoder *e = g_new0(rr_recording_encoder, 1);
   if ((ogg_path && !vorbis_open(e, ogg_path, tags, id)) ||
       (flac_path && !flac_open(e, flac_path, tags, id))) {
      rr_recording_encoder_close(e);
      if (ogg_path) g_unlink(ogg_path);
      if (flac_path) g_unlink(flac_path);
      return NULL;
   }
   return e;
}

gboolean rr_recording_encoder_write(rr_recording_encoder *e, const int16_t *pcm, size_t frames) {
   if (!e || e->failed || !pcm) return FALSE;
   if (e->flac) {
      FLAC__int32 planar[READ_BLOCK * 2];
      size_t pos = 0;
      while (pos < frames) {
         size_t n = MIN(frames - pos, (size_t)READ_BLOCK);
         for (size_t i = 0; i < n * 2; ++i) planar[i] = pcm[pos * 2 + i];
         if (!FLAC__stream_encoder_process_interleaved(e->flac, planar, (unsigned)n))
            return !(e->failed = TRUE);
         pos += n;
      }
   }
   if (e->ogg_file) {
      size_t pos = 0;
      while (pos < frames) {
         int n = (int)MIN(frames - pos, (size_t)READ_BLOCK);
         float **buffer = vorbis_analysis_buffer(&e->dsp, n);
         if (!buffer) return !(e->failed = TRUE);
         for (int i = 0; i < n; i++) {
            buffer[0][i] = pcm[(pos + i) * 2] / 32768.0f;
            buffer[1][i] = pcm[(pos + i) * 2 + 1] / 32768.0f;
         }
         if (vorbis_analysis_wrote(&e->dsp, n) || !ogg_drain(e, FALSE))
            return !(e->failed = TRUE);
         pos += n;
      }
   }
   return TRUE;
}

gboolean rr_recording_encoder_close(rr_recording_encoder *e) {
   if (!e) return FALSE;
   gboolean success = !e->failed;
   if (e->ogg_file && e->dsp_ok && e->ogg_stream_ok && !e->failed) {
      if (vorbis_analysis_wrote(&e->dsp, 0) || !ogg_drain(e, TRUE)) success = FALSE;
   }
   if (e->flac) {
      if (e->flac_ok && !FLAC__stream_encoder_finish(e->flac)) success = FALSE;
      FLAC__stream_encoder_delete(e->flac);
   }
   if (e->flac_comments) FLAC__metadata_object_delete(e->flac_comments);
   if (e->ogg_stream_ok) ogg_stream_clear(&e->ogg);
   if (e->block_ok) vorbis_block_clear(&e->block);
   if (e->dsp_ok) vorbis_dsp_clear(&e->dsp);
   if (e->info_ok) vorbis_info_clear(&e->info);
   if (e->ogg_file && fclose(e->ogg_file)) success = FALSE;
   g_free(e);
   return success;
}

struct rr_recording_decoder {
   gboolean flac;
   OggVorbis_File ogg;
   gboolean ogg_ok;
   FLAC__StreamDecoder *fd;
   gboolean fd_ok, error, eof;
   guint sample_rate;
   guint channels;
   guint64 total;
   /* Native source frames, consumed through a streaming resampler. */
   gint16 *chunk;
   size_t chunk_len, chunk_pos, chunk_capacity;
   guint64 source_pos;
   double fractional;
   gint16 left[2], right[2];
   gboolean have_left, have_right, exhausted;
};

static FLAC__StreamDecoderWriteStatus flac_write_cb(const FLAC__StreamDecoder *fd,
   const FLAC__Frame *frame, const FLAC__int32 *const samples[], void *userdata) {
   (void)fd;
   rr_recording_decoder *d = userdata;
   size_t n = frame->header.blocksize;
   guint channels = frame->header.channels;
   guint bits = frame->header.bits_per_sample;
   if (!n || !channels || bits < 8 || bits > 32) {
      d->error = TRUE;
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
   }
   if (n > d->chunk_capacity) {
      d->chunk = g_realloc_n(d->chunk, n, sizeof(gint16) * 2);
      d->chunk_capacity = n;
   }
   unsigned shift = bits > 16 ? bits - 16 : 0;
   for (size_t i = 0; i < n; ++i) {
      for (int c = 0; c < 2; ++c) {
         FLAC__int32 v = samples[MIN((unsigned)c, channels - 1)][i];
         if (shift) v >>= shift;
         else if (bits < 16) v <<= 16 - bits;
         d->chunk[i * 2 + c] = (gint16)CLAMP(v, -32768, 32767);
      }
   }
   d->chunk_len = n;
   d->chunk_pos = 0;
   return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
static void flac_metadata_cb(const FLAC__StreamDecoder *fd,
   const FLAC__StreamMetadata *metadata, void *userdata) {
   (void)fd;
   rr_recording_decoder *d = userdata;
   if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
      d->sample_rate = metadata->data.stream_info.sample_rate;
      d->channels = metadata->data.stream_info.channels;
      d->total = metadata->data.stream_info.total_samples;
   }
}
static void flac_error_cb(const FLAC__StreamDecoder *fd,
   FLAC__StreamDecoderErrorStatus status, void *userdata) {
   (void)fd; (void)status;
   ((rr_recording_decoder *)userdata)->error = TRUE;
}

rr_recording_decoder *rr_recording_decoder_open(const char *path) {
   if (!path) return NULL;
   rr_recording_decoder *d = g_new0(rr_recording_decoder, 1);
   if (g_str_has_suffix(path, ".flac")) {
      d->flac = TRUE;
      d->fd = FLAC__stream_decoder_new();
      if (!d->fd ||
          FLAC__stream_decoder_init_file(d->fd, path, flac_write_cb,
             flac_metadata_cb, flac_error_cb, d) != FLAC__STREAM_DECODER_INIT_STATUS_OK ||
          !FLAC__stream_decoder_process_until_end_of_metadata(d->fd)) {
         rr_recording_decoder_close(d); return NULL;
      }
      d->fd_ok = TRUE;
   } else if (g_str_has_suffix(path, ".ogg")) {
      FILE *file = g_fopen(path, "rb");
      if (!file) { g_free(d); return NULL; }
      if (ov_open(file, &d->ogg, NULL, 0) < 0) {
         fclose(file); g_free(d); return NULL;
      }
      d->ogg_ok = TRUE;
      vorbis_info *info = ov_info(&d->ogg, -1);
      if (info) {
         d->sample_rate = (unsigned)info->rate;
         d->channels = (unsigned)info->channels;
      }
      ogg_int64_t n = ov_pcm_total(&d->ogg, -1);
      d->total = n > 0 ? (guint64)n : 0;
   } else {
      g_free(d); return NULL;
   }
   if (!d->sample_rate || !d->channels) {
      rr_recording_decoder_close(d); return NULL;
   }
   return d;
}

static gboolean decoder_next(rr_recording_decoder *d, gint16 stereo[2]) {
   if (d->chunk_pos == d->chunk_len) {
      d->chunk_pos = d->chunk_len = 0;
      if (d->flac) {
         while (!d->chunk_len && !d->error &&
                FLAC__stream_decoder_get_state(d->fd) != FLAC__STREAM_DECODER_END_OF_STREAM) {
            if (!FLAC__stream_decoder_process_single(d->fd)) { d->error = TRUE; break; }
         }
      } else {
         float **pcm = NULL;
         int bitstream = 0;
         long n = ov_read_float(&d->ogg, &pcm, READ_BLOCK, &bitstream);
         if (n > 0) {
            if ((size_t)n > d->chunk_capacity) {
               d->chunk = g_realloc_n(d->chunk, n, sizeof(gint16) * 2);
               d->chunk_capacity = (size_t)n;
            }
            for (long i = 0; i < n; ++i)
               for (int c = 0; c < 2; ++c) {
                  float x = pcm[MIN(c, d->channels - 1)][i];
                  d->chunk[i * 2 + c] = (gint16)CLAMP((int)lrintf(x * 32768.0f), -32768, 32767);
               }
            d->chunk_len = (size_t)n;
         } else if (n < 0) d->error = TRUE;
      }
   }
   if (d->chunk_pos >= d->chunk_len) return FALSE;
   stereo[0] = d->chunk[d->chunk_pos * 2];
   stereo[1] = d->chunk[d->chunk_pos * 2 + 1];
   d->chunk_pos++;
   d->source_pos++;
   return TRUE;
}

gboolean rr_recording_decoder_seek(rr_recording_decoder *d, double seconds) {
   if (!d || !isfinite(seconds)) return FALSE;
   double sample = MAX(0.0, seconds) * d->sample_rate;
   guint64 frame = sample >= (double)d->total && d->total ? d->total : (guint64)sample;
   d->chunk_len = d->chunk_pos = 0;
   d->error = FALSE;
   d->have_left = d->have_right = d->exhausted = FALSE;
   d->fractional = sample - floor(sample);
   d->source_pos = frame;
   if (d->flac) {
      if (frame >= d->total && d->total) return TRUE;
      return FLAC__stream_decoder_seek_absolute(d->fd, frame);
   }
   return ov_pcm_seek(&d->ogg, (ogg_int64_t)frame) == 0;
}

gssize rr_recording_decoder_read(rr_recording_decoder *d, int16_t *pcm, size_t frames) {
   if (!d || !pcm) return -1;
   size_t done = 0;
   double step = (double)d->sample_rate / RECORD_RATE;
   while (done < frames) {
      if (!d->have_left) {
         if (!decoder_next(d, d->left)) break;
         d->have_left = TRUE;
      }
      if (!d->have_right) {
         if (!decoder_next(d, d->right)) {
            if (d->exhausted) break;
            d->exhausted = TRUE;
            d->right[0] = d->left[0];
            d->right[1] = d->left[1];
         }
         d->have_right = TRUE;
      }
      for (int c = 0; c < 2; ++c) {
         double value = d->left[c] + (d->right[c] - d->left[c]) * d->fractional;
         pcm[done * 2 + c] = (gint16)CLAMP((int)lrint(value), -32768, 32767);
      }
      done++;
      d->fractional += step;
      while (d->fractional >= 1.0) {
         d->fractional -= 1.0;
         d->left[0] = d->right[0];
         d->left[1] = d->right[1];
         if (d->exhausted) {
            /* No source frame beyond this one; stop, don't repeat EOF. */
            d->have_left = d->have_right = FALSE;
            break;
         }
         if (!decoder_next(d, d->right)) {
            d->exhausted = TRUE;
            d->right[0] = d->left[0];
            d->right[1] = d->left[1];
         }
         d->have_right = TRUE;
      }
   }
   return done ? (gssize)done : d->error ? -1 : 0;
}

double rr_recording_decoder_duration(rr_recording_decoder *d) {
   return d && d->sample_rate ? (double)d->total / d->sample_rate : 0.0;
}
void rr_recording_decoder_close(rr_recording_decoder *d) {
   if (!d) return;
   if (d->fd) {
      if (d->fd_ok) FLAC__stream_decoder_finish(d->fd);
      FLAC__stream_decoder_delete(d->fd);
   }
   if (d->ogg_ok) ov_clear(&d->ogg);
   g_free(d->chunk);
   g_free(d);
}
