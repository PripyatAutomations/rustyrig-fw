#!/usr/bin/perl
#
# Convert $profile.json to an eeprom.bin and appropriate headers needed to build
# the radio software, either natively or cross compiled.
# $profile defaults to 'radio' but can be provided as the first (and only) argument
###############################################################################
use strict;
use warnings;
use Getopt::Std;
use IO::Handle;
use JSON;
use Data::Dumper;
use MIME::Base64;
use Hash::Merge qw( merge );
use Mojo::JSON::Pointer;
use File::Path qw(make_path remove_tree);

Hash::Merge::set_behavior('RIGHT_PRECEDENT');
$Data::Dumper::Terse = 1;
$Data::Dumper::Indent = 1;       # mild pretty print
$Data::Dumper::Useqq = 1;        # print strings in double quotes
STDOUT->autoflush(1);

#############################
# Determine Project Version #
#############################
open(my $verfh, "<", ".version") or die("Missing .version!\n");
my $verdata = '';
while (<$verfh>) {
   $verdata = $_;
}
close($verfh);
my @ver = split(/\./, $verdata);
my $version = {
   "firmware" => {
      "major" => $ver[0],
      "minor" => $ver[1]
   }
};

############
# Defaults #
############
my $default_cfg = {
   "owner" => {
      "call" => "N0CALL",
      "privs" => "US/General"
   },
   "pin" => {
      "master" => "654321",
      "reset" => "654321"
   },
   "eeprom" => {
      "size" => 16384,
      "type" => "mmap",
      "addr" => 0x0000
   },
   "net" => {
      "ip" => "10.201.1.100",
      "gw" => "10.201.1.1",
      "mask" => "255.255.255.0",
      "dns1" => "10.201.1.1",
      "dns2" => "1.1.1.1",
   },
   "device" => {
      "serial" => "000000"
   }
};

###########
# Globals #
###########
my $build_dir = '';
my $config = { };
my $config_file = '';
my $cptr;
my $eeprom_file = '';
my $eeprom_data = '';
my $eeprom_layout = { };
my @eeprom_dd;
my $profile = 'radio';

# use profile name from command line, if given
if ($#ARGV == 0) {
   $profile = $ARGV[0];
}
$config_file = "$profile.json";
$build_dir = "build/$profile";
$eeprom_file = "$build_dir/eeprom.bin";

# Make the build directory tree, if not present
make_path("$build_dir/obj");

# Load configuration from $config_file
sub config_load {
   # Merge the version information into the default configuration
   my $base_cfg = merge($default_cfg, $version);

   print "* Load config from $_[0]\n";
   open(my $fh, '<', $_[0]) or warn("  * Couldn't open configuration file $_[0], using defaults!\n") and return;
   my $nbytes = 0;
   my $tmp = '';

   while (1) {
      my $res = read $fh, $tmp, 512, length($tmp);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($fh);

   $nbytes = length($tmp);
   print "  * Read $nbytes bytes from $_[0]\n";
   my $json = JSON->new;
   my $loaded_cfg = $json->decode($tmp) or warn("ERROR: Can't parse configuration $_[0]!\n") and return;

   # Merge defaults with loaded configuration
   $config = merge($base_cfg, $loaded_cfg);

   # apply mojo json pointer, so we can reference by path
   $cptr = Mojo::JSON::Pointer->new($config);
}

# Load eeprom_layout from eeprom_layout.json
sub eeprom_layout_load {
   print "* Load EEPROM layout definition from $_[0]\n";
   open(my $fh, '<', $_[0]) or die("ERROR: Couldn't open $_[0]!\n");
   my $nbytes = 0;
   my $tmp = '';

   while (1) {
      my $res = read $fh, $tmp, 512, length($tmp);
      die "$!\n" if not defined $res;
      last if not $res;
   }

   close($fh);
   $nbytes = length($tmp);
   print "  * Read $nbytes bytes from $_[0]\n";
   my $json = JSON->new;
   @eeprom_dd = $json->decode($tmp) or warn("ERROR: Can't parse $_[0]!\n") and return;
}

sub eeprom_load {
   my $eeprom_size = $cptr->get("/eeprom/size");
   print "* Load EEPROM data from $_[0]\n";
   open(my $EEPROM, '<:raw', $_[0]) or print "  * Not found, skipping\n" and return;
   my $nbytes = 0;

   while (1) {
      my $res = read $EEPROM, $eeprom_data, 100, length($eeprom_data);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($EEPROM);

   $nbytes = length($eeprom_data);

   if ($nbytes == $eeprom_size) {
      print "  * Read $nbytes bytes from $_[0]\n";
   } elsif ($nbytes == 0) {
      print "  * Read empty EEPROM from $_[0]... Fixing!\n";
      return;
   } else {
      die("ERROR: Expected $eeprom_size bytes but read $nbytes from $_[0], bailing to avoid damage...\n");
   }
}

sub eeprom_patch {
   my $changes = 0;
   my $errors = 0;
   my $warnings = 0;
   my $eeprom_size = $cptr->get("/eeprom/size");

   print "* Patch in-memory image\n";

   if ($eeprom_data eq "") {
      print "  * No EEPROM data, starting clean with $eeprom_size bytes empty image\n";
      $eeprom_data = "\x00" x $eeprom_size;
   }

   # Walk over the layout structure and see if we have a value set in the config...
   for my $item (@eeprom_dd) {
      for my $key (keys(%$item)) {
          my $ee_key = $item->{$key}{key};
          my $ee_offset = $item->{$key}{offset};
          my $ee_size = $item->{$key}{size};
          my $ee_type = $item->{$key}{type};
          my $eeprom_size = $cptr->get("/eeprom/size");

          if (defined($ee_key)) {
             $changes++;

             my $cval = $cptr->get("/$ee_key");
             my $ckey = $ee_key;
             $ckey =~ s/\//_/;
             if (defined($cval)) {
                # Validate the offset and size will fit within the rom
                if (($ee_offset >= 0 && $ee_offset < $eeprom_size) &&
                    ($ee_offset + $ee_size <= $eeprom_size)) {
                   printf "   * Patching %d bytes @ %04d: [%s] %s=%s\n", $ee_size, $ee_offset, $ee_type, $ee_key, $cval;
#                   print "  * Patching $ee_size bytes @ $ee_offset: [$ee_type] $ee_key=$cval\n";
                   if ($ee_type eq 'd') {
                      # numeric
                   } elsif ($ee_type eq 's') {
                      # string
                   } elsif ($ee_type eq 'b') {
                      # raw bytes
                   } elsif ($ee_type eq 'LicensePrivs') {
                      # license privileges
                   } elsif ($ee_type eq 'ip4') {
                      # ipv4 address
                   }
                } else {
                   print "Invalid offset/size ($ee_offset, $ee_size) - our EEPROM is $eeprom_size\n";
                }
             } else {
                print "    * No value at key $ee_key\n";
                $warnings++;
             }
          }
       }
   }

   if ($errors > 0) {
      print "*** There were $errors errors while processing $changes patche, aborting without change!\n";
      die();
   }

   print "  * Finished patching, there were $warnings warnings from $changes patches\n";
}

sub eeprom_save {
   my $nbytes = length($eeprom_data);
   my $eeprom_size = $cptr->get("/eeprom/size");

   if ($nbytes == 0) {
      die("ERROR: Refusing to write empty (0 byte) EEPROM to $_[0]\n");
   }

   if ($nbytes != $eeprom_size) {
      die("ERROR: Our EEPROM ($_[0]) is $eeprom_size bytes, but we ended up with $nbytes bytes of data. Bailing to avoid damage!\n");
   }

   print "* Save $nbytes bytes to $_[0]\n";
   open(my $EEPROM, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $EEPROM $eeprom_data;
   close $EEPROM;
   print "  * Wrote $nbytes bytes to $_[0]\n";
}

sub generate_eeprom_layout_h {
   my $file = $_[0];
   my $nbytes = 0;

   if ($file eq '') {
      die("generate_eeprom_layout_h: No argument given\n");
   }

   print "  * Generating $_[0]\n";
   open(my $fh, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $fh "#if     !defined(_eeprom_layout_h)\n#define _eeprom_layout_h\n";

   # XXX: Sort the list here, based on the offset

   my $eeprom_size = $cptr->get("/eeprom/size");
   # Walk over the layout structure and see if we have a value set in the config...
   for my $item (@eeprom_dd) {
      for my $key (keys(%$item)) {
          my $ee_key = $item->{$key}{key};
          my $ee_offset = $item->{$key}{offset};
          my $ee_size = $item->{$key}{size};
          my $ee_type = $item->{$key}{type};

          if (defined($ee_key)) {
             my $cval = $cptr->get("/$ee_key");
             if (defined($cval)) {
                my $fullkey = uc $ee_key;
                my $ckey = $fullkey;
                $ckey =~ s/\//_/;
                print $fh "#define ROM_OFFSET_${ckey} $ee_offset\n";
             }
          }
       }
   }
   print $fh "#endif\n";
   close $fh;
}

sub generate_config_h {
   my $file = $_[0];
   my $nbytes = 0;
   if ($file eq '') {
      die("generate_eeprom_layout_h: No argument given\n");
   }
   print "  * Generating $_[0]\n";
   open(my $fh, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $fh "// Auto-generated file. Please do not edit. Run buildconf.pl instead\n\n";
   print $fh "#if     !defined(_config_h)\n#define _config_h\n";
   printf $fh "#define VERSION \"%02d.%02d\"\n", $version->{firmware}{major}, $version->{firmware}{minor};
   printf $fh "#define VERSION_MAJOR 0x%x\n", $version->{firmware}{major};
   printf $fh "#define VERSION_MINOR 0x%x\n", $version->{firmware}{minor};

   if (defined($config->{i2c})) {
      if (defined($config->{i2c}{myaddr})) {
         printf $fh "#define MY_I2C_ADDR %s\n", $config->{i2c}{myaddr};
      }
   }

   if (!defined($config->{eeprom}{type}) || !defined($config->{eeprom}{addr})) {
      die("ERROR: Invalid configuration, edit $file and try again!\n");
   }

   if ($config->{eeprom}{type} eq "mmap") {
      print $fh "#define EEPROM_TYPE_MMAP true\n";
      print $fh "#undef EEPROM_TYPE_I2C\n";
      printf $fh "#define EEPROM_MMAP_ADDR %s\n", $config->{eeprom}{addr};
   } elsif ($config->{eeprom}{type} eq "i2c") {
      print $fh "#undef EEPROM_TYPE_MMAP\n";
      print $fh "#define EEPROM_TYPE_I2C\n";
      printf $fh "#define EEPROM_I2C_ADDR %s\n", $config->{eeprom}{addr};
   }
   if (defined($config->{features})) {
      if (defined($config->{features}{'cat-kpa500'}) && $config->{features}{'cat-kpa500'} eq 1) {
         printf $fh "#define CAT_KPA500 true\n";
      }
      if (defined($config->{features}{'cat-yaesu'}) && $config->{features}{'cat-yaesu'} eq 1) {
         printf $fh "#define CAT_YAESU true\n";
      }
   }
   if (!defined($config->{eeprom}{size})) {
      die("ERROR: eeprom.size not set!\n");
   }

   printf $fh "#define EEPROM_SIZE %d\n", $config->{eeprom}{size};

   my $max_bands = '';
   if (defined($config->{features}{'max-bands'})) {
      $max_bands = $config->{features}{'max-bands'};
   } else {
      $max_bands = 10;
   }
   printf $fh "#define MAX_BANDS %d\n", $max_bands;

   # XXX: Host mode stuff
   print $fh "#define HOST_EEPROM_FILE \"$eeprom_file\"\n";
   print $fh "#define HOST_LOG_FILE \"firmware.log\"\n";
   print $fh "#define HOST_CAT_PIPE \"cat.fifo\"\n";

   # footer
   print $fh "#endif\n";
   close $fh;
}

sub generate_headers {
   print "* Generating headers\n";
   generate_config_h($build_dir . "/build_config.h");
   generate_eeprom_layout_h($build_dir . "/eeprom_layout.h");
}

#############################################################
# Load the configuration
config_load($config_file);

# Load the EEPROM layout definition
eeprom_layout_load("res/eeprom_layout.json");

# Try loading the eeprom into memory
eeprom_load($eeprom_file);

# Apply loaded configuration to in-memory EEPROM image
eeprom_patch($config);

# Disable interrupt while saving
$SIG{INT} = 'IGNORE';

# Save the patched eeprom.bin
eeprom_save($eeprom_file);

# And generate headers for the C bits...
generate_headers();
