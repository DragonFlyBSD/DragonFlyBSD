#!/bin/sh

# $DragonFly: src/usr.bin/make/tests/Attic/env.sh,v 1.1 2005/02/01 07:07:14 okumoto Exp $

MAKE=/usr/bin/make

std_action() {
	if [ -d $1 ]; then
		(cd $d; ./test.sh $2)
	else
		echo Test directory $1 missing in `pwd`
	fi
}

std_compare() {
	if cmp -s expected.stdout stdout && \
	   cmp -s expected.stderr stderr && \
	   cmp -s expected.status status; then
		true
	else
		echo Test failed `pwd`
	fi
}

std_diff() {
	diff -u expected.stdout stdout
	diff -u expected.stderr stderr
	diff -u expected.status status
}

std_update() {
	$0 test
	cp stdout expected.stdout
	cp stderr expected.stderr
	cp status expected.status
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

