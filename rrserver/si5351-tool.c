// Si5351 bring-up utility; preview by default. Part of RustyRig; MIT license.
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rrserver/si5351.h>
static volatile sig_atomic_t stopping;
static void stop(int signal) {
   (void)signal;
   stopping = 1;
}
static void usage(FILE *out) {
   fputs("Usage: rr-si5351 [--bus /dev/i2c-N] [--address 0x60]\n"
      "  [--crystal-hz 25000000] [--correction-ppb 0] [--load-pf 10] [--drive-ma 2]\n"
      "  [--clk0 HZ] [--clk1 HZ] [--clk2 HZ] [--apply]\n"
      "Preview only by default. 0 disables an output; otherwise 8000..100000000 Hz.\n"
      "--apply claims the entire chip; Enter, EOF, SIGINT or SIGTERM disables it.\n", out);
}
static bool integer(const char *text, long long low, long long high, long long *value) {
   char *end;
   errno = 0;
   *value = strtoll(text, &end, 0);

   return *text && end != text && !*end && !errno && *value >= low && *value <= high;
}
static void dump(const char *name, const uint8_t *bytes) {
   printf("%s:", name);

   for (unsigned i = 0 ; i < 8 ; i++) {
      printf(" %02x", bytes[i]);
   }

   putchar('\n');
}
int main(int argc, char **argv) {
   static const struct option options[] = {
      {
         "bus", required_argument, NULL, 'b'
      }, {
         "address", required_argument, NULL, 'a'
      },
      {
         "crystal-hz", required_argument, NULL, 'x'
      }, {
         "correction-ppb", required_argument, NULL, 'c'
      },
      {
         "load-pf", required_argument, NULL, 'l'
      }, {
         "drive-ma", required_argument, NULL, 'd'
      },
      {
         "clk0", required_argument, NULL, '0'
      }, {
         "clk1", required_argument, NULL, '1'
      },
      {
         "clk2", required_argument, NULL, '2'
      }, {
         "apply", no_argument, NULL, 'A'
      },
      {
         "help", no_argument, NULL, 'h'
      }, {
         0
      }
   };
   rr_si5351_config_t config = {
      25000000, 0, 10, 2
   };
   uint32_t clocks[3] = {
      0
   };
   const char *bus = NULL;
   unsigned address = 0x60;
   bool apply = false;
   int option;
   while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
      long long value;

      if (option == 'h') {
         usage(stdout);

         return 0;
      }

      if (option == 'A') {
         apply = true;
         continue;
      }

      if (option == 'b') {
         bus = optarg;
         continue;
      }

      if (option == '?' || !integer(optarg, -100000, 100000000, &value)) {
         usage(stderr);

         return 2;
      }

      switch (option) {
         case 'a': {
            if (value < 8 || value > 0x77) {
               return 2;
            }
            address = value;
            break;
         }
         case 'x': {
            config.crystal_hz = value;
            break;
         }
         case 'c': {
            config.correction_ppb = value;
            break;
         }
         case 'l': {
            config.load_pf = value;
            break;
         }
         case 'd': {
            config.drive_ma = value;
            break;
         }
         default: {
            clocks[option - '0'] = value;
            break;
         }
      }
   }
   rr_si5351_plan_t plan;

   if (optind != argc || !rr_si5351_plan(&config, 10000000, &plan)) {
      usage(stderr);

      return 2;
   }
   dump("PLLA registers 26..33", plan.pll);

   for (unsigned i = 0 ; i < 3 ; i++) {
      if (!clocks[i]) {
         printf("CLK%u: disabled\n", i);
         continue;
      }

      if (!rr_si5351_plan(&config, clocks[i], &plan)) {
         fprintf(stderr, "CLK%u: invalid frequency\n", i);

         return 2;
      }
      printf("CLK%u: %u Hz, R=%u, control=%02x\n", i, clocks[i], plan.r_divider, plan.control);
      dump("MultiSynth", plan.multisynth);
   }

   if (!apply) {
      puts("Preview only; no hardware accessed.");

      return 0;
   }

   if (!bus || !*bus) {
      fputs("--apply requires --bus\n", stderr);

      return 2;
   }
   struct sigaction action = {
      .sa_handler = stop
   };
   sigemptyset(&action.sa_mask);
   sigaction(SIGINT, &action, NULL);
   sigaction(SIGTERM, &action, NULL);
   rr_hardware_t *registry = rr_hardware_new();
   bool ok = rr_si5351_register(registry, "si5351:test", bus, address, &config);
   rr_hardware_device_t *device = rr_hardware_find(registry, "si5351:test");

   if (ok) {
      ok = rr_hardware_claim(device, "bringup");
   }

   for (unsigned i = 0 ; ok && !stopping && i < 3 ; i++) {
      ok = rr_si5351_tune(device, i, clocks[i]);
   }

   if (ok && !stopping) {
      puts("Clocks applied. Press Enter to disable all outputs and exit.");
      fflush(stdout);
      int ch;
      while (!stopping && (ch = getchar()) != '\n' && ch != EOF) {
      }
   }

   if (!ok) {
      fprintf(stderr, "Si5351: %s\n", strerror(errno));
   }

   if (device && *rr_hardware_owner(device) && !rr_hardware_release(device, "bringup")) {
      fprintf(stderr, "Cannot confirm clocks disabled: %s\n", strerror(errno));
      ok = false;
   }
   rr_hardware_free(registry);

   return ok ? 0 : 1;
}
