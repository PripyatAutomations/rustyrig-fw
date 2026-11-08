#!/usr/bin/env perl
# Dump the native client's compiled default CSS as an editable config section.
# Part of rustyrig-fw; MIT without mongoose, GPL when built with it.
use strict;
use warnings;
use FindBin qw($RealBin);
use open qw(:std :encoding(UTF-8));

my $plain_css = 0;
if (@ARGV) {
   if (@ARGV == 1 && ($ARGV[0] eq '--help' || $ARGV[0] eq '-h')) {
      print "Usage: $0 [--css] > output\n";
      print "Default: escaped [gtk-css] block for rrclient.cfg.\n";
      print "--css: plain Linux CSS reference for /etc/rustyrig/rrclient.css.\n";
      exit 0;
   }
   $plain_css = 1 if @ARGV == 1 && $ARGV[0] eq '--css';
   die "Usage: $0 [--css]\n" unless $plain_css;
}
open my $input, '<', "$RealBin/../rrclient/defconfig.c" or die "Cannot read client defaults: $!\n";
my @definitions;
my %font_macro;
while (my $line = <$input>) {
   # The last definition is the Linux fallback in defconfig.c. Windows and
   # macOS choose their own families at build time; this reference is for
   # POSIX /etc/rustyrig installs.
   if ($line =~ /^\s*#define\s+(RR_(?:UI|MONO)_FONT)\s+("(?:\\.|[^"\\])*")/) {
      $font_macro{$1} = $2;
   }
   next unless $line =~ /^\s*#\s*define\s+DEFAULT_CSS\b/;
   my $block = $line;
   while ($line =~ /\\\r?\n\z/) {
      $line = <$input>;
      defined $line or die "Unterminated DEFAULT_CSS definition\n";
      $block .= $line;
   }
   push @definitions, $block;
}
close $input or die "Cannot close client defaults: $!\n";
@definitions or die "No DEFAULT_CSS definition in rrclient/defconfig.c\n";

sub decode_escape {
   my ($escape) = @_;
   my %simple = ('n' => "\n", 'r' => "\r", 't' => "\t", 'b' => "\b", 'f' => "\f", 'v' => "\x0b", 'a' => "\a",
                 '\\' => '\\', '"' => '"', "'" => "'", '?' => '?');
   return $simple{$escape} if exists $simple{$escape};
   return chr(hex(substr($escape, 1))) if $escape =~ /^x[0-9a-f]+$/i;
   return chr(oct($escape)) if $escape =~ /^[0-7]{1,3}$/;
   die "Unsupported C escape in DEFAULT_CSS: \\$escape\n";
}

my @styles;
for my $definition (@definitions) {
   $definition =~ s/\\\r?\n//g; # C preprocessing joins continued source lines first.
   my $css = '';
   # Ignore comments while collecting adjacent C string literals; never evaluate source.
   while ($definition =~ m{(/\*.*?\*/|//[^\n]*|RR_UI_FONT|RR_MONO_FONT|"(?:\\.|[^"\\])*")}gs) {
      my $token = $1;
      if ($token eq 'RR_UI_FONT' || $token eq 'RR_MONO_FONT') {
         my $macro = $font_macro{$token} // die "Missing $token definition\n";
         my $value = substr($macro, 1, -1);
         $value =~ s/\\(x[0-9a-fA-F]+|[0-7]{1,3}|.)/decode_escape($1)/ge;
         $css .= $value;
      } elsif (substr($token, 0, 1) eq '"') {
         my $text = substr($token, 1, -1);
         $text =~ s/\\(x[0-9a-fA-F]+|[0-7]{1,3}|.)/decode_escape($1)/ge;
         $css .= $text;
      }
   }
   length $css or die "Empty DEFAULT_CSS definition\n";
   push @styles, $css;
}
# The current source repeats the same definition. Refuse ambiguous future variants.
for my $css (@styles) {
   $css eq $styles[0] or die "Distinct DEFAULT_CSS variants found; select the intended build variant before dumping\n";
}
if ($plain_css) {
   print "/* Reference only: compiled RustyRig GTK defaults (Linux font stack).\n";
   print " * NOT a configuration file and NOT loaded automatically.\n";
   print " * Copy/edit rules in rrclient.cfg [gtk-css], then run /css-reload.\n";
   print " */\n";
} else {
   print "[gtk-css]\n";
}
for my $line (split /\n/, $styles[-1]) {
   $line =~ s/^(\s*)(?=[#;\\]|\/\/)/$1\\/ unless $plain_css;
   print "$line\n";
}
