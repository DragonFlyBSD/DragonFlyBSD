#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/Attic/env.sh,v 1.2 2005/02/01 11:19:37 okumoto Exp $

MAKE=/usr/bin/make

std_action() {
	if [ -d $1 ]; then
		chmod +x $d/test.sh
		(cd $d; ./test.sh $2)
	else
		echo Test directory $1 missing in `pwd`
	fi
}

#
# We can't check a file into cvs without a DragonFly RCS Id tag, so
# we need to remove it before we compare. 
#
hack_cmp() {
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
hack_diff() {
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

std_compare() {
	hack_cmp expected.stdout stdout || FAIL="stdout "$FAIL
	hack_cmp expected.stderr stderr || FAIL="stderr "$FAIL
	hack_cmp expected.status status || FAIL="status "$FAIL

	if [ -z "$FAIL" ]; then
		:
	else
		echo "Test failed ( $FAIL) `pwd`"
	fi
}

std_diff() {
	$0 test
	echo "------------------------"
	echo "- `pwd`"
	echo "------------------------"
	hack_diff expected.stdout stdout
	hack_diff expected.stderr stderr
	hack_diff expected.status status
}

std_update() {
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

std_clean() {
	rm -f Makefile
	rm -f stdout
	rm -f stderr
	rm -f status
}

std_run() {
	$0 test
	$0 compare
	$0 clean
}

