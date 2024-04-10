#!/bin/tcsh
#

umask 022
#limit maxproc 200
#limit datasize unlimit

set userresources = "$HOME/.Xresources"
set usermodmap = "$HOME/.Xmodmap"

cd $HOME

if ( -f $userresources ) then
    xrdb -merge $userresources
endif

if ( -f $usermodmap ) then
    xmodmap $usermodmap
endif

xinput set-prop 'SynPS/2 Synaptics TouchPad' 'libinput Tapping Enabled' 1

xset m 2 1
(limit core 0; xscreensaver -nosplash) &
asclock -shape -position -60+345 -led green -noblink &
xclock -geometry 80x80-1+250 &
eval `/usr/bin/ssh-agent -c`
setenv TERM xterm

# start window manager here
#
/usr/local/bin/fvwm3 -f ~/.fvwm/.fvwm2rc
#startkde

/bin/kill $SSH_AGENT_PID
jobs -l >.tmp.$$
/bin/kill `awk '{ print $3; }' < .tmp.$$`
rm -f .tmp.$$
