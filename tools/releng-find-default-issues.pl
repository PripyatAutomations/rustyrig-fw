#!/usr/bin/perl
#
# cfg-find-defaults.pl: Find cfg_get*() calls whose default value differs
#                       from what's in the respective defconfig.c
#    This is part of rustyrig-fw.
# https://github.com/pripyatautomations/rustyrig-fw
#
# Scans:
#   - librustyaxe and librrprotocol sources for "common" cfg_get* calls
#     (compared against BOTH rrclient and rrserver defconfig.c)
#   - rrclient sources (compared against rrclient/defconfig.c)
#   - rrserver sources (compared against rrserver/defconfig.c)
#
# Only cfg_get_*() calls taking a default (cfg_get_bool, cfg_get_int,
# cfg_get_uint) are compared.  Plain cfg_get()/cfg_get_exp() have no
# default argument, so they are only used to detect keys missing from
# the defconfig entirely.
#
# Output (quiet by default, this is a sanity check for missed config keys):
#   1. A list of MISMATCHED defaults -- the default in the cfg_get* call
#      differs from the value in that subproject's defconfig.c, with the
#      file:line of the call.
#   2. Paste-ready template lines for each subproject, listing keys missing
#      from its defconfig.c.  If a default was found in the cfg_get* call
#      it's pre-filled; otherwise NULL is used -- just add a description
#      (and a value if the default is not known).
#
# Usage: tools/cfg-find-defaults.pl [-v] [rootdir]
#
# Licensed under MIT license, if built without mongoose or GPL if built with.
#

use strict;
use warnings;

use File::Find;
use Getopt::Long;

my $verbose = 0;
GetOptions('v|verbose' => \$verbose) or die "Usage: $0 [-v] [rootdir]\n";

my $root = shift @ARGV || '.';
$root =~ s{/+$}{};

# Subprojects: name => [source dirs, defconfig file]
my %projects = (
   rrclient => { srcs => ['rrclient'], defconfig => 'rrclient/defconfig.c' },
   rrserver => { srcs => ['rrserver'], defconfig => 'rrserver/defconfig.c' },
);

# Common sources are checked against every project's defconfig
my @common_srcs = ('librustyaxe', 'librrprotocol');

# ---------------------------------------------------------------------------
# Load defconfig.c defaults for a subproject: { key => value (undef = NULL) }
# ---------------------------------------------------------------------------
sub load_defconfig {
   my ($file) = @_;
   my %defaults;

   open(my $fh, '<', $file) or die "Can't read $file: $!\n";
   my $in_array = 0;
   while (my $line = <$fh>) {
      # Skip comments
      next if $line =~ m{^\s*//};
      $line =~ s{//.*$}{};

      if (!$in_array) {
         $in_array = 1 if $line =~ /defconfig_t\s+defcfg\s*\[\s*\]/;
         next;
      }
      last if $line =~ /^\s*\}\s*;/;

      # { "key", "value", "comment" }
      if ($line =~ /^\s*\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*(NULL|"(?:[^"\\]|\\.)*")\s*,/) {
         my ($key, $val) = ($1, $2);
         $key =~ s/\\(.)/$1/g;
         if ($val eq 'NULL') {
            $defaults{$key} = undef;
         } else {
            $val =~ s/^"//;
            $val =~ s/"$//;
            $val =~ s/\\(.)/$1/g;
            $defaults{$key} = $val;
         }
      }
   }
   close($fh);
   return \%defaults;
}

# ---------------------------------------------------------------------------
# Normalize a default for comparison
# ---------------------------------------------------------------------------
sub norm_bool {
   my ($v) = @_;
   return undef unless defined $v;
   return 'true'  if $v =~ /^(true|yes|on|1)$/i;
   return 'false' if $v =~ /^(false|no|off|0)$/i;
   return undef;
}

sub norm_num {
   my ($v) = @_;
   return undef unless defined $v;
   my $s = $v;
   $s =~ s/^\s+|\s+$//g;
   $s =~ s/^0+//;         # leading zeros
   $s =~ s/^\+//;
   return $s eq '' ? '0' : $s;
}

sub defaults_match {
   my ($func, $call_def, $cfg_def) = @_;

   return 0 unless defined $cfg_def;   # missing or NULL in defconfig

   if ($func eq 'cfg_get_bool') {
      my $a = norm_bool($call_def);
      my $b = norm_bool($cfg_def);
      return (defined($a) && defined($b)) ? ($a eq $b ? 1 : 0) : 0;
   }
   if ($func eq 'cfg_get_int' || $func eq 'cfg_get_uint') {
      my $a = norm_num($call_def);
      my $b = norm_num($cfg_def);
      return (defined($a) && defined($b)) ? ($a == $b ? 1 : 0) : 0;
   }
   # Anything else: literal string compare, trimmed
   (my $a = $call_def) =~ s/^\s+|\s+$//g;
   (my $b = $cfg_def)  =~ s/^\s+|\s+$//g;
   return $a eq $b ? 1 : 0;
}

# ---------------------------------------------------------------------------
# Scan a file for cfg_get* calls
# Returns list of { file, line, func, key, def }
# ---------------------------------------------------------------------------
sub scan_file {
   my ($file) = @_;
   my @found;

   open(my $fh, '<', $file) or do {
      warn "Can't read $file: $!\n";
      return @found;
   };

   my $lineno = 0;
   my $pending = '';    # for calls spanning lines
   my $pending_line = 0;

   while (my $line = <$fh>) {
      $lineno++;
      chomp $line;
      next if $line =~ m{^\s*//};

      my $text = $pending eq '' ? $line : "$pending $line";
      my $start_line = $pending eq '' ? $lineno : $pending_line;

      # Find calls; allow multiple per line
      my $pos = 0;
      my $matched = 0;
      while ($text =~ /\b(cfg_get_(?:bool|int|uint|exp)|cfg_get)\s*\(/g) {
         $matched = 1;
         my $func = $1;
         my $paren_at = pos($text) - 1;
         my $rest = substr($text, $paren_at);

         # Extract balanced-paren argument list
         my ($args, $consumed);
         {
            my $depth = 0;
            my $i = 0;
            my $len = length($rest);
            while ($i < $len) {
               my $c = substr($rest, $i, 1);
               $depth++ if $c eq '(';
               if ($c eq ')') {
                  $depth--;
                  if ($depth == 0) { last; }
               }
               $i++;
            }
            if ($depth != 0) { $consumed = -1; last; }   # unbalanced: continue to next line
            $args = substr($rest, 1, $i - 1);
            $consumed = $i + 1;
         }

         if ($consumed == -1) {
            # Call continues on next line: keep as pending if at text end
            if (!/\;\s*$/) {
               $pending = $text;
               $pending_line = $start_line;
            } else {
               $pending = '';
            }
            last;
         }

         # Parse args: first is "key", second (optional) is default
         my ($key, $def) = (undef, undef);
         if ($args =~ /^\s*"((?:[^"\\]|\\.)*)"\s*(?:,\s*(.*?)\s*)?$/) {
            $key = $1;
            $key =~ s/\\(.)/$1/g;
            $def = $2 if defined $2 && $2 ne '';
         }

         if (defined $key) {
            push @found, {
               file => $file, line => $start_line, func => $func,
               key => $key, def => $def,
            };
         }

         # Continue scanning after this call
         $pos = $paren_at + $consumed;
         pos($text) = $pos;
         $text =~ /\G/gc;   # re-anchor
      }

      if (!$matched || pos($text)) {
         # If a call was left unbalanced mid-text and we ran out of regex
         if ($pending ne '' && !/\;\s*$/) {
            # keep pending for next line
         } else {
            $pending = '';
         }
      }
   }
   close($fh);
   return @found;
}

# ---------------------------------------------------------------------------
# Gather source files under a dir (skip tests, disabled, win32, build dirs)
# ---------------------------------------------------------------------------
sub want_file {
   my ($path) = @_;
   return 0 unless $path =~ /\.(c|h|cpp|hpp)$/;
   return 0 if $path =~ m{(^|/)(tests?|disabled|build|win32|\.git)(/|$)};
   return 1;
}

my %files;   # file => project ('rrclient', 'rrserver', or 'common')
my @scan_dirs = map { "$root/$_" } (@common_srcs, map { @{$projects{$_}{srcs}} } keys %projects);
my @all_files;
find({
   wanted => sub {
      return unless -f $_;
      my $p = $File::Find::name;
      return unless want_file($p);
      push @all_files, $p;
   },
   no_chdir => 1,
}, @scan_dirs);

# Classify files by subproject
for my $f (@all_files) {
   if ($f =~ m{^\Q$root\E/librustyaxe/} || $f =~ m{^\Q$root\E/librrprotocol/}) {
      $files{$f} = 'common';
   } elsif ($f =~ m{^\Q$root\E/rrclient/}) {
      $files{$f} = 'rrclient';
   } elsif ($f =~ m{^\Q$root\E/rrserver/}) {
      $files{$f} = 'rrserver';
   }
}

# Load defconfigs
my %defaults;
for my $proj (keys %projects) {
   my $df = "$root/$projects{$proj}{defconfig}";
   die "Missing defconfig: $df\n" unless -f $df;
   $defaults{$proj} = load_defconfig($df);
}

# Missing keys per subproject, in first-seen order:
# $missing{$proj}{$key} = { def => default-or-undef }
my %missing;
my @missing_order;   # [$proj, $key] to keep stable output

# Collected mismatches: { p, file, line, func, key, def, cfg_def }
my @mismatch_list;

# Scan and compare
my $mismatches = 0;
for my $f (sort keys %files) {
   my $proj = $files{$f};
   my @calls = scan_file($f);
   next unless @calls;

   for my $call (@calls) {
      my @check;   # list of [project, defaults-hashref]
      if ($proj eq 'common') {
         push @check, [$_, $defaults{$_}] for sort keys %projects;
      } else {
         push @check, [$proj, $defaults{$proj}];
      }

      for my $c (@check) {
         my ($p, $defs) = @$c;
         my $key = $call->{key};
         my $has = exists $defs->{$key};
         my $cfg_def = $has ? $defs->{$key} : undef;

         if (!$has) {
            if (!exists $missing{$p}{$key}) {
               push @missing_order, [$p, $key];
            }
            $missing{$p}{$key} //= {};
            # Prefer a default seen in a call, first one with a default wins
            $missing{$p}{$key}{def} = $call->{def}
               if defined $call->{def} && !defined $missing{$p}{$key}{def};
            next;
         }

         # NULL in defconfig means "no default" - nothing to compare
         next if !defined $cfg_def;

         # No default arg in the call: nothing to compare
         next if !defined $call->{def};

         next if defaults_match($call->{func}, $call->{def}, $cfg_def);

         push @mismatch_list, {
            p => $p, file => $call->{file}, line => $call->{line},
            func => $call->{func}, key => $key,
            def => $call->{def}, cfg_def => $cfg_def,
         };
      }
   }
}

# ---------------------------------------------------------------------------
# Output 1: mismatched defaults, with the file/line of the cfg_get* call
# ---------------------------------------------------------------------------
if (@mismatch_list) {
   print "MISMATCHED DEFAULTS (call default differs from defconfig.c):\n";
   for my $m (@mismatch_list) {
      printf("  %s: %s:%d: %s(\"%s\", %s) -- defconfig has \"%s\"\n",
             $m->{p}, $m->{file}, $m->{line}, $m->{func},
             $m->{key}, $m->{def}, $m->{cfg_def});
   }
}

# ---------------------------------------------------------------------------
# Output 2: paste-ready template lines for keys missing from each defconfig
# ---------------------------------------------------------------------------
$mismatches = @mismatch_list + @missing_order;   # unique findings
if (%missing) {
   # Group keys by project, preserving first-seen order within each
   my (%by_proj, @proj_order);
   for my $entry (@missing_order) {
      my ($p, $key) = @$entry;
      if (!exists $by_proj{$p}) {
         $by_proj{$p} = [];
         push @proj_order, $p;
      }
      push @{$by_proj{$p}}, $key;
   }

   for my $p (@proj_order) {
      print "\n# Paste into $p/defconfig.c (add a description; fill a value where the default is unknown):\n";
      for my $key (@{$by_proj{$p}}) {
         my $def = $missing{$p}{$key}{def};
         my $value = defined $def ? "\"$def\"" : 'NULL';
         printf("   { \"%s\", %s, \"TODO: describe this key\" },\n", $key, $value);
      }
   }
}

print "\n$mismatches mismatch(es) found.\n" if $mismatches || $verbose;
exit($mismatches ? 1 : 0);
