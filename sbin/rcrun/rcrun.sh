#!/bin/sh
#
# rcng command
#
# $DragonFly: src/sbin/rcrun/rcrun.sh,v 1.3 2003/12/12 00:14:28 dillon Exp $

OS=`sysctl -n kern.ostype`

dostart()
{
    arg=$1
    shift

    for i in $@; do
	case X`varsym -s -q rcng_$i` in
	Xrunning*)
	    echo "$i has already been started"
	    ;;
	*)
	    _return=0
	    for j in `rcorder -k $OS -o $i /etc/rc.d/*`; do
		need=1
		for k in `rcorder -p $j`; do
		    if [ $k = $i ]; then
			need=0
		    else
			state=`varsym -s -q rcng_$k`
			case X$state in
			Xrunning*)
			    ;;
			Xdisabled*)
			    ;;
			*)
			    echo "$i depends on $k, current state: $state"
			    _return=1
			    ;;
			esac
		    fi
		done
	    done
	    # $j contains the last dependancy, which we run
	    #
	    if [ X$j = X ]; then
		echo "Unable to find keyword $i"
	    elif [ $_return = 0 ]; then
		echo "Running $j $arg"
		(cd /etc/rc.d; sh $j $arg)
		case X`varsym -s -q rcng_$i` in
		Xdisabled*)
		    echo "$i is disabled, enable in rc.conf first or use rcforce"
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
stop)
	for i in $@; do
	    j=`rcorder -k $OS -o $i /etc/rc.d/* | tail -1`
	    if [ X$j = X ]; then
		echo "Unable to find keyword $i"
	    else
		(cd /etc/rc.d; sh $j stop)
	    fi
	done
	;;
restart)
	for i in $@; do
	    j=`rcorder -k $OS -o $i /etc/rc.d/* | tail -1`
	    if [ X$j = X ]; then
		echo "Unable to find keyword $i"
	    else
		(cd /etc/rc.d; sh $j restart)
	    fi
	done
	;;
rcvar)
	for i in $@; do
	    if [ X$j = X ]; then
		echo "Unable to find keyword $i"
	    else
		(cd /etc/rc.d; sh $j rcvar)
	    fi
	done
	;;
list)
	if [ X$@ = X ]; then
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
	echo "$0 {start|stop|restart|rcvar|list|forcestart|faststart} <rcng_list>"
	;;
esac

