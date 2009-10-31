#!/bin/sh

CDIR=$(dirname $0)
CNAME=$(basename $0)

# XXX clang is called only for "cc" and "gcc" for now
#
if [ "${CCVER}" = "clang" ]; then
	if [ "${CNAME}" = "cpp" ]; then
		exec ${CDIR}/../gcc41/cpp "$@"
	elif [ "${CNAME}" = "c++" -o "${CNAME}" = "g++" ]; then
		exec ${CDIR}/../gcc41/c++ "$@"
	else
		INCOPT="-nobuiltininc -nostdinc \
		    -isysroot @@INCPREFIX@@ \
		    -isystem /usr/include \
		    -isystem /usr/libdata/gcc41 \
		    -isystem /usr/include/c++/4.1"
	fi
fi

. /etc/defaults/compilers.conf
[ -f /etc/compilers.conf ] && . /etc/compilers.conf

CUSTOM_CC=`eval echo \$\{${CCVER}_CC\}`
CUSTOM_CFLAGS=`eval echo \$\{${CCVER}_CFLAGS\}`
CUSTOM_CXX=`eval echo \$\{${CCVER}_CXX\}`
CUSTOM_CXXFLAGS=`eval echo \$\{${CCVER}_CXXFLAGS\}`
CUSTOM_CPP=`eval echo \$\{${CCVER}_CPP\}`
CUSTOM_CPPFLAGS=`eval echo \$\{${CCVER}_CPPFLAGS\}`
CUSTOM_VERSION=`eval echo \$\{${CCVER}_VERSION\}`

if [ "${CUSTOM_VERSION}" != "" -a "$1" = "-dumpversion" ]; then
	echo ${CUSTOM_VERSION}
elif [ "${CNAME}" = "cc" -o "${CNAME}" = "gcc" ]; then
	exec ${CUSTOM_CC} ${INCOPT} ${CUSTOM_CFLAGS} "$@"
elif [ "${CNAME}" = "c++" -o "${CNAME}" = "g++" ]; then
	exec ${CUSTOM_CXX} ${INCOPT} ${CUSTOM_CXXFLAGS} "$@"
elif [ "${CNAME}" = "cpp" ]; then
	exec ${CUSTOM_CPP} ${INCOPT} ${CUSTOM_CPPFLAGS} "$@"
fi
