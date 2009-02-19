#!/bin/sh
#
# Common code used run regression tests for usr.bin/make.
#
# $DragonFly: src/usr.bin/make/tests/common.sh,v 1.8 2005/03/02 10:55:37 okumoto Exp $

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
# Check if the test result is the same as the expected result.
#
# $1	Input file
#
hack_cmp()
{
	local EXPECTED RESULT
	EXPECTED="expected.$1"
	RESULT=$WORK_BASE/$1

	if [ -f $EXPECTED ]; then
		diff -q $EXPECTED $RESULT 1> /dev/null 2> /dev/null
		return $?
	else
		return 1	# FAIL
	fi
}

#
# Check if the test result is the same as the expected result.
#
# $1	Input file
#
hack_diff()
{
	local EXPECTED RESULT
	EXPECTED="expected.$1"
	RESULT=$WORK_BASE/$1

	echo diff $EXPECTED $RESULT
	if [ -f $EXPECTED ]; then
		diff $EXPECTED $RESULT
		return $?
	else
		return 1	# FAIL
	fi
}

#
# Default run_test() function.  It should be replace by the
# user specified regression test.
#
# Both the variables SRC_BASE WORK_BASE are available.
#
setup_test()
{
	echo "Missing setup_test() function in $SRC_BASE/test.sh"
}

#
# Default run_test() function.  It can be replace by the
# user specified regression test.
#
# Both the variables SRC_BASE WORK_BASE are available.
#
# Note: this function executes from a subshell.
#
run_test()
(
	cd $WORK_BASE;
        $MAKE_PROG 1> stdout 2> stderr
        echo $? > status
)

#
# Execute cmd in subdirectory. 
#
eval_subdir_cmd()
{
	local SRC_BASE WORK_BASE

	if [ ! -d $1 ]; then
		echo "Test directory '$1' missing in directory '$SRC_BASE'"
		return
	fi

	if [ ! -f $1/test.sh ]; then
		echo "Test script missing in directory '$SRC_BASE/$1'"
		return
	fi

	SRC_BASE=${SRC_BASE}/$1
	WORK_BASE=${WORK_BASE}/$1
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
		clean|compare|desc|diff|run|test|update|run_output)
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
				echo "$SRC_BASE: Test failed {$FAIL}"
			fi
			;;
		desc)
			echo -n "$SRC_BASE: "
			desc_test
			;;
		diff)
			sh $0 test
			echo "------------------------"
			echo "- $SRC_BASE"
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
		run_output)
			sh $0 test
			sh $0 compare
			echo "------------------------"
			echo "- stdout"
			echo "------------------------"
			cat $WORK_BASE/stdout
			echo "------------------------"
			echo "- stderr"
			echo "------------------------"
			cat $WORK_BASE/stderr
			echo "------------------------"
			echo "- status"
			echo "------------------------"
			echo -n "status ="
			cat $WORK_BASE/status
			sh $0 clean
			;;
		test)
			[ -d $WORK_BASE ] || mkdir -p $WORK_BASE
			setup_test
			run_test
			;;
		update)
			sh $0 test
			cp $WORK_BASE/stdout "."$SRC_BASE/expected.stdout
			cp $WORK_BASE/stderr "."$SRC_BASE/expected.stderr
			cp $WORK_BASE/status "."$SRC_BASE/expected.status
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
args=`getopt m:w:v $*`
if [ $? != 0 ]; then
	echo 'Usage: ...'
	exit 2
fi
set -- $args
for i; do
	case "$i" in
	-m)
		MAKE_PROG="$2"
		shift
		shift
		;;
	-w)
		WORK_BASE="$2"
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

SRC_BASE=${SRC_BASE:-""}
WORK_BASE=${WORK_BASE:-"/tmp/$USER.make.test"}
MAKE_PROG=${MAKE_PROG:-/usr/bin/make}

export MAKE_PROG
export VERBOSE
export SRC_BASE
export WORK_BASE
