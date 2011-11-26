#!/bin/sh

#
# Copyright (c) 2008 Peter Holm <pho@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

# Run 1) with no argument: the default test cases
#     2) with the "-a" arguments all the *.cfg test variations
#     3) with one argument: that specific test case

while getopts a name; do
   case $name in
   a) aflag=1;;
   ?) printf "Usage: %s: [-a] [arg]\n" $0
      exit 2;;
   esac
done
shift $(($OPTIND - 1))

. ./default.cfg
if [ ! -z "$aflag" ]; then
   export runRUNTIME=5m
   t1=`date '+%s'`
   while true;do
      for i in `ls *.cfg | grep -v default`; do
         t2=`date '+%s'`
#         e=` date -u -j -f '%s' '+%T' $((t2 - t1))`
#         echo "`date '+%Y%m%d %T'` $i, elapsed $e" | tee /dev/tty >> /tmp/all.log
         echo "`date '+%Y%m%d %T'` $i, elapsed $((t2 - t1))s" | tee /dev/tty >> /tmp/all.log
         logger "Starting test $i"
         $0 $i
      done
      [ "`id -un`" = pho ] && ipcs | \
      awk '$5 ~/pho/ && $6 ~/pho/ {print "-" $1,$2}' | xargs ipcrm
   done
else
   CONFIG=./default.cfg
   if [ $# -eq 1 ]; then
      [ ! -r $1 ] && echo "$0: $1 not found!" && exit 1
      CONFIG=$1
   fi
   . $CONFIG

   [ -z "$RUNDIR" ] && echo "$0: RUNDIR is unset!" && exit 1
   [ `basename $RUNDIR` != stressX ] && \
      echo "$0: Basename of RUNDIR must be stressX!" && exit 2
   [ -d "$RUNDIR" ] && (cd $RUNDIR && find . -delete)

   [ -z "$EXCLUDETESTS" ] && EXCLUDETESTS=DuMmY

   [ -z "$TESTPROGS" ] && \
      TESTPROGS=`find testcases/ -perm -1 -type f | egrep -v "/run/|$EXCLUDETESTS"`
   #LD_PRELOAD=/usr/local/lib/libefence.so.0 ./testcases/run/run $TESTPROGS
   ./testcases/run/run $TESTPROGS
fi
