#!/usr/bin/env perl
# Extract mIRC/RGB color and Unicode block artwork from IRC logs.
# Only core Perl modules are required.
use strict;
use warnings;
use utf8;
use open ':std', ':encoding(UTF-8)';
use Encode qw(decode FB_CROAK);
use Getopt::Long qw(GetOptions);
use File::Path qw(make_path);
use File::Basename qw(basename);
use Time::Local qw(timelocal);
use POSIX qw(strftime);
use Fcntl qw(O_CREAT O_EXCL O_WRONLY);

my $out = './artwork';
my $channel = '';
my $gap = 4;
my $min_lines = 2;
my $max_lines = 100;
my $max_interleave = 8;
my $help = 0;
GetOptions(
  'out|o=s' => \$out, 'channel|c=s' => \$channel,
  'gap=f' => \$gap, 'min-lines=i' => \$min_lines,
  'max-lines=i' => \$max_lines, 'interleave=i' => \$max_interleave,
  'help|h' => \$help,
) or usage(1);
usage(0) if $help;
@ARGV or usage(1);
$gap >= 0 && $min_lines >= 1 && $max_lines >= 1 && $max_interleave >= 0
  or die "Invalid option value\n";
make_path($out);
my $files_written = 0;
my $lines_written = 0;

for my $input (@ARGV) {
  open my $fh, '<:raw', $input or do { warn "Cannot open $input: $!\n"; next };
  my $default_chan = $channel || guess_channel($input);
  my @st = stat($input);
  my $fallback_time = $st[9] || time;
  my $day = strftime('%Y-%m-%d', localtime($fallback_time));
  my $last_ts;
  my $number = 0;
  my %active;

  while (my $raw = <$fh>) {
    $number++;
    $raw =~ s/\r?\n\z//;
    my $line = eval { decode('UTF-8', $raw, FB_CROAK) };
    if (!defined $line) { warn "$input:$number: invalid UTF-8, skipped\n"; next }

    my ($stamp, $nick, $room, $text) = parse_line($line, $default_chan);
    if (!defined $nick) {
      # Unprefixed lines are continuations only when clearly art-like.
      my @candidate = sort { $b->{line_no} <=> $a->{line_no} }
        grep { $number - $_->{line_no} == 1 } values %active;
      if (@candidate == 1 && looks_like_art($line)) {
        my $s = $candidate[0];
        ($nick, $room, $text) = ($s->{nick}, $s->{room}, $line);
      } else { next }
    }
    next unless defined $text && length $text;
    my $ts = parse_timestamp($stamp, $day, $last_ts, $fallback_time);
    $last_ts = $ts if defined $stamp && defined $ts;
    $ts //= $fallback_time;
    my $key = lc($room) . "\0" . lc($nick);
    my $art = looks_like_art($text);

    # Flush older captures before adding the next message. Time-less logs
    # rely on proximity in log lines instead of the file modification time.
    for my $k (keys %active) {
      my $s = $active{$k};
      my $too_far = $number - $s->{line_no} > $max_interleave + 1;
      my $too_old = defined($stamp) && defined($s->{last_ts}) &&
        ($ts < $s->{last_ts} || $ts - $s->{last_ts} > $gap);
      if ($too_far || $too_old) { flush_capture(delete $active{$k}) }
    }

    my $s = $active{$key};
    if ($s && defined($stamp) && defined($s->{last_ts}) &&
        ($ts < $s->{last_ts} || $ts - $s->{last_ts} > $gap)) {
      flush_capture(delete $active{$key});
      $s = undef;
    }
    # Only an art-bearing line can start a new capture. After that, ordinary
    # plain text can continue art when it is symbol-heavy, but a sentence
    # terminates the group instead of contaminating the .mac file.
    if (!$s) {
      next unless $art;
      $s = $active{$key} = {
        room => $room, nick => $nick, start_ts => $ts,
        last_ts => defined($stamp) ? $ts : undef,
        line_no => $number, rows => [],
      };
    } elsif (!$art && !looks_like_continuation($text)) {
      flush_capture(delete $active{$key});
      next;
    }

    push @{ $s->{rows} }, $text;
    $s->{line_no} = $number;
    $s->{last_ts} = $ts if defined $stamp;
    if (@{ $s->{rows} } >= $max_lines) { flush_capture(delete $active{$key}) }
  }
  close $fh;
  flush_capture($_) for values %active;
}
print "Saved $files_written artwork file(s), $lines_written lines into $out\n";

sub flush_capture {
  my ($s) = @_;
  return unless $s && @{ $s->{rows} } >= $min_lines;
  my $base = slug($s->{room}) . '-' . slug($s->{nick}) . '-' .
             strftime('%Y%m%d-%H%M%S', localtime($s->{start_ts}));
  for my $n (1..99999) {
    my $name = $base . ($n == 1 ? '' : "-$n") . '.mac';
    my $path = "$out/$name";
    # Exclusive creation prevents overwriting even if log files overlap.
    if (sysopen(my $of, $path, O_WRONLY | O_CREAT | O_EXCL, 0600)) {
      binmode($of, ':encoding(UTF-8)');
      print {$of} join("\n", @{ $s->{rows} }), "\n";
      close $of or warn "Write failed for $path: $!\n";
      $files_written++;
      $lines_written += @{ $s->{rows} };
      return;
    }
    if ($!{EEXIST}) { next }
    warn "Cannot create $path: $!\n";
    return;
  }
  warn "Too many colliding filenames for $base\n";
}

sub looks_like_art {
  my ($text) = @_;
  return 1 if $text =~ /\x03\d{1,2}/;
  return 1 if $text =~ /\x04[0-9a-fA-F]{6}/;
  return 1 if $text =~ /[\x{2580}-\x{259F}\x{2500}-\x{257F}\x{25A0}\x{25AA}]/;
  return 0;
}
sub looks_like_continuation {
  my ($text) = @_;
  return 1 if looks_like_art($text);
  return 1 if $text =~ /^\s*$/;
  my $clean = $text;
  $clean =~ s/[\x02\x03\x04\x0F\x16\x1D\x1F\x1E\x11]//g;
  return 0 if $clean =~ /\b(?:https?:\/\/|www\.)/i;
  my $letters = () = $clean =~ /[[:alpha:]]/g;
  my $symbols = () = $clean =~ /[^[:alnum:]\s]/g;
  return $symbols >= 3 && $symbols >= $letters;
}
sub slug {
  my ($s) = @_;
  $s =~ s/^[#&]//;
  $s =~ s/[^a-zA-Z0-9_-]/_/g;
  $s = substr($s, 0, 48);
  return length($s) ? $s : 'unknown';
}
sub guess_channel {
  my ($path) = @_;
  my $name = basename($path);
  $name =~ s/\.(?:log|txt)\z//i;
  return $1 if $name =~ /([#&][\w.\-]+)/;
  return $name if $name =~ /^[\w.\-]+$/;
  return 'unknown';
}
sub parse_line {
  my ($line, $default_room) = @_;
  # Raw IRC protocol (with or without @time tags)
  if ($line =~ /^(?:@[^ ]+\s+)?:(\S+?)(?:!\S+)?\s+PRIVMSG\s+(\S+)\s+:(.*)$/i) {
    return (undef, $1, $2, $3);
  }
  my $stamp;
  if ($line =~ s/^\[?(\d{4}-\d\d-\d\d[ T]\d\d:\d\d(?::\d\d)?)\]?\s+//) { $stamp = $1 }
  elsif ($line =~ s/^\[?(\d\d:\d\d(?::\d\d)?)\]?\s+//) { $stamp = $1 }
  elsif ($line =~ s/^\[(\d\d:\d\d(?::\d\d)?)\]\s*//) { $stamp = $1 }
  # Nick with status prefix (+, @, %, ~, &)
  if ($line =~ /^<([~&@%+]?)([^>\s]+)>\s?(.*)$/s) {
    return ($stamp, $2, $default_room, $3);
  }
  # HexChat style: nick<TAB>message
  if ($line =~ /^([~&@%+]?)([\w\[\]{}|\\^`-]{1,40})\t(.*)$/s) {
    return ($stamp, $2, $default_room, $3);
  }
  return;
}
sub parse_timestamp {
  my ($stamp, $date, $prev, $fallback) = @_;
  return undef unless defined $stamp;
  my ($y,$m,$d,$h,$mi,$ss);
  if ($stamp =~ /^(\d{4})-(\d\d)-(\d\d)[ T](\d\d):(\d\d)(?::(\d\d))?$/) {
    ($y,$m,$d,$h,$mi,$ss) = ($1,$2,$3,$4,$5,$6 // 0);
  } elsif ($stamp =~ /^(\d\d):(\d\d)(?::(\d\d))?$/) {
    ($h,$mi,$ss) = ($1,$2,$3 // 0);
    ($y,$m,$d) = split /-/, $date;
  } else { return undef }
  my $ts = eval { timelocal($ss,$mi,$h,$d,$m-1,$y) };
  return defined($ts) ? $ts : $fallback;
}
sub usage {
  my ($exit) = @_;
  print STDERR <<'HELP';
Usage: perl extract-irc-art.pl [options] LOGFILE [LOGFILE ...]
  --out DIR           Output folder (default: ./artwork)
  --channel '#name'   Channel for logs without explicit channel metadata
  --gap SEC           Max gap between timestamped lines (default: 4)
  --min-lines N       Minimum lines per saved art (default: 2)
  --max-lines N       Max lines per art (default: 100)
  --interleave N      Other log lines allowed between art lines (default: 8)
Formats: [HH:MM:SS] <+nick> text, <nick> text, nick<TAB>text,
         YYYY-MM-DD HH:MM:SS <nick> text, raw IRC PRIVMSG.
Recognizes IRC Ctrl+C indexed colors, Ctrl+D RGB colors, and block art.
HELP
  exit $exit;
}
