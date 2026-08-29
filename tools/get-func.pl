#!/usr/bin/env perl
# a little llm created tool for pulling functions and touched globals from a tree
#
# this should make it easier to focus in on one function and its bugs
#
use strict;
use warnings;
use File::Glob qw(bsd_glob);

my $func = shift @ARGV
   or die "usage: $0 function files...\n";

my @files;

for my $arg (@ARGV) {
   if (-f $arg) {
      push @files, $arg;
   } else {
      push @files, bsd_glob($arg);
   }
}

die "No source files found\n" unless @files;

my %src;
my %globals;
my %functions;
my %macros;
my %output;

sub load_file {
   my ($file) = @_;

   open my $fh, '<', $file or die "$file: $!\n";
   local $/;
   $src{$file} = <$fh>;
   close $fh;
}

#
# Replace strings, character constants, and comments with spaces while
# preserving newlines and character positions.
#
sub mask_c {
   my ($s) = @_;

   $s =~ s{
      ("(?:\\.|[^"\\])*") |
      ('(?:\\.|[^'\\])*') |
      (//[^\n]*) |
      (/\*.*?\*/)
   }{
      my $x = $1 // $2 // $3 // $4;
      $x =~ s/[^\n]/ /g;
      $x
   }gse;

   return $s;
}

#
# Find the matching closing character for an opening character.
#
sub matching_pair {
   my ($s, $start, $open, $close) = @_;

   my $depth = 0;

   for (my $i = $start; $i < length($s); ++$i) {
      my $c = substr($s, $i, 1);

      ++$depth if $c eq $open;

      if ($c eq $close) {
         --$depth;
         return $i if $depth == 0;
      }
   }

   return;
}

sub line_number {
   my ($s, $pos) = @_;
   return 1 + (substr($s, 0, $pos) =~ tr/\n//);
}

#
# Find function definitions.
#
sub find_functions {
   my ($file) = @_;

   my $text = $src{$file};
   my $s = mask_c($text);

   while ($s =~ /\b([A-Za-z_]\w*)\s*\(/g) {
      my $name = $1;
      my $name_start = $-[1];
      my $open = $+[0] - 1;

      my $close = matching_pair($s, $open, '(', ')');
      next unless defined $close;

      # Skip whitespace between ')' and '{'.
      my $p = $close + 1;

      $p++ while $p < length($s) &&
         substr($s, $p, 1) =~ /\s/;

      # Function declaration/prototype rather than definition.
      next unless $p < length($s) &&
         substr($s, $p, 1) eq '{';

      my $end = matching_pair($s, $p, '{', '}');
      next unless defined $end;

      $functions{$name} ||= [];

      push @{$functions{$name}}, {
         file  => $file,
         start => $name_start,
         end   => $end,
      };

      # Don't search inside the function body for more functions.
      pos($s) = $end + 1;
   }
}

#
# Find #define directives.
#
sub find_macros {
   my ($file) = @_;

   my $text = $src{$file};

   while ($text =~ /^(\s*#\s*define\s+([A-Za-z_]\w*(?:\([^)]*\))?)[^\n]*(?:\\\n[^\n]*)*)/mg) {
      my $name = $2;

      # Strip function-like argument list from the lookup name.
      $name =~ s/\(.*\)$//;

      $macros{$name} ||= [];

      push @{$macros{$name}}, {
         file  => $file,
         start => $-[1],
         end   => $+[1] - 1,
      };
   }
}

#
# Find file-scope variable definitions/declarations.
#
# This is deliberately heuristic. We're interested in common C globals,
# not in trying to implement the C grammar.
#
sub find_globals {
   my ($file) = @_;

   my $text = $src{$file};
   my $s = mask_c($text);

   # Blank function bodies so declarations inside functions aren't
   # mistaken for globals.
   while ($s =~ /\{/g) {
      my $open = $-[0];
      my $end = matching_pair($s, $open, '{', '}');

      last unless defined $end;

      substr($s, $open, $end - $open + 1) =~ s/[^\n]/ /g;
   }

   #
   # Look for declarations beginning at the start of a line.
   #
   # Handles things such as:
   #
   #   bool foo;
   #   bool foo = false;
   #   static int foo;
   #   static struct foo bar;
   #   const char *foo = "...";
   #
   while ($s =~ /
      ^[ \t]*
      (?:
         static\s+
      |  extern\s+
      |  const\s+
      |  volatile\s+
      |  unsigned\s+
      |  signed\s+
      |  long\s+
      |  short\s+
      )*
      (?:
         struct\s+[A-Za-z_]\w*\s* |
         union\s+[A-Za-z_]\w*\s* |
         enum\s+[A-Za-z_]\w*\s* |
         [A-Za-z_]\w*\s+
      )
      (?:\*+\s*)?
      ([A-Za-z_]\w*)
      \s*
      (?:\[[^\]]*\]\s*)?
      (?:
         =
         |
         ;
      )
   /gmx) {
      my $name = $1;
      my $start = $-[0];

      # Find the terminating semicolon.
      my $end = index($text, ';', $start);
      next if $end < 0;

      # Don't record extern declarations as definitions unless there
      # really is an initializer.
      my $decl = substr($text, $start, $end - $start + 1);

      if ($decl =~ /\bextern\b/ && $decl !~ /=/) {
         next;
      }

      $globals{$name} ||= [];

      push @{$globals{$name}}, {
         file  => $file,
         start => $start,
         end   => $end,
      };
   }
}

#
# Load everything first.
#
for my $file (@files) {
   load_file($file);
}

#
# Build symbol tables.
#
for my $file (@files) {
   find_functions($file);
   find_macros($file);
   find_globals($file);
}

die "Function '$func' not found\n"
   unless exists $functions{$func};

#
# Emit something once.
#
sub emit {
   my ($key, $text) = @_;

   return if $output{$key}++;

   print $text;
}

#
# Extract a global variable definition.
#
sub extract_global {
   my ($name) = @_;

   return unless exists $globals{$name};

   for my $g (@{$globals{$name}}) {
      my $text = $src{$g->{file}};
      my $line = line_number($text, $g->{start});

      my $key = "global:$g->{file}:$g->{start}";

      emit(
         $key,
         "/* global $name: $g->{file}:$line */\n" .
         substr(
            $text,
            $g->{start},
            $g->{end} - $g->{start} + 1
         ) .
         "\n\n"
      );
   }
}

#
# Extract a macro.
#
sub extract_macro {
   my ($name) = @_;

   return unless exists $macros{$name};

   for my $m (@{$macros{$name}}) {
      my $text = $src{$m->{file}};
      my $line = line_number($text, $m->{start});

      my $key = "macro:$m->{file}:$m->{start}";

      emit(
         $key,
         "/* macro $name: $m->{file}:$line */\n" .
         substr(
            $text,
            $m->{start},
            $m->{end} - $m->{start} + 1
         ) .
         "\n\n"
      );
   }
}

#
# Extract the requested function.
#
my $f = $functions{$func}[0];

my $function_text = substr(
   $src{$f->{file}},
   $f->{start},
   $f->{end} - $f->{start} + 1
);

#
# Mask the function and collect identifiers.
#
my $masked = mask_c($function_text);

my %used;

while ($masked =~ /\b([A-Za-z_]\w*)\b/g) {
   $used{$1} = 1;
}

#
# Globals referenced by the function.
#
for my $name (sort keys %used) {
   extract_global($name);
}

#
# Macros referenced by the function.
#
for my $name (sort keys %used) {
   extract_macro($name);
}

#
# Finally print the requested function.
#
my $line = line_number(
   $src{$f->{file}},
   $f->{start}
);

print "/* ============================================================ */\n";
print "/* get-func: $func */\n";
print "/* source: $f->{file}:$line */\n";
print "/* ============================================================ */\n\n";

print $function_text;
print "\n";
