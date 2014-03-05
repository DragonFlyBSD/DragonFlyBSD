#!/bin/sh

# installer - start installer frontend and backend as per pfi config.
# $Id: installer,v 1.20 2005/04/13 03:32:16 cpressey Exp $

### SUBS ###

background_backend()
{
	RENDEZVOUS=$1
	TRANSPORT=$2
	$pfi_backend -r $RENDEZVOUS -t $TRANSPORT >/dev/null 2>&1
	RESULT=$?
	case "$RESULT" in
	0)
		;;
	5)
		$pfi_shutdown_command
		;;
	*)
		;;
	esac
}

installer_start()
{
	# Console start sequence:
	# - Backend (and all other logging) goes to console (ttyv0)
	# - curses frontend starts on ttyv1.
	# - Uses vidcontrol -s 2 to switch to ttyv1 once the frontend is up.

	echo -n "Starting installer.  "

	if [ -r /etc/defaults/pfi.conf ]; then
		. /etc/defaults/pfi.conf
	fi

	if [ -r /etc/pfi.conf ]; then
		echo "Reading /etc/pfi.conf ..."
		. /etc/pfi.conf
	else
		echo "/etc/pfi.conf not found, starting interactive install."
	fi

	# We can set up any install variables and such
	# here by examining pfi_* variables.

	if [ "X$pfi_run" != "X" ]; then
		pfi_frontend=none
		$pfi_run
	fi

	case "X$pfi_dfui_transport" in
	Xnpipe)
		RENDEZVOUS="installer"
		;;
	Xtcp)
		RENDEZVOUS="9999"
		;;
	*)
		echo "Unsupported DFUI transport '$pfi_dfui_transport'."
		return
		;;
	esac

	if [ "X$pfi_frontend" = "Xauto" ]; then
		if [ "X$DISPLAY" = "X" ]; then
			if [ "X$LIVECD" = "X" ]; then
				pfi_frontend="curseslog"
			else
				pfi_frontend="cursesvty"
			fi
		else
			pfi_frontend="cursesx11"
		fi
	fi

	case "X$pfi_frontend" in
	Xqt)
		$pfi_backend -r $RENDEZVOUS -t $pfi_dfui_transport
		RESULT=$?
		;;
	Xcgi)
		$pfi_backend -r $RENDEZVOUS -t $pfi_dfui_transport
		RESULT=$?
		;;
	Xcursesvty)
		ps auwwwxxx > /tmp/ps.txt
		if grep -q dfuife_curses /tmp/ps.txt; then
			# Frontend is already running.
		else
			ESCDELAY=$pfi_curses_escdelay \
			    /usr/sbin/dfuife_curses \
				-r $RENDEZVOUS \
				-t $pfi_dfui_transport \
				-b /usr/share/installer/fred.txt \
			    2>/dev/ttyv0 <$TTY >$TTY &
		fi
		rm -f /tmp/ps.txt
		sleep 1
		vidcontrol -s 2 </dev/ttyv0
		$pfi_backend \
		    -o $SOURCE_DIR \
		    -r $RENDEZVOUS \
		    -t $pfi_dfui_transport
		RESULT=$?
		sleep 1
		killall dfuife_curses
		vidcontrol -s 1 </dev/ttyv0
		;;
	Xcurseslog)
		ps auwwwxxx > /tmp/ps.txt
		if grep -q dfuife_curses /tmp/ps.txt; then
			# Frontend is already running.
		else
			ESCDELAY=$pfi_curses_escdelay \
			    /usr/sbin/dfuife_curses \
				-r $RENDEZVOUS \
				-t $pfi_dfui_transport \
				-b /usr/share/installer/fred.txt \
			    2>/tmp/dfuife_curses.log <$TTY >$TTY &
		fi
		rm -f /tmp/ps.txt
		sleep 1
		$pfi_backend \
		    -o $SOURCE_DIR \
		    -r $RENDEZVOUS \
		    -t $pfi_dfui_transport \
		    >/dev/null 2>&1
		RESULT=$?
		sleep 1
		killall -q dfuife_curses
		;;
	Xcursesx11)
		ps auwwwxxx > /tmp/ps.txt
		if grep -q dfuife_curses /tmp/ps.txt; then
			echo "Frontend is already running"
		else
			ESCDELAY=$pfi_curses_escdelay \
			/usr/sbin/dfuife_curses \
			-r $RENDEZVOUS \
			-t $pfi_dfui_transport \
			-b /usr/share/installer/fred.txt \
			>$TTY_INST <$TTY_INST 2>&1 &
		fi
		rm -f /tmp/ps.txt
		sleep 1
		$pfi_backend -r $RENDEZVOUS -t $pfi_dfui_transport
		RESULT=$?
		sleep 1
		killall dfuife_curses
		;;
	Xnone)
		RESULT=0
		;;
	*)
		echo "Unknown installer frontend '$pfi_frontend'."
		return
		;;
	esac

	case "$RESULT" in
	0)
		;;
	5)
		$pfi_shutdown_command
		;;
	*)
		;;
	esac
}

### MAIN ###

if [ $# -gt 1 ]; then
	echo "usage: installer [source_directory]"
	exit 1
fi

# Check if we are booted from a LiveCD, DVD etc. ttyv1 isn't configured in
# this case, so use that as a clue for now. Also, we have to use /dev/console
# in vkernels.
#
_ttyv1=`grep -w "^ttyv1" /etc/ttys`
if [ "`sysctl -n kern.vmm_guest`" = "vkernel" ]; then
	SOURCE_DIR=/
	TTY=/dev/console
elif [ -z "$_ttyv1" ]; then
	LIVECD=YES
	SOURCE_DIR=/
	TTY=/dev/ttyv1
elif [ $# = 1 -a -d $1 ]; then
	SOURCE_DIR=$1/
	TTY=/dev/`w | awk '{ print $2 }' | tail -n1`
else
	SOURCE_DIR=/
	TTY=/dev/`w | awk '{ print $2 }' | tail -n1`
fi

ps auwwwxxx > /tmp/ps.txt
if grep -q dfuibe_installer /tmp/ps.txt; then
	# Installer is already running. Log in normally.
	rm -f /tmp/ps.txt
else
	rm -f /tmp/ps.txt
	installer_start
fi

### END of installer ###
