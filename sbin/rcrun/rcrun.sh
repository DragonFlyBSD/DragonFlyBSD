#!/bin/sh
#
# Copyright (c) 2003
#	The DragonFly Project.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of The DragonFly Project nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
# COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

if [ -r /etc/defaults/rc.conf ]; then
	. /etc/defaults/rc.conf
fi

if [ -r /etc/rc.conf ]; then
	. /etc/rc.conf
fi

buildrclist()
{
    rcfiles=`find /etc/rc.d -type f`
    for d in $local_startup; do
	if [ -d $d ]; then
	    rcfiles="$rcfiles `find $d -type f`"
	fi
    done
    rclist=`rcorder -o $1 $rcfiles`
}

dostart()
{
    arg=$1
    shift

    for i in $@; do
	case X`varsym -s -q rcng_$i` in
	Xrunning*)
	    echo "$i has already been started"
	    ;;
	Xconfigured*)
	    echo "$i has already been configured"
	    ;;
	*)
	    _return=0
	    buildrclist $i
	    for j in $rclist; do
		need=1
		for k in `rcorder -p $j`; do
		    if [ $k = $i ]; then
			need=0
		    else
			state=`varsym -s -q rcng_$k`
			case X$state in
			Xrunning*|Xconfigured*|Xirrelevant*|Xdisabled*)
			    ;;
			*)
			    echo "$i depends on $k, current state: $state"
			    _return=1
			    ;;
			esac
		    fi
		done
	    done
	    # $j contains the last dependency, which we run
	    #
	    if [ X$j = X ]; then
		echo "Unable to find keyword $i"
	    elif [ $_return = 0 ]; then
		echo "Running $j $arg"
		(sh $j $arg)
		case X`varsym -s -q rcng_$i` in
		Xdisabled*)
		    echo "$i is disabled, enable in rc.conf first or use rcforce/rcone"
		    ;;
		Xfailed*)
		    echo "$i has failed to start"
		    ;;
			
		esac
	    fi
	    ;;
	esac
    done
}

arg=$0
case ${0##*/} in
rcstart)
    arg=start
    ;;
rcstop)
    arg=stop
    ;;
rcrestart)
    arg=restart
    ;;
rcvar)
    arg=rcvar
    ;;
rcvars)
    arg=rcvar
    ;;
rclist)
    arg=list
    ;;
rcforce)
    arg=forcestart
    ;;
rcfast)
    arg=faststart
    ;;
rcone)
    arg=onestart
    ;;
rcenable)
    arg=enable
    ;;
rcdisable)
    arg=disable
    ;;
*)
    arg=$1
    shift
    ;;
esac

case $arg in
start)
	dostart start $@
	;;
forcestart)
	dostart forcestart $@
	;;
faststart)
	dostart faststart $@
	;;
onestart)
	dostart onestart $@
	;;
stop)
	for i in $@; do
	    buildrclist $i
	    j=`echo "$rclist" | tail -1`
	    if [ X$j = X ]; then
		echo "Unable to find keyword $i"
	    else
		(sh $j stop)
	    fi
	done
	;;
restart)
	for i in $@; do
	    buildrclist $i
	    j=`echo "$rclist" | tail -1`
	    if [ X$j = X ]; then
		echo "Unable to find keyword $i"
	    else
		(sh $j restart)
	    fi
	done
	;;
disable|enable)
	if [ "$arg" = "enable" ]; then
	    mode=YES
	else
	    mode=NO
	fi
	for i in $@; do
	    buildrclist $i
	    j=`echo "$rclist" | tail -1`
	    if [ X$j = X ]; then
		echo "Unable to find provider id $i"
	    elif [ `varsym -s -q rcng_$i` = "$mode" ]; then
		echo "$i is already $mode"
	    else
		vars=`(sh $j rcvar) 2>/dev/null | grep = | sed -e 's/\\$//g' | sed -e 's/=.*//g'`
		cp /etc/rc.conf /etc/rc.conf.bak
		if [ $arg = disable ]; then
		    rcstop $i
		fi
		for k in $vars; do
		    rm -f /etc/rc.conf.$$
		    ( egrep -v "# rcrun enable ${k}$" /etc/rc.conf; printf "${k}=${mode}\t# rcrun enable ${k}\n" ) > /etc/rc.conf.$$
		    mv -f /etc/rc.conf.$$ /etc/rc.conf
		    echo "added/modified: ${k}=${mode}"
		done
		if [ $arg = enable ]; then
		    rcstart $i
		fi
	    fi
	done
	;;
rcvar)
	for i in $@; do
	    buildrclist $i
	    j=`echo "$rclist" | tail -1`
	    if [ X$j = X ]; then
		echo "Unable to find provider id $i"
	    else
		(sh $j rcvar)
	    fi
	done
	;;
list)
	if [ "X$*" = X ]; then
	    for i in `varsym -a -s | egrep '^rcng_'`; do
		echo $i
	    done
	else
	    for i in $@; do
		varsym -s rcng_$i 2>/dev/null || varsym -s rcng_$i
	    done
	fi
	;;
*)
	echo "usage: rcrun action rcscript1 ..."
	echo "  where 'action' is one of:"
	echo "    start|stop|restart|rcvar|list|forcestart|faststart|onestart"
	echo "    disable|enable"
	;;
esac
