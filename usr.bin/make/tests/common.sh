#!/bin/sh
#
# Common code used run regression tests for usr.bin/make.
#
# $DragonFly: src/usr.bin/make/tests/common.sh,v 1.5 2005/03/01 22:01:16 okumoto Exp $

IDTAG='$'DragonFly'$'

#
# Output usage messsage.
#
print_usage()
{
	echo "Usage: $0 command"
	echo "	clean	- remove temp files"
	echo "	compare	- compare result of test to expected"
	echo "	desc	- print description of test"
	echo "	diff	- print diffs between results and expected"
	echo "	run	- run the {test, compare, clean}"
	echo "	test	- run test case"
	echo "	update	- update the expected with current results"
}

#
# Check if the test results are the same as the expected. 
# We can't check a file into CVS without a DragonFly RCS Id tag, so
# we need to remove it before we compare. 
#
# $1	Input file
#
#
hack_cmp()
{
	local EXPECTED RESULT
	EXPECTED="expected.$1"
	RESULT=$1

	if [ -f $EXPECTED ]; then
		cat $EXPECTED |\
			sed -e '1d' |\
			diff -q - $RESULT \
			1> /dev/null 2> /dev/null
		return $?
	else
		return 1	# FAIL
	fi
}

#
# We can't check a file into CVS without a DragonFly RCS Id tag, so
# we need to remove it before we compare. 
#
hack_diff()
{
	local EXPECTED RESULT
	EXPECTED="expected.$1"
	RESULT=$1

	echo diff $EXPECTED $RESULT
	if [ -f $EXPECTED ]; then
		cat $EXPECTED |\
			sed -e '1d' |\
			diff - $RESULT
		return $?
	else
		return 1	# FAIL
	fi
}

#
# Default run_test() function.  It should be replace by the
# user specified regression test.
#
run_test()
{
	echo "Missing run_test() function in $START_BASE/test.sh"
}

#
# Execute cmd in subdirectory. 
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
	(cd $1; sh ./test.sh $2)
}

#
# Note: Uses global variable $DIR which might be assigned by
#	the script which sourced this file.
#
eval_cmd()
{
	if [ "${DIR}" ]; then
		#
		# When there are subdirectories defined, recurse
		# down into them if the cmd is valid.
		#
		case $1 in
		clean|compare|desc|diff|run|test|update)
			for d in $DIR; do
				eval_subdir_cmd $d $1
			done
			;;
		*)
			print_usage;
			;;
		esac
	else
		#
		#
		#
		case $1 in
		clean)
			rm -f Makefile
			rm -f stdout
			rm -f stderr
			rm -f status
			;;
		compare)
			hack_cmp stdout || FAIL="stdout $FAIL"
			hack_cmp stderr || FAIL="stderr $FAIL"
			hack_cmp status || FAIL="status $FAIL"

			if [ ! -z "$FAIL" ]; then
				FAIL=`echo $FAIL`
				echo "$START_BASE: Test failed {$FAIL}"
			fi
			;;
		desc)
			echo -n "$START_BASE: "
			desc_test
			;;
		diff)
			sh $0 test
			echo "------------------------"
			echo "- $START_BASE"
			echo "------------------------"
			hack_diff stdout
			hack_diff stderr
			hack_diff status
			;;
		run)
			sh $0 test
			sh $0 compare
			sh $0 clean
			;;
		test)
			run_test
			;;
		update)
			sh $0 test
			# Create expected files if they don't exist.
			[ -f expected.stdout ] || echo $IDTAG > expected.stdout
			[ -f expected.stderr ] || echo $IDTAG > expected.stderr
			[ -f expected.status ] || echo $IDTAG > expected.status

			# Remove previous contents of expected files, and
			# replace them with the new contents.
			sed -e '2,$d' < expected.stdout > new.expected.stdout
			sed -e '2,$d' < expected.stderr > new.expected.stderr
			sed -e '2,$d' < expected.status > new.expected.status
			cat stdout >> new.expected.stdout
			cat stderr >> new.expected.stderr
			cat status >> new.expected.status
			mv new.expected.stdout expected.stdout
			mv new.expected.stderr expected.stderr
			mv new.expected.status expected.status
			;;
		*)
			print_usage
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
MAKE=${MAKE:-/usr/bin/make}
SCRATCH=${SCRATCH:-/tmp}

export MAKE
export VERBOSE
export START_BASE
