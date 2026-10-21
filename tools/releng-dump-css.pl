#!/usr/bin/env perl
# Dump the native client's compiled default CSS as an editable config section.
# Part of rustyrig-fw; MIT without mongoose, GPL when built with it.
use strict;
use warnings;
use FindBin qw($RealBin);
use open qw(:std :encoding(UTF-8));

if (@ARGV) {
   if (@ARGV == 1 && ($ARGV[0] eq '--help' || $ARGV[0] eq '-h')) {
      print "Usage: $0 > gtk-css.cfg\nExtract rrclient/defconfig.c defaults as a [gtk-css] block.\n";
      exit 0;
   }
   die "Usage: $0 [--help]\n";
}
open my $input, '<', "$RealBin/../rrclient/defconfig.c" or die "Cannot read client defaults: $!\n";
my @definitions;
while (my $line = <$input>) {
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
   while ($definition =~ m{(/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*")}gs) {
      my $token = $1;
      next unless substr($token, 0, 1) eq '"';
      my $text = substr($token, 1, -1);
      $text =~ s/\\(x[0-9a-fA-F]+|[0-7]{1,3}|.)/decode_escape($1)/ge;
      $css .= $text;
   }
   length $css or die "Empty DEFAULT_CSS definition\n";
   push @styles, $css;
}
# The current source repeats the same definition. Refuse ambiguous future variants.
for my $css (@styles) {
   $css eq $styles[0] or die "Distinct DEFAULT_CSS variants found; select the intended build variant before dumping\n";
}
print "[gtk-css]\n";
for my $line (split /\n/, $styles[-1]) {
   $line =~ s/^(\s*)(?=[#;\\]|\/\/)/$1\\/;
   print "$line\n";
}
