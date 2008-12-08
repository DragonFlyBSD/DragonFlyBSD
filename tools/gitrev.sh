#!/bin/sh

if ! which git >/dev/null 2>&1 ||
	! cd "$(dirname "$0")" ||
	! git rev-parse --git-dir >/dev/null 2>&1
then
	# XXX get version from newvers.sh?
	echo "unknown"
	exit 0
fi

v=$(git describe --abbrev=4 HEAD 2>/dev/null || git rev-parse --short HEAD)
git update-index -q --refresh
[ -z "$(git diff-index --name-only HEAD --)" ] || v="$v-dirty"

v=$(echo "$v" | sed -e 's/-/./g;s/^v//;')

echo "$v"
exit 0
