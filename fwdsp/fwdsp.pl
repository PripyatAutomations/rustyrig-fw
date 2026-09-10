#!/usr/bin/perl
#
# fwdsp.pl: Perl bridge between GStreamer and stdin/stdout, mirroring
# bin/fwdsp (fwdsp/fwdsp.c) behavior for use where the C binary is not
# practical (boxes without gst dev headers, quick tests, containers).
#
# This is part of rustyrig-fw.
# https://github.com/pripyatautomations/rustyrig-fw
#
# Licensed under MIT license, if built without mongoose or GPL if built with.
#
# CLI and config key layout intentionally match fwdsp.c:
#   -c codec id (4 chars, e.g. pc16)    -f config file
#   -t TX (encode) mode                 -v video
#
# Direction handling (parity with fwdsp.c run_loop()):
#   TX (encoder): pipeline captures audio from the soundcard, encodes and
#                 writes to STDOUT
#   RX (decoder): pipeline reads from STDIN, decodes and plays audio on
#                 the soundcard
#
# The gstreamer side is driven by gst-launch-1.0, with fdsrc fd=0 or
# fdsink fd=1 attached to our own stdio so whatever rrserver/fwdsp-mgr
# pipes in/out of this process is what GStreamer sees.
#
# PARITY: fwdsp/fwdsp.c (run_loop CLI + config key selection)
use strict;
use warnings;
use IO::Handle;
use Getopt::Long;
use POSIX qw(_exit);

my $VERSION = "0.1";
my $DEBUG   = 0;

my $codec       = "pc16";
my $config_file = "config/fwdsp.cfg";
my $video       = 0;
my $tx_mode     = 0;
my $help        = 0;

GetOptions(
   "c=s" => \$codec,
   "f=s" => \$config_file,
   "t"   => \$tx_mode,
   "v"   => \$video,
   "h"   => \$help,
   "d+"  => \$DEBUG,
) || usage(1);
usage(0) if $help;

sub usage {
   my ($rc) = @_;
   print "Usage: fwdsp.pl [-f config file] [-c codec id] [-t] [-v] [-d]\n";
   print "  -c\t\tCodec id such as pc16, mu16, mu08 (default: pc16)\n";
   print "  -f\t\tConfig file (default: config/fwdsp.cfg)\n";
   print "  -t\t\tTX mode: capture soundcard, encode, write to stdout\n";
   print "    \t\tWithout -t: RX mode: read stdin, decode, play on soundcard\n";
   print "  -v\t\tVideo mode\n";
   print "  -d\t\tDebug output\n";
   exit($rc);
}

# ---------------------------------------------------------------------------
# Load config: simple ini parse.  Sections [general] [pipeline] etc.
# Keys are looked up as codec.<tx|rx> in [pipeline] (matching the C one).
# ---------------------------------------------------------------------------
my %cfg;      # section => { key => value }
my $cur_section;

sub cfg_load {
   my ($file) = @_;

   if (!open(my $fh, '<', $file)) {
      warn "fwdsp.pl: Couldn't load config $file: $!\n";
      return 0;
   }
   while (my $line = <$fh>) {
      chomp($line);
      $line =~ s/^\s+|\s+$//g;
      next if !$line || $line =~ /^[#;]/ || $line =~ /^\[.*\]$/;
      if ($line =~ /^\[([^]]+)\]\s*$/) {
         $cur_section = $1;
         $cfg{$cur_section} //= {};
      } elsif ($line =~ /^([^=]+)=(.*)$/) {
         my ($key, $val) = ($1, $2);
         $key =~ s/^\s+|\s+$//g;
         $val =~ s/^\s+|\s+$//g;
         $cfg{$cur_section}{$key} = $val;
      }
   }
   close($fh);
   return 1;
}

# Read a key from [section], with optional dotted-path like 'a.b'
sub cfg_get {
   my ($section, $key) = @_;

   if (my $s = $cfg{$section}) {
      return $s->{$key} if exists $s->{$key};
   }
   return undef;
}
