#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/test.sh,v 1.2 2005/02/01 11:19:37 okumoto Exp $

. ./env.sh

DIR="basic variables"

case $1 in
test)
	for d in $DIR; do
		std_action $d $1
	done
	;;

compare)
	for d in $DIR; do
		std_action $d $1
	done
	;;

diff)
	for d in $DIR; do
		std_action $d $1
	done
	;;

update)
	for d in $DIR; do
		std_action $d $1
	done
	;;

run)
	for d in $DIR; do
		std_action $d $1
	done
	;;

clean)
	for d in $DIR; do
		std_action $d $1
	done
	;;

*)
	echo "Usage: $0 run | compare | update | clean"
	;;
esac

