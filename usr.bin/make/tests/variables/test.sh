#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/variables/test.sh,v 1.1 2005/02/01 07:07:14 okumoto Exp $

. ../env.sh

DIR="t?"

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

