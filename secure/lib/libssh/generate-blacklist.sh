#!/bin/bash
# $DragonFly: src/secure/lib/libssh/generate-blacklist.sh,v 1.1 2008/05/21 14:07:41 corecode Exp $
set -e

LIBSSL=$(apt-cache policy libssl0.9.8 | grep Installed | awk '{print $NF}')
dpkg --compare-versions "$LIBSSL" lt 0.9.8g-9 || {
    echo "Your libssl0.9.8 is newer than the fixed version (0.9.8g-9)." >&2
    echo "This script is only sensible to run with a broken version.  :)" >&2
    exit 1
}

KEYTYPE=$(echo "$1" | tr A-Z a-z)
KEYSIZE="$2"
if [ -z "$KEYTYPE" ] || [ -z "$KEYSIZE" ]; then
    echo "Usage: $0 KEYTYPE KEYSIZE" >&2
    exit 1
fi

WORKDIR=$(mktemp -d -t blacklist-XXXXXX)
cd "$WORKDIR"

cat >getpid.c <<EOM
/*
 * Compile:

gcc -fPIC -c getpid.c -o getpid.o
gcc -shared -o getpid.so getpid.o

 * Use:
 
FORCE_PID=1234 LD_PRELOAD=./getpid.so bash

#
# Copyright (C) 2001-2008 Kees Cook
# kees@outflux.net, http://outflux.net/
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
# http://www.gnu.org/copyleft/gpl.html

*/

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

pid_t getpid(void)
{
    return atoi(getenv("FORCE_PID"));
}
EOM

gcc -fPIC -c getpid.c -o getpid.o
gcc -shared -o getpid.so getpid.o

echo "# generated on $(uname -m) at $(date)"

for pid in $(seq 1 32767)
do
    FILE="key-$pid"
    HASH=$(FORCE_PID="$pid" LD_PRELOAD=./getpid.so \
        ssh-keygen -P "" -t "$KEYTYPE" -b "$KEYSIZE" -f "$FILE" | \
            grep :..: | cut -d" " -f1 | sed -e 's/://g')
    rm -f "$FILE" "$FILE".pub
    echo "$HASH"
done

rm -f getpid.*
cd /
rmdir "$WORKDIR"
