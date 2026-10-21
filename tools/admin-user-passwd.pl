#!/usr/bin/env perl
use strict;
use warnings;
use DBI;
use Digest::SHA qw(sha1_hex);
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
die "Password cannot be empty\n" unless length($pass1);

my $hash = sha1_hex($pass1);

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
