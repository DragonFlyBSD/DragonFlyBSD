#!/bin/tcsh
#

umask 022
#limit maxproc 200
#limit datasize unlimit

set userresources = "$HOME/.Xresources"
set usermodmap = "$HOME/.Xmodmap"
set sysresources = "/usr/pkg/xorg/lib/X11/xinit/.Xresources"
set sysmodmap = "/usr/pkg/xorg/lib/X11/xinit/.Xmodmap"

cd $HOME

if ( -f $sysresources ) then
    xrdb -merge $sysresources
endif

if ( -f $sysmodmap ) then
    xmodmap $sysmodmap
endif

if ( -f $userresources ) then
    xrdb -merge $userresources
endif

if ( -f $usermodmap ) then
    xmodmap $usermodmap
endif

xset m 2 1
(limit core 0; xscreensaver -nosplash) &
asclock -shape -position -60+345 -led green -noblink &
xclock -geometry 80x80-1+250 &
eval `/usr/bin/ssh-agent -c`
setenv TERM xterm

# start window manager here
#
/usr/pkg/bin/fvwm2 -f ~/.fvwm/.fvwm2rc
#startkde

/bin/kill $SSH_AGENT_PID
jobs -l >.tmp.$$
/bin/kill `awk '{ print $3; }' < .tmp.$$`
rm -f .tmp.$$
