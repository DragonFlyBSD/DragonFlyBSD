#!/bin/sh
#
# $DragonFly: doc/share/pgpkeys/addkey.sh,v 1.1.1.1 2004/04/02 09:36:37 hmp Exp $
# $DragonFly: doc/share/pgpkeys/addkey.sh,v 1.1.1.1 2004/04/02 09:36:37 hmp Exp $
#

LANG=C; export LANG
unset LC_ALL
unset LC_MESSAGES

me="$1"
if [ -z "${me}" ]; then
    me=$(id -nu)
else
    shift
fi

id="$@"
if [ -z "${id}" ]; then
    id="${me}@dragonfly.org"
fi

gpg=$(which gpg)
if [ ! -x "${gpg}" ]; then
    echo "GnuPG does not seem to be installed" >/dev/stderr
    exit 1
fi

echo "Retrieving key..."
keylist=$(gpg --list-keys ${id})
echo "${keylist}" | grep '^pub'
id=$(echo "${keylist}" | awk '/^pub/ { print $2 }' | sed 's%.*/%%' | sort -u)
id=$(echo $id)
if [ "${#id}" -lt 8 ]; then
    echo "Invalid key ID." >/dev/stderr
    exit 1
elif [ "${#id}" -gt 8 ]; then
    echo "WARNING: Multiple keys; exporting all.  If this is not what you want," >/dev/stderr
    echo "WARNING: you should specify a key ID on the command line." >/dev/stderr
fi
fp=$(gpg --fingerprint ${id})
[ $? -eq 0 ] || exit 1
key=$(gpg --no-version --armor --export ${id})
[ $? -eq 0 ] || exit 1

keyfile="${me}.key"
if [ -f "${keyfile}" ]; then
    rcsid=$(grep '^<!-- \$Free.*-->$' "${keyfile}")
fi
if [ -z "${rcsid}" ]; then
    rcsid='<!-- $''FreeBSD''$ -->'
fi
echo "Generating ${keyfile}..."
(
    echo "${rcsid}"
    echo '<!--'
    echo "sh $0 ${me} ${id};"
    echo '-->'
    echo '<programlisting role="pgpfingerprint"><![CDATA['
    echo "${fp}"
    echo ']]></programlisting>'
    echo '<programlisting role="pgpkey"><![CDATA['
    echo "${key}"
    echo ']]></programlisting>'
) >"${keyfile}"

echo "Adding key to entity list..."
mv pgpkeys.ent pgpkeys.ent.orig || exit 1
(
    cat pgpkeys.ent.orig
    printf '<!ENTITY pgpkey.%.*s SYSTEM "%s">' 16 "${me}" "${keyfile}"
) | sort -u >pgpkeys.ent

echo
echo "Unless you are already listed there, you should now add the"
echo "following text to chapter.sgml in the appropriate position in"
echo "the developer section (unless this is a role key or you are a"
echo "core member.)  Remember to keep the list sorted by last name!"
echo
echo "    <sect2 id=\"pgpkey-${me}\">"
echo "      <title>&a.${me};</title>"
echo "      &pgpkey.${me};"
echo "    </sect2>"
echo
echo "Don't forget to 'cvs add ${keyfile}' if this is a new entry,"
echo "and check your diffs before committing!"
