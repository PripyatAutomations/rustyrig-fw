// Standalone native recorder codec round-trip regression. GPL-2.0-only.
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <vorbis/vorbisfile.h>
#include <FLAC/metadata.h>
#include <rrclient/recordings.codec.h>

static void roundtrip(const char *directory) {
   char *ogg = g_build_filename(directory, "recording.ogg", NULL);
   char *flac = g_build_filename(directory, "recording.flac", NULL);
   rr_recording_tags tags = {
      .title = "Native recording test",
      .user = "tester",
      .site = "sb50",
      .mode = "USB",
      .frequency = "14074000",
      .start_time = "2026-11-11T12:00:00Z"
   };
   rr_recording_encoder *e = rr_recording_encoder_open(ogg, flac, &tags, "test-id-123");
   assert(e);
   int16_t block[320 * 2];
   for (int n = 0; n < 50; ++n) {
      for (int i = 0; i < 320; ++i) {
         double t = (n * 320 + i) / 16000.0;
         block[i * 2] = (int16_t)(10000 * sin(t * 440 * 2 * G_PI));
         block[i * 2 + 1] = (int16_t)(7000 * sin(t * 880 * 2 * G_PI));
      }
      assert(rr_recording_encoder_write(e, block, 320));
   }
   assert(rr_recording_encoder_close(e));
   assert(g_file_test(ogg, G_FILE_TEST_EXISTS));
   assert(g_file_test(flac, G_FILE_TEST_EXISTS));

   for (int kind = 0; kind < 2; ++kind) {
      const char *path = kind ? flac : ogg;
      rr_recording_decoder *d = rr_recording_decoder_open(path);
      assert(d);
      assert(fabs(rr_recording_decoder_duration(d) - 1.0) < .03);
      int16_t pcm[2048 * 2];
      size_t total = 0;
      gssize got;
      while ((got = rr_recording_decoder_read(d, pcm, 2048)) > 0) {
         total += (size_t)got;
         assert(total < 17000);
      }
      assert(got == 0);
      assert(total >= 15500 && total <= 16001);
      assert(rr_recording_decoder_seek(d, .5));
      assert(rr_recording_decoder_read(d, pcm, 2048) > 0);
      rr_recording_decoder_close(d);
   }

   FILE *f = fopen(ogg, "rb");
   assert(f);
   OggVorbis_File ov;
   assert(ov_open(f, &ov, NULL, 0) == 0);
   vorbis_comment *comments = ov_comment(&ov, -1);
   assert(comments && vorbis_comment_query(comments, "RECORDING_ID", 0));
   assert(!strcmp(vorbis_comment_query(comments, "RECORDING_ID", 0), "test-id-123"));
   ov_clear(&ov);
   FLAC__Metadata_SimpleIterator *it = FLAC__metadata_simple_iterator_new();
   assert(it && FLAC__metadata_simple_iterator_init(it, flac, true, false));
   gboolean found = FALSE;
   do {
      if (FLAC__metadata_simple_iterator_get_block_type(it) != FLAC__METADATA_TYPE_VORBIS_COMMENT) continue;
      FLAC__StreamMetadata *md = FLAC__metadata_simple_iterator_get_block(it);
      assert(md);
      for (unsigned i = 0; i < md->data.vorbis_comment.num_comments; ++i) {
         FLAC__StreamMetadata_VorbisComment_Entry entry = md->data.vorbis_comment.comments[i];
         if (entry.length == strlen("RECORDING_ID=test-id-123") &&
             !memcmp(entry.entry, "RECORDING_ID=test-id-123", entry.length)) found = TRUE;
      }
      FLAC__metadata_object_delete(md);
   } while (FLAC__metadata_simple_iterator_next(it));
   assert(found);
   FLAC__metadata_simple_iterator_delete(it);
   g_unlink(ogg);
   g_unlink(flac);
   g_free(ogg);
   g_free(flac);
}
int main(int argc, char **argv) {
   assert(argc == 2);
   roundtrip(argv[1]);
   puts("PASS: native Ogg/Vorbis and FLAC stereo encode, decode, seek, metadata");
   return 0;
}
