#!/usr/bin/perl -w
# $DragonFly: doc/share/sgml/man-refs.pl,v 1.1.1.1 2004/04/02 09:37:08 hmp Exp $
# $DragonFly: doc/share/sgml/man-refs.pl,v 1.1.1.1 2004/04/02 09:37:08 hmp Exp $

use strict;

while (<>) {
        next unless (m,^(.*/)([\w\._-]+)\.(\d\w*)(\.gz)?$,);
        my ($entity, $page, $volume) = ($2, $2, $3);
        $entity =~ y/_/./;
        print "<!ENTITY man.$entity.$volume \"<citerefentry/<refentrytitle/$page/<manvolnum/$volume//\">\n";
}
