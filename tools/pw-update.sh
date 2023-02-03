#!/bin/sh
#
# Copyright (c) 2020 The DragonFly Project.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of The DragonFly Project nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
# COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

# Exit if any untested command fails in non-interactive mode
set -e
# Exit when an undefined variable is referenced
set -u

# Usage: add_users <etcdir> <master.passwd> <group>
#
# Add new users and groups in <etcdir> according to the given <master.passwd>
# and <group> files.
#
# NOTE: Existing users and groups are not modified.
#
add_users() {
	local etcdir="$1"
	local fpasswd="$2"
	local fgroup="$3"
	local _name _pw _uid _gid _gids _group item
	local _class _change _expire _gecos _home _shell _members

	echo "===> Adding new users ..."
	_gids=""
	while IFS=':' read -r _name _pw _uid _gid _class \
			_change _expire _gecos _home _shell; do
		case ${_name} in
		'' | \#*) continue ;;
		esac
		if pw -V ${etcdir} usershow ${_name} -q >/dev/null; then
			continue
		fi
		echo "   * ${_name}: ${_uid}, ${_gid}, ${_gecos}, ${_home}, ${_shell}"

		_group=${_gid}
		if ! pw -V ${etcdir} groupshow ${_gid} -q >/dev/null; then
			# Primary group doesn't exist yet, so first assign to
			# the 'nogroup' group, and then adjust it after
			# creating the group.
			_group="nogroup"
			_gids="${_gids} ${_name}:${_gid}"
		fi

		# NOTE: The shell field can be empty (e.g., user 'toor') and
		#       would default to '/bin/sh'.
		# NOTE: Use '-o' option to allow to create user of duplicate
		#       UID, which is required by the 'toor' user (same UID
		#       as 'root').
		pw -V ${etcdir} useradd ${_name} \
			-o \
			-u ${_uid} \
			-g ${_group} \
			-d "${_home}" \
			-s "${_shell}" \
			-L "${_class}" \
			-c "${_gecos}"
	done < ${fpasswd}

	echo "===> Adding new groups ..."
	while IFS=':' read -r _name _pw _gid _members; do
		case ${_name} in
		'' | \#*) continue ;;
		esac
		if pw -V ${etcdir} groupshow ${_name} -q >/dev/null; then
			continue
		fi
		echo "   * ${_name}: ${_gid}, ${_members}"
		pw -V ${etcdir} groupadd ${_name} -g ${_gid} -M "${_members}"
	done < ${fgroup}

	echo "===> Adjusting the group of new users ..."
	for item in ${_gids}; do
		_name=${item%:*}
		_gid=${item#*:}
		echo "   * ${_name}: ${_gid}"
		pw -V ${etcdir} usermod ${_name} -g ${_gid}
	done
}

# Usage: update_user <user> <etcdir> <master.passwd>
#
# Update an existing user in <etcdir> according to the given <master.passwd>.
#
update_user() {
	local user="$1"
	local etcdir="$2"
	local fpasswd="$3"
	local _line
	local _name _pw _uid _gid _class _change _expire _gecos _home _shell

	_line=$(grep "^${user}:" ${fpasswd}) || true
	if [ -z "${_line}" ]; then
		echo "ERROR: no such user '${user}'" >&2
		exit 1
	fi

	echo "${_line}" | {
		IFS=':' read -r _name _pw _uid _gid _class \
			_change _expire _gecos _home _shell
		echo "===> Updating user ${user} ..."
		echo "   * ${_name}: ${_uid}, ${_gid}, ${_gecos}, ${_home}, ${_shell}"
		pw -V ${etcdir} usermod ${user} \
			-u ${_uid} \
			-g ${_gid} \
			-d ${_home} \
			-s ${_shell} \
			-L "${_class}" \
			-c "${_gecos}"
	}
}

# Usage: update_group <group> <etcdir> <group>
#
# Update an existing group in <etcdir> according to the given <group> file.
#
update_group() {
	local group="$1"
	local etcdir="$2"
	local fgroup="$3"
	local _line
	local _name _pw _gid _members

	_line=$(grep "^${group}:" ${fgroup}) || true
	if [ -z "${_line}" ]; then
		echo "ERROR: no such group '${group}'" >&2
		exit 1
	fi

	echo "${_line}" | {
		IFS=':' read -r _name _pw _gid _members
		echo "===> Updating group ${group} ..."
		echo "   * ${_name}: ${_gid}, ${_members}"
		pw -V ${etcdir} groupmod ${group} -g ${_gid} -M "${_members}"
	}
}

usage() {
	cat > /dev/stderr << _EOF_
Add/update users and groups.

Usage: ${0##*/} -d <etc-dir> -g <group-file> -p <master.passwd-file>
	[-G group] [-U user]

_EOF_

	exit 1
}

ETC_DIR=
GROUP_FILE=
PASSWD_FILE=
UPDATE_GROUP=
UPDATE_USER=

while getopts :d:G:g:hp:U: opt; do
	case ${opt} in
	d)
		ETC_DIR=${OPTARG}
		;;
	G)
		UPDATE_GROUP=${OPTARG}
		;;
	g)
		GROUP_FILE=${OPTARG}
		;;
	p)
		PASSWD_FILE=${OPTARG}
		;;
	U)
		UPDATE_USER=${OPTARG}
		;;
	h | \? | :)
		usage
		;;
	esac
done

shift $((OPTIND - 1))
[ $# -eq 0 ] || usage
[ -n "${ETC_DIR}" ] || usage
[ -n "${GROUP_FILE}" ] || usage
[ -n "${PASSWD_FILE}" ] || usage

if [ -z "${UPDATE_GROUP}" ] && [ -z "${UPDATE_USER}" ]; then
	add_users "${ETC_DIR}" "${PASSWD_FILE}" "${GROUP_FILE}"
else
	if [ -n "${UPDATE_GROUP}" ]; then
		update_group "${UPDATE_GROUP}" "${ETC_DIR}" "${GROUP_FILE}"
	fi
	if [ -n "${UPDATE_USER}" ]; then
		update_user "${UPDATE_USER}" "${ETC_DIR}" "${PASSWD_FILE}"
	fi
fi
