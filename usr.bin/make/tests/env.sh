#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/Attic/env.sh,v 1.4 2005/02/25 11:57:32 okumoto Exp $

MAKE=/usr/bin/make

#
# We can't check a file into cvs without a DragonFly RCS Id tag, so
# we need to remove it before we compare. 
#
hack_cmp()
{
	if [ -f $1 ]; then
		cat $1 |\
			sed -e '1d' |\
			diff -q - $2 \
			1> /dev/null 2> /dev/null
		return $?
	else
		return 1	# FAIL
	fi
}

#
# We can't check a file into cvs without a DragonFly RCS Id tag, so
# we need to remove it before we compare. 
#
hack_diff()
{
	echo diff $1 $2
	if [ -f $1 ]; then
		cat $1 |\
			sed -e '1d' |\
			diff - $2
		return $?
	else
		return 1	# FAIL
	fi
}

std_compare()
{
	hack_cmp expected.stdout stdout || FAIL="stdout "$FAIL
	hack_cmp expected.stderr stderr || FAIL="stderr "$FAIL
	hack_cmp expected.status status || FAIL="status "$FAIL

	if [ -z "$FAIL" ]; then
		:
	else
		echo "Test failed ( $FAIL) `pwd`"
	fi
}

std_diff()
{
	$0 test
	echo "------------------------"
	echo "- `pwd`"
	echo "------------------------"
	hack_diff expected.stdout stdout
	hack_diff expected.stderr stderr
	hack_diff expected.status status
}

std_update()
{
	$0 test
	[ -f expected.stdout ] || echo '$'DragonFly'$' > expected.stdout
	[ -f expected.stderr ] || echo '$'DragonFly'$' > expected.stderr
	[ -f expected.status ] || echo '$'DragonFly'$' > expected.status
	sed -e '2,$d' < expected.stdout > new.expected.stdout
	sed -e '2,$d' < expected.stderr > new.expected.stderr
	sed -e '2,$d' < expected.status > new.expected.status
	cat stdout >> new.expected.stdout
	cat stderr >> new.expected.stderr
	cat status >> new.expected.status
	mv new.expected.stdout expected.stdout
	mv new.expected.stderr expected.stderr
	mv new.expected.status expected.status
}

std_clean()
{
	rm -f Makefile
	rm -f stdout
	rm -f stderr
	rm -f status
}

std_run()
{
	$0 test
	$0 compare
	$0 clean
}

#
# Run 
#
eval_subdir_cmd()
{
	local START_BASE

	if [ ! -d $1 ]; then
		echo "Test directory '$1' missing in directory '$START_BASE'"
		return
	fi

	if [ ! -f $1/test.sh ]; then
		echo "Test script missing in directory '$START_BASE/$1'"
		return
	fi

	START_BASE=${START_BASE}/$1
	(cd $1; sh test.sh $2)
}

#
# Note: Uses global variable $DIR which might be assigned by
#	the script which sourced this file.
#
eval_cmd()
{
	if [ "${DIR}" ]; then
		case $1 in
		test|compare|diff|update|clean|run)
			for d in $DIR; do
				eval_subdir_cmd $d $1
			done
			;;
		*)
			echo "Usage: $0 run | compare | update | clean"
			;;
		esac
	else
		case $1 in
		test)
			run_test
			;;
		compare)
			std_compare
			;;
		diff)
			std_diff
			;;
		update)
			std_update
			;;
		run)
			std_run
			;;
		clean)
			std_clean
			;;
		*)
			echo "Usage: $0 run | compare | update | clean"
			;;
		esac
	fi
}

#
# Parse command line arguments.
#
args=`getopt m:v $*`
if [ $? != 0 ]; then
	echo 'Usage: ...'
	exit 2
fi
set -- $args
for i; do
	case "$i" in
	-m)
		MAKE="$2"
		shift
		shift
		;;
	-v)
		VERBOSE=1
		shift
		;;
	--)
		shift
		break
		;;
	esac
done

START_BASE=${START_BASE:-.}

export MAKE
export VERBOSE
export START_BASE
