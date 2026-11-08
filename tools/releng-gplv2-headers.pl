#!/usr/bin/env perl
# Convert historical RustyRig-owned dual MIT/Mongoose notices to GPLv2-only.
# Safe to rerun. Submodules and bundled third-party files are deliberately skipped.
use strict;
use warnings;
use File::Find;
my $check = @ARGV && $ARGV[0] eq '--check';
die "Usage: $0 [--check]\n" if @ARGV > ($check ? 1 : 0);
my @roots = qw(rrclient rrserver rrbridge rrpipelines fwdsp libfwdspmgr librrsdrutils librustyirc modems gmatrix inc tools tests packaging);
my ($modified, $remaining) = (0, 0);
my @findings;
find({no_chdir => 1, wanted => sub {
   return unless -f $_ && /\.(?:c|h|cc|cpp|m|pl|py|sh|mk)\z/;
   my $file = $File::Find::name;
   return if $file =~ m{(?:^|/)(?:ext|librustyaxe|librrprotocol|callsign-lookup)/};
   return if $file =~ m{^rrclient/win32/};
   open my $in, '<', $file or die "$file: $!\n";
   local $/;
   my $src = <$in>;
   close $in;
   return unless defined $src;
   my $s = $src;
   # Exact historical RustyRig boilerplates only: do not rewrite quoted
   # notices documenting licenses of dependencies.
   $s =~ s{Licensed under MIT license, if built without mongoose or GPL if built with\.}{SPDX-License-Identifier: GPL-2.0-only}gi;
   $s =~ s{Part of rustyrig-fw; MIT license without mongoose, GPL with mongoose\.}{Part of rustyrig-fw; SPDX-License-Identifier: GPL-2.0-only.}gi;
   $s =~ s{Copyright RustyRig contributors, MIT license when built without Mongoose\.}{Copyright RustyRig contributors. SPDX-License-Identifier: GPL-2.0-only.}g;
   if ($s =~ /(?:This is part of rustyrig-fw|Part of rustyrig-fw|Copyright RustyRig|rrserver\/|rrclient\/)/i) {
      $s =~ s{SPDX-License-Identifier:\s*MIT\b}{SPDX-License-Identifier: GPL-2.0-only}g;
   }
   # Duplicate tags can occur where an old boilerplate and a newer SPDX
   # header were present in the same file.
   $s =~ s{(//\s*SPDX-License-Identifier: GPL-2\.0-only\s*\n)(?:(?://[^\n]*\n){0,2})?//\s*SPDX-License-Identifier: GPL-2\.0-only\s*\n}{$1}g;
   return if $s eq $src;
   push @findings, $file;
   $remaining++;
   unless ($check) {
      open my $out, '>', $file or die "$file: $!\n";
      print {$out} $s;
      close $out or die "$file: $!\n";
      $modified++;
   }
}}, @roots);
print "$_\n" for @findings;
print $check ? "$remaining file(s) still use legacy RustyRig headers\n" : "Updated $modified file(s)\n";
exit($check && $remaining ? 1 : 0);
