#!/usr/bin/perl -w
# $DragonFly: doc/share/sgml/man-refs.pl,v 1.2 2005/08/14 09:18:30 asmodai Exp $

use strict;

while (<>) {
        next unless (m,^(.*/)([\w\._-]+)\.(\d\w*)(\.gz)?$,);
        my ($entity, $page, $volume) = ($2, $2, $3);
        $entity =~ y/_/./;
        print "<!ENTITY man.$entity.$volume \"<citerefentry/<refentrytitle/$page/<manvolnum/$volume//\">\n";
}
