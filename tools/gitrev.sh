#!/bin/sh

# We might be run during buildkernel/world, where PATH is
# limited.  To reach git, we need to add the directories
# git might be located in.  Not a very nice solution, but
# it works well enough.
PATH=$PATH:/usr/pkg/bin:/usr/local/bin

srcdir=${1:-$(dirname $0)}

[ -n "$srcdir" ] && cd "$srcdir"

if ! git version >/dev/null 2>&1 ||
	! cd "$(dirname "$0")" ||
	! git rev-parse --git-dir >/dev/null 2>&1
then
	exit 0
fi

v=$(git describe --abbrev=5 2>/dev/null || git rev-parse --short HEAD)
v=$(echo "$v" | sed -e 'y/-/./')

# Takes too long when running over NFS
#git update-index -q --refresh
#[ -z "$(git diff-index --name-only HEAD --)" ] || v="$v*"

echo "$v"
exit 0
