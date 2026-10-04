//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
struct fwdsp_recorder {
   pthread_t thread;
   pthread_mutex_t lock;
   pthread_cond_t cond;

   uint8_t *ring;
   size_t ring_size;
   size_t read_pos;
   size_t write_pos;
   size_t used;

   bool running;
   bool stopping;
   bool overflow;

   unsigned sample_rate;
   unsigned channels;
   unsigned bits_per_sample;

   char filename[PATH_MAX];
};
