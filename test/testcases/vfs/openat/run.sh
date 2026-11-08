#!/bin/sh
#
# Run the testcase in a scratch directory below /tmp.  The tests are
# intended for tmpfs, so warn when /tmp is mounted with a different
# filesystem.
#
set -u

testdir=$(pwd)

fstype=$(df -T /tmp | awk 'NR == 2 { print $2 }')
if [ "$fstype" != "tmpfs" ]; then
	echo "WARNING: /tmp is not a tmpfs mount (found ${fstype:-unknown});" \
	    "the test is intended to run on tmpfs" >&2
fi

scratch=$(mktemp -d /tmp/vfs.XXXXXX) || exit 1

cleanup()
{
	rc=$?
	trap - EXIT HUP INT TERM
	cd /
	rm -rf "$scratch"
	exit $rc
}
trap cleanup EXIT HUP INT TERM

cd "$scratch" || exit 1
mkdir sub || exit 1
echo x > reg || exit 1

"$testdir/openat"
