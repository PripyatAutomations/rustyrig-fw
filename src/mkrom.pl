#!/usr/bin/perl
#
# Here we generate an eeprom.bin from the radio.cf configuration file
use strict;
use warnings;
use Getopt::Std;
use IO::Handle;
use JSON;
use Data::Dumper;
use MIME::Base64;

STDOUT->autoflush(1);

# XXX: Parse commandline for these
my $eeprom = "eeprom.bin";
my $config = "radio.cf";
my $eeprom_size = 16384;		# expected EEPROM size (XXX: get from radio.cf)
my $eedata = '';

# Load configuration from radio.cf ($2)
sub config_load {
   open(my $fh, '<', $_[0]) or die("ERROR: Couldn't open configuration file $_[0], aborting!");

   # XXX: parse the configuration file
}

# Set some defaults
sub eeprom_set_defaults {
   # XXX: do stuff
}

sub eeprom_load {
   print "* Loading EEPROM from $_[0]\n";
   open(my $EEPROM, '<:raw', $_[0]) or $eedata = "\0" x $eeprom_size and
                                    warn("Couldn't open $_[0] for reading: $!\n") and return;
   my $nbytes = 0;

   while (1) {
      my $res = read $EEPROM, $eedata, 100, length($eedata);
      die "$!\n" if not defined $res;
      last if not $res;
   }
   close($EEPROM);

   $nbytes = length($eedata);
   if ($nbytes == $eeprom_size) {
      print "=> Loaded $nbytes bytes from $_[0]\n";
   } elsif ($nbytes == 0) {
      print "=> Read empty EEPROM from $_[0]... Fixing!\n";
      $eedata = "\0" x $eeprom_size;
   } else {
      die("ERROR: Expected $eeprom_size bytes but only read $nbytes from $_[0], bailing to avoid damage...\n");
   }
}

sub eeprom_patch {
   print "* Applying configuration from $config to in memory EEPROM\n";
   $eedata[0] = 23;
   $eedata[1] = 2;
   $eedata[$eeprom_size - 1] = '\ff';
   # If we take errors here, don't automatically apply below
   # XXX: How?
}

sub eeprom_save {
   # Disable interrupt while saving
   $SIG{INT} = 'IGNORE';
   my $nbytes = length($eedata);

   if ($nbytes == 0) {
      die("ERROR: Refusing to write empty (0 byte) EEPROM to $_[0]\n");
   }

   print "* Saving $nbytes to $_[0]\n";
   open(my $EEPROM, '>:raw', $_[0]) or die("ERROR: Couldn't open $_[0] for writing: $!\n");

   print $EEPROM, $eedata;
   close $EEPROM;
   print "\n=> Wrote $nbytes bytes to $_[0]\n";
}

#############################################################
# Apply default settings
eeprom_set_defaults();

# Load the configuration
config_load($config);

# Try loading the eeprom into memory
eeprom_load($eeprom);

# Apply our radio.cf
eeprom_patch();

# Save the patched eeprom.bin
eeprom_save($eeprom);
