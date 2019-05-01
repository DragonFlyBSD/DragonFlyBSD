#!/bin/sh

if [ $# -ne 2 ]
then
	echo "$0 [unpriv] [restore] <DESTDIR>" >&2
	echo "   create/remove conveniece symlinks for base private" >&2
	echo "   libraries and headers:" >&2
	echo "      libedit ncurses openssl" >&2
	echo "   if new software package needs runtime for tests or does" >&2
	echo "   not respect -rpath /lib/priv -rpath /usr/lib/priv flags" >&2
	echo "   use export LD_LIBRARY_PATH=/lib/priv:/usr/lib/priv"
	exit 1
fi

DESTDIR=$2

set -e

if [ "$1" == "unpriv" ]
then
	echo "Creating symlinks:"
	ln -sv priv/ncurses  "$DESTDIR"/usr/include/ncurses
	ln -sv priv/openssl  "$DESTDIR"/usr/include/openssl
	ln -sv priv/readline "$DESTDIR"/usr/include/readline
	ln -sv priv/histedit.h "$DESTDIR"/usr/include/histedit.h
	# ncurses
	ln -sv priv/libprivate_ncurses.a   "$DESTDIR"/usr/lib/libncurses.a
	ln -sv priv/libprivate_ncurses.so  "$DESTDIR"/usr/lib/libncurses.so
	ln -sv priv/libprivate_ncursesw.a  "$DESTDIR"/usr/lib/libncursesw.a
	ln -sv priv/libprivate_ncursesw.so "$DESTDIR"/usr/lib/libncursesw.so
	# LibreSSL
	ln -sv priv/libprivate_crypto.a  "$DESTDIR"/usr/lib/libcrypto.a
	ln -sv priv/libprivate_crypto.so "$DESTDIR"/usr/lib/libcrypto.so
	ln -sv priv/libprivate_ssl.a     "$DESTDIR"/usr/lib/libssl.a
	ln -sv priv/libprivate_ssl.so    "$DESTDIR"/usr/lib/libssl.so
	# libedit
	ln -sv priv/libprivate_edit.a  "$DESTDIR"/usr/lib/libedit.a
	ln -sv priv/libprivate_edit.so "$DESTDIR"/usr/lib/libedit.so
	# warn
	echo "Done. Later DO NOT forget to run '$0 restore'"
fi

if [ "$1" == "restore" ]
then
	echo "Cleaning symlinks:"
	rm -fv "$DESTDIR"/usr/include/ncurses
	rm -fv "$DESTDIR"/usr/include/openssl
	rm -fv "$DESTDIR"/usr/include/readline
	rm -fv "$DESTDIR"/usr/include/histedit.h
	# ncurses
	rm -fv "$DESTDIR"/usr/lib/libncurses.a
	rm -fv "$DESTDIR"/usr/lib/libncurses.so
	rm -fv "$DESTDIR"/usr/lib/libncursesw.a
	rm -fv "$DESTDIR"/usr/lib/libncursesw.so
	# LibreSSL
	rm -fv "$DESTDIR"/usr/lib/libcrypto.a
	rm -fv "$DESTDIR"/usr/lib/libcrypto.so
	rm -fv "$DESTDIR"/usr/lib/libssl.a
	rm -fv "$DESTDIR"/usr/lib/libssl.so
	# libedit
	rm -fv "$DESTDIR"/usr/lib/libedit.a
	rm -fv "$DESTDIR"/usr/lib/libedit.so
	# warn
	echo "Done."
fi
