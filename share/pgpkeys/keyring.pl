#!/usr/bin/perl -w
#
# $DragonFly: doc/share/pgpkeys/keyring.pl,v 1.1.1.1 2004/04/02 09:36:37 hmp Exp $

use strict;
use Fcntl;
use Getopt::Std;
use vars qw(%KEYS);

sub add_file($);
sub add_dir($);

sub add_dir($) {
    my $dn = shift;

    local *DIR;
    my $ent;

    $dn =~ s|/+|/|g;
    $dn =~ s|^(.+)/$|$1|;
    opendir(DIR, $dn)
	or die("$dn: opendir(): $!\n");
    while ($ent = readdir(DIR)) {
	next if ($ent eq "." || $ent eq "..");
	add_file("$dn/$ent");
    }
    closedir(DIR);
}

sub add_file($) {
    my $fn = shift;

    local *FILE;
    my $line;
    my $key;

    if (-d $fn) {
	return add_dir($fn);
    }

    sysopen(FILE, $fn, O_RDONLY)
	or die("$fn: open(): $!\n");
    while (<FILE>) {
	if (m'-----BEGIN PGP PUBLIC KEY BLOCK-----$') {
	    $key = $_;
	} elsif (m'^-----END PGP PUBLIC KEY BLOCK-----') {
	    $key .= $_;
	    chomp($key);
	    $KEYS{$key}++
		if defined($key);
	    $key = undef;
	} elsif (defined($key)) {
	    $key .= $_;
	}
    }
}

sub usage() {

    print(STDERR "Usage: keyring [-o outfile] [dir|file] ...\n");
}

MAIN:{
    my %opts;

    getopts('o:', \%opts)
	or usage();

    if ($opts{'o'}) {
	sysopen(STDOUT, $opts{'o'}, O_RDWR|O_CREAT|O_TRUNC)
	    or die("$opts{'o'}: open(): $!\n");
    }
    if (!@ARGV) {
	add_dir(".");
    } else {
	while (@ARGV) {
	    add_file(shift(@ARGV));
	}
    }
    print(join("\n", sort(keys(%KEYS))));
}
