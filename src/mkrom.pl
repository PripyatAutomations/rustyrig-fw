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

sub eeprom_load {
    print "Loading EEPROM from $_[0]\n";
    open(EEPROM, $_[0]) or warn("Couldn't open EEPROM from $_[0]: $!\n");
    binmode EEPROM;
    my $nbytes = 0;

    while (<EEPROM>) {
       my $byte = $_;
       print "read($_[0]): $byte\n";
    }
    close(EEPROM);
}

sub eeprom_patch {
    print "Applying configuration from $_[0]\n";

    # If we take errors here, don't automatically apply below
    # XXX: How?
}

sub eeprom_save {
    # Disable interrupt while saving
    $SIG{INT} = 'IGNORE';

    print "Saving $_[0]\n";
}

eeprom_load($eeprom);
eeprom_patch($config);
eeprom_save($eeprom);
