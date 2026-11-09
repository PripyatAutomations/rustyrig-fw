#!/usr/bin/env perl
use strict;
use warnings;
use DBI;
use IPC::Open2 qw(open2);
use Term::ReadKey;

my $db = $ENV{RRSERVER_DB} // '/var/lib/rustyrig/db/master.db';
my $user = shift @ARGV or die "Usage: $0 <username> [database]\n";
$db = shift @ARGV // $db;

die "Database not found: $db\n" unless -f $db;

my $dbh = DBI->connect("dbi:SQLite:dbname=$db", "", "", {
    RaiseError => 1,
    PrintError => 0,
    AutoCommit => 1,
});

my ($exists) = $dbh->selectrow_array(
    'SELECT 1 FROM users WHERE name = ?', undef, $user
);
die "User '$user' not found\n" unless $exists;

print "New password for $user: ";
ReadMode('noecho');
chomp(my $pass1 = ReadLine(0));

print "\nConfirm password: ";
chomp(my $pass2 = ReadLine(0));
ReadMode('restore');
print "\n";

die "Passwords do not match\n" unless $pass1 eq $pass2;
die "Password must be 8 to 256 characters\n" unless length($pass1) >= 8 && length($pass1) <= 256;

my ($reader, $writer);
my $pid = open2($reader, $writer, '/usr/lib/rustyrig/rr-password-hash');
print {$writer} $pass1;
close $writer;
my $hash = <$reader>;
close $reader;
waitpid($pid, 0);
die "Argon2id hashing failed\n" unless defined($hash) && $? == 0;
chomp($hash);
die "Unexpected password hash format\n" unless index($hash, chr(36) . 'argon2id' . chr(36)) == 0;

$dbh->do(q{
    UPDATE users
       SET password = ?,
           password_set = unixepoch(),
           password_expires = NULL,
           password_change_required = 0
     WHERE name = ?
}, undef, $hash, $user);

print "Password updated for '$user'\n";
$dbh->disconnect;
