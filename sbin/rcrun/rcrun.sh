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

    for tgt in $@; do
	case X`varsym -s -q rcng_$tgt` in
	Xrunning*)
	    echo "$tgt has already been started"
	    ;;
	Xconfigured*)
	    echo "$tgt has already been configured"
	    ;;
	*)
	    _return=0
	    buildrclist $tgt
	    for dep in $rclist; do
		need=1
		for dep_prvd in `rcorder -p $dep`; do
		    if [ $dep_prvd = $tgt ]; then
			need=0
		    else
			state=`varsym -s -q rcng_$dep_prvd`
			case X$state in
			Xrunning*|Xconfigured*|Xirrelevant*|Xdisabled*)
			    ;;
			*)
			    echo "$tgt depends on $dep_prvd, current state: $state"
			    _return=1
			    ;;
			esac
		    fi
		done
	    done
	    # $dep contains the last dependency, which we run
	    #
	    if [ X$dep = X ]; then
		echo "Unable to find keyword $tgt"
	    elif [ $_return = 0 ]; then
		echo "Running $dep $arg"
		(sh $dep $arg)
		case X`varsym -s -q rcng_$tgt` in
		Xdisabled*)
		    echo "$tgt is disabled, enable in rc.conf first or use rcforce/rcone"
		    ;;
		Xfailed*)
		    echo "$tgt has failed to start"
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
	for tgt in $@; do
	    buildrclist $tgt
	    dep=`echo "$rclist" | tail -1`
	    if [ X$dep = X ]; then
		echo "Unable to find keyword $tgt"
	    else
		(sh $dep stop)
	    fi
	done
	;;
restart)
	for tgt in $@; do
	    buildrclist $tgt
	    dep=`echo "$rclist" | tail -1`
	    if [ X$dep = X ]; then
		echo "Unable to find keyword $tgt"
	    else
		(sh $dep restart)
	    fi
	done
	;;
disable|enable)
	if [ "$arg" = "enable" ]; then
	    mode=YES
	else
	    mode=NO
	fi
	for tgt in $@; do
	    buildrclist $tgt
	    dep=`echo "$rclist" | tail -1`
	    if [ X$dep = X ]; then
		echo "Unable to find provider id $tgt"
	    elif [ `varsym -s -q rcng_$tgt` = "$mode" ]; then
		echo "$tgt is already $mode"
	    else
		vars=`(sh $dep rcvar) 2>/dev/null | grep = | sed -e 's/\\$//g' | sed -e 's/=.*//g'`
		cp /etc/rc.conf /etc/rc.conf.bak
		if [ $arg = disable ]; then
		    rcstop $tgt
		fi
		for k in $vars; do
		    rm -f /etc/rc.conf.$$
		    ( egrep -v "# rcrun enable ${k}$" /etc/rc.conf; printf "${k}=${mode}\t# rcrun enable ${k}\n" ) > /etc/rc.conf.$$
		    mv -f /etc/rc.conf.$$ /etc/rc.conf
		    echo "added/modified: ${k}=${mode}"
		done
		if [ $arg = enable ]; then
		    rcstart $tgt
		fi
	    fi
	done
	;;
rcvar)
	for tgt in $@; do
	    buildrclist $tgt
	    dep=`echo "$rclist" | tail -1`
	    if [ X$dep = X ]; then
		echo "Unable to find provider id $tgt"
	    else
		(sh $dep rcvar)
	    fi
	done
	;;
list)
	if [ "X$*" = X ]; then
	    for tgt in `varsym -a -s | egrep '^rcng_'`; do
		echo $tgt
	    done
	else
	    for tgt in $@; do
		varsym -s rcng_$tgt 2>/dev/null || varsym -s rcng_$tgt
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
