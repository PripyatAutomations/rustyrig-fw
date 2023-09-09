#!/usr/bin/perl
#
# Here we generate an eeprom.bin from the radio.json configuration file
#
# We also generate the build_config.h and eeprom_layouts.h needed for the host
#
# You should pass arguments to me, or i will use defaults...
use strict;
use warnings;
use Getopt::Std;
use IO::Handle;
use JSON;
use Data::Dumper;
use MIME::Base64;
use Hash::Merge qw( merge );
Hash::Merge::set_behavior('RIGHT_PRECEDENT');

# XXX: parse these out from .version
my $ver_major = 23;
my $ver_minor = 1;

STDOUT->autoflush(1);

my $eeprom_file = '';
my $config_file = '';
my $build_dir = '';

# use commandline args, if present [eeprom] [config]
if ($#ARGV >= 0) {
   $eeprom_file = $ARGV[0];

   if ($#ARGV >= 1) {
      $config_file = $ARGV[1];
   }
   if ($#ARGV >= 2) {
      $build_dir = $ARGV[2];
   } else {
      # XXX: Make this use the config :P
      $build_dir = "build/host";
   }
} else {
   $eeprom_file = "eeprom.bin";
   $config_file = "radio.json";
   $build_dir = "build/host";
}
my $eeprom_data = '';

# Here we hold the final configuration, at first with defaults loaded...
my $version = {
   "firmware" => {
      "major" => $ver_major,
      "minor" => $ver_minor
   }
};

# Here we store the default configuration
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
# Merge in the version information
$default_cfg = merge($default_cfg, $version);

my $cfgdata = { };
my $use_defaults = 0;

# Load configuration from radio.json ($2)
sub config_load {
   print "* Load config from $_[0]\n";
   open(my $fh, '<', $_[0]) or warn("  => Couldn't open configuration file $_[0], using defaults!\n") and $use_defaults = 1 and return;
   my $nbytes = 0;
   my $tmp = '';

   while (1) {
      my $res = read $fh, $tmp, 512, length($tmp);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($fh);
   $nbytes = length($tmp);
   print "  => Read $nbytes bytes from $_[0]\n";
   my $json = JSON->new;
   $tmp = $json->decode($tmp) or warn("ERROR: Can't parse configuration $_[0]!\n") and return;
   $cfgdata = merge($default_cfg, $tmp);
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
   print "  => Read $nbytes bytes from $_[0]\n";
   my $json = JSON->new;
   $tmp = $json->decode($tmp) or warn("ERROR: Can't parse $_[0]!\n") and return;
}

sub eeprom_load {
   my $eeprom_size = $cfgdata->{"eeprom"}{"size"};
   print "* Loading EEPROM from $_[0]\n";
   open(my $EEPROM, '<:raw', $_[0]) or return;
   my $nbytes = 0;

   while (1) {
      my $res = read $EEPROM, $eeprom_data, 100, length($eeprom_data);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($EEPROM);

   $nbytes = length($eeprom_data);
   if ($nbytes == $eeprom_size) {
      print "  => Loaded $nbytes bytes from $_[0]\n";
   } elsif ($nbytes == 0) {
      print "  => Read empty EEPROM from $_[0]... Fixing!\n";
      return;
   } else {
      die("ERROR: Expected $eeprom_size bytes but read $nbytes from $_[0], bailing to avoid damage...\n");
   }
}

sub eeprom_patch {
   my $errors = 0;
   my $warnings = 0;
   my $eeprom_size = $cfgdata->{"eeprom"}{"size"};

   if ($eeprom_data eq "") {
      print "  => No EEPROM data, starting clean with $eeprom_size byes empty image\n";
      $eeprom_data = "\x00" x $eeprom_size;
   }
   
   if ($use_defaults == 0) {
      print "* Applying configuration...\n";
   }

   if ($warnings > 0) {
      print "*** There were $warnings warnings during patching.\n";
   }

   if ($errors > 0) {
      die("*** There were $errors errors during patching, aborting!\n");
   }
}

sub eeprom_save {
   my $nbytes = length($eeprom_data);
   my $eeprom_size = $cfgdata->{"eeprom"}{"size"};

   if ($nbytes == 0) {
      die("ERROR: Refusing to write empty (0 byte) EEPROM to $_[0]\n");
   }

   if ($nbytes != $eeprom_size) {
      die("ERROR: Our EEPROM ($_[0]) is $eeprom_size bytes, but we ended up with $nbytes bytes of data. Bailing to avoid damage!\n");
   }

   print "* Saving $nbytes bytes to $_[0]\n";
   open(my $EEPROM, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $EEPROM $eeprom_data;
   close $EEPROM;
   print "  => Wrote $nbytes bytes to $_[0]\n";
}

sub generate_eeprom_layout_h {
   my $file = $_[0];
   my $nbytes = 0;
   if ($file eq '') {
      die("generate_eeprom_layout_h: No argument given\n");
   }
   print "  => Generating $_[0]\n";
   open(my $fh, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   # XXX: Walk the json layout and spit out a header for the eeprom.c mess to use...

   close $fh;
}

sub generate_config_h {
   my $file = $_[0];
   my $nbytes = 0;
   if ($file eq '') {
      die("generate_eeprom_layout_h: No argument given\n");
   }
   print "  => Generating $_[0]\n";
   open(my $fh, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $fh "#if     !defined(_config_h)\n#define _config_h\n";
   printf $fh "#define VERSION \"%02d.%02d\"\n", $version->{firmware}{major}, $version->{firmware}{minor};
   printf $fh "#define VERSION_MAJOR 0x%x\n", $version->{firmware}{major};
   printf $fh "#define VERSION_MINOR 0x%x\n", $version->{firmware}{minor};
   printf $fh "#define MY_I2C_ADDR %s\n", $cfgdata->{i2c}{myaddr};
   if ($cfgdata->{eeprom}{type} eq "mmap") {
      print $fh "#define EEPROM_TYPE_MMAP true\n";
      print $fh "#undef EEPROM_TYPE_I2C\n";
      printf $fh "#define EEPROM_MMAP_ADDR %s\n", $cfgdata->{eeprom}{addr};
   } elsif ($cfgdata->{eeprom}{type} eq "i2c") {
      print $fh "#undef EEPROM_TYPE_MMAP\n";
      print $fh "#define EEPROM_TYPE_I2C\n";
      printf $fh "#define EEPROM_I2C_ADDR %s\n", $cfgdata->{eeprom}{addr};
   }
   if (defined($cfgdata->{features})) {
      if (defined($cfgdata->{features}{'cat-kpa500'}) && $cfgdata->{features}{'cat-kpa500'} eq 1) {
         printf $fh "#define CAT_KPA500 true\n";
      }
      if (defined($cfgdata->{features}{'cat-yaesu'}) && $cfgdata->{features}{'cat-yaesu'} eq 1) {
         printf $fh "#define CAT_YAESU true\n";
      }
   }
   printf $fh "#define EEPROM_SIZE %d\n", $cfgdata->{eeprom}{size};
   my $x = '';
   if (defined($cfgdata->{features}{'max-bands'})) {
      $x = $cfgdata->{features}{'max-bands'};
   } else {
      $x = 10;
   }
   printf $fh "#define MAX_BANDS %d\n", $x;

   # XXX: Host mode
   print $fh "#define HOST_EEPROM_FILE \"$eeprom_file\"\n";
   print $fh "#define HOST_LOG_FILE \"firmware.log\"\n";
   print $fh "#define HOST_CAT_PIPE \"cat.fifo\"\n";
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
eeprom_layout_load("eeprom_layout.json");

# Try loading the eeprom into memory
eeprom_load($eeprom_file);

# Apply our radio.json
eeprom_patch($cfgdata);

# Disable interrupt while saving
$SIG{INT} = 'IGNORE';

# Save the patched eeprom.bin
eeprom_save($eeprom_file);

generate_headers();
