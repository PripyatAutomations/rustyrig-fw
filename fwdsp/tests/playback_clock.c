// Exercise the actual framed-input path with a deterministic pipeline clock.
#define main fwdsp_program_main
#include <fwdsp/fwdsp.c>
#undef main
#include <assert.h>
#include <gst/check/gsttestclock.h>

static GstBuffer *push_pcm(struct fwdsp_frame_reader *reader, GstAppSrc *src, GstAppSink *sink, uint32_t flags) {
   uint8_t frame[644] = {0};
   frame_length_encode(frame, 640 | flags);
   assert(frame_reader_append(reader, frame, sizeof(frame)));
   assert(frame_reader_push_appsrc(reader, src));
   GstSample *sample = gst_app_sink_try_pull_sample(sink, GST_SECOND);
   assert(sample);
   GstBuffer *buffer = gst_buffer_ref(gst_sample_get_buffer(sample));
   gst_sample_unref(sample);
   assert(gst_buffer_get_size(buffer) == 640);
   assert(GST_BUFFER_DURATION(buffer) == 20 * GST_MSECOND);
   return buffer;
}

int main(int argc, char **argv) {
   gst_init(&argc, &argv);
   GstClock *clock = gst_test_clock_new_with_start_time(30 * GST_SECOND);
   GstElement *test_pipeline = gst_parse_launch(
      "appsrc name=src is-live=true format=time do-timestamp=true "
      "caps=audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved "
      "! appsink name=sink sync=false async=false", NULL);
   assert(test_pipeline);
   gst_pipeline_use_clock(GST_PIPELINE(test_pipeline), clock);
   assert(gst_element_set_state(test_pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
   assert(gst_element_get_state(test_pipeline, NULL, NULL, GST_SECOND) == GST_STATE_CHANGE_SUCCESS);
   GstAppSrc *src = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(test_pipeline), "src"));
   GstAppSink *sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(test_pipeline), "sink"));
   struct fwdsp_frame_reader reader = {0};
   processor_mode = true;
   processor_io_mode = PCM_PROCESSOR_MODE_PLAYBACK;

   // A persistent speaker may start long before the first subscription.
   gst_test_clock_advance_time(GST_TEST_CLOCK(clock), 5 * GST_SECOND);
   GstBuffer *buffer = push_pcm(&reader, src, sink, 0);
   assert(GST_BUFFER_PTS(buffer) == 5 * GST_SECOND + 75 * GST_MSECOND);
   assert(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT));
   gst_buffer_unref(buffer);

   // Packet bursts and ordinary jitter retain a continuous sample timeline.
   for (int i = 0; i < 30; i++) {
      GstClockTime expected = reader.next_pts;
      gst_test_clock_advance_time(GST_TEST_CLOCK(clock), (i % 2 ? 30 : 10) * GST_MSECOND);
      buffer = push_pcm(&reader, src, sink, 0);
      assert(GST_BUFFER_PTS(buffer) == expected);
      assert(!GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT));
      gst_buffer_unref(buffer);
   }

   // Codec-switch gaps must not leave every subsequent buffer in the past.
   for (int i = 0; i < 20; i++) {
      gst_test_clock_advance_time(GST_TEST_CLOCK(clock), GST_SECOND);
      GstClockTime running = gst_element_get_current_running_time(GST_ELEMENT(src));
      buffer = push_pcm(&reader, src, sink, 0);
      assert(GST_BUFFER_PTS(buffer) == running + 75 * GST_MSECOND);
      assert(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT));
      gst_buffer_unref(buffer);
   }
   buffer = push_pcm(&reader, src, sink, FWDSP_FRAME_DISCONT);
   assert(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT));
   gst_buffer_unref(buffer);
   frame_reader_clear(&reader);

   // PCM transforms/codec inputs keep sample-relative timestamps.
   processor_io_mode = PCM_PROCESSOR_MODE_BIDIRECTIONAL;
   reader.next_pts = 0;
   buffer = push_pcm(&reader, src, sink, 0);
   assert(GST_BUFFER_PTS(buffer) == 0);
   gst_buffer_unref(buffer);
   frame_reader_clear(&reader);
   gst_element_set_state(test_pipeline, GST_STATE_NULL);
   gst_object_unref(src);
   gst_object_unref(sink);
   gst_object_unref(test_pipeline);
   gst_object_unref(clock);
   puts("PASS: playback clock recovery after startup, jitter and repeated codec gaps");
   return 0;
}
