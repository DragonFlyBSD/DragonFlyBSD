#!/bin/sh

# installer - start installer frontend and backend as per pfi config.
# $Id: installer,v 1.20 2005/04/13 03:32:16 cpressey Exp $

### SUBS ###
cleanup()
{
	killall -q dfuife_curses dfuibe_installer
}

background_backend()
{
	RENDEZVOUS=$1
	TRANSPORT=$2
	$pfi_backend \
	    -o $SOURCE_DIR \
	    -r $RENDEZVOUS \
	    -t $TRANSPORT \
	    >/dev/null 2>&1
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

is_serial()
{
	# Detect if we are currently connected via a serial console
	if [ "X`/usr/bin/kenv -q console`" == "Xcomconsole" ]; then
		return 0 # return success
	fi
	return 1
}

setup_term()
{
	# If TERM has not been set manually (ie: still 'dialup' or from /etc/ttyd), we ask the user what they want to use
	if [ "X`tty |cut -c6-9`" == "Xttyd" ]; then
		newterm=${TERM}
		if [ "X`/usr/bin/kenv smbios.bios.vendor`" == "XSeaBIOS" ]; then
			# installation on a virtial machine uses this type of simulated bios often, so we can do better than vt100 (eg:vt220-co, vt320-co, cons50-w)
			newterm="xterm"
		elif [ "X${TERM}" == "Xdialup" ]; then
			newterm="vt100"
		fi
		echo ""
		echo -n "What is your terminal type (provide value termcap name)? [${newterm}]: "
		read input
		[ "${input}" = '' ] && input=$newterm
		export TERM="${input}"
		echo "set new TERM=$TERM"
	fi
	TTY_BAUD=`stty speed`
	if [ $TTY_BAUD -lt 38400 ]; then
		echo -n "Your serial connection is quite slow ($TTY_BAUD), causing installer slow down. Continue Anyway ? [Y/n]: "
		read input
		[ "${input}" == "N" ] || [ "${input}" == "n" ] && exit 0
	fi
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
		if [ "X$TTY_INST" = "X" ]; then
			if $(is_serial); then
				setup_term
				RENDEZVOUS="installer"
				pfi_dfui_transport="npipe"
				TTY=$(tty)
				pfi_frontend="curseslog"
			else
				if $(is_installmedia); then
					TTY=/dev/ttyv1
					pfi_frontend="cursesvty"
				else
					TTY=$(tty)
					pfi_frontend="curseslog"
				fi
			fi
		else
			pfi_frontend="cursesx11"
		fi
	fi

	case "X$pfi_frontend" in
	Xqt)
		$pfi_backend \
		    -o $SOURCE_DIR \
		    -r $RENDEZVOUS \
		    -t $pfi_dfui_transport
		RESULT=$?
		;;
	Xcgi)
		$pfi_backend \
		    -o $SOURCE_DIR \
		    -r $RENDEZVOUS \
		    -t $pfi_dfui_transport
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
		$pfi_backend \
		    -o $SOURCE_DIR \
		    -r $RENDEZVOUS \
		    -t $pfi_dfui_transport
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

is_installmedia()
{
    local _ttyv1=$(grep -w "^ttyv1" /etc/ttys)
    local guest=$(sysctl -n kern.vmm_guest)

    #
    # ttyv1 isn't configured for the install media so use
    # that as a clue for now. Vkernels will be forced
    # to use 'curseslog' to avoid polluting its only
    # terminal.
    #
    [ "${guest}" = "vkernel" ] && return 1;

    if [ -z "${_ttyv1}" ]; then
	return 0	# Return success, it's a USB image, ISO etc.
    else
	return 1
    fi
}

### MAIN ###

if [ $# -gt 1 ]; then
	echo "usage: installer [source_directory]"
	exit 1
elif [ $# = 1 -a ! -d $1 ]; then
	echo "source_directory does not exist or is no directory"
	exit 1
fi

trap cleanup EXIT SIGTERM SIGINT

#
# Source directory for the installation
#
if [ $# = 1 ]; then
	SOURCE_DIR=$1
else
	SOURCE_DIR=/
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
