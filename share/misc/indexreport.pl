#!/usr/bin/perl
# 
# Report on the sparsity of indexterms in specified DocBook SGML files.
#
# Usage : indexreport.pl <sgmlfiles> ...
#
# $DragonFly: doc/share/misc/indexreport.pl,v 1.1.1.1 2004/04/02 09:36:31 hmp Exp $
#

use strict;

# Number of lines in which we expect to see at least one indexterm.
my $threshold = 75;

foreach my $filename (@ARGV) {
    print "Checking $filename...\n";
    check_index($filename);
}

# check_index(filename)
#
# Checks for indexterms in a given filename and prints a warning for
# sparse areas of the file that may not be fully indexed.

sub check_index($) {
    my $filename = $_[0];
    open(SGMLFILE, $filename) || die "Could not open $filename : $!";
    my $linecnt = 0;
    my $lastterm = 0;

    foreach my $line (<SGMLFILE>) {
	if ($line =~ /<indexterm>/) {
	    if (($linecnt - $lastterm) > $threshold) {
		printf "%d lines with no indexterm starting at %s:%d\n",
		    $linecnt - $lastterm, $filename, $lastterm;
	    }
	    $lastterm = ++$linecnt;
	} else {
	    $linecnt++;
	}
    }
}
