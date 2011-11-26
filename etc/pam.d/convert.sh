#! /bin/sh
# 
# Copyright (c) 2005 The DragonFly Project.  All rights reserved.
#
# This code is derived from software contributed to The DragonFly Project
# by Joerg Sonnenberger <joerg@bec.de>
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
# $DragonFly: src/etc/pam.d/convert.sh,v 1.1 2005/07/22 18:20:43 joerg Exp $

if [ $# -ge 1 ]
then
	dir="$1"
else
	dir=/etc/pam.d
fi
if [ $# = 2 ]
then
	file="$2"
else
	file=/etc/pam.conf
fi
if [ $# -gt 2 ]
then
	echo "Usage: $0 [ output directory ] [ input file ]"
	echo "Default output is /etc/pam.d, default input is /etc/pam.conf"
	exit 1
fi

awk '/^([#[:space:]]*)([[:alnum:]_]+)[[:space:]]+(auth|account|session|password)[[:space:]]+([^[:space:]].*)$/ {
	match($0, /[#[:space:]]*/)
	prefix = substr($0, 0, RLENGTH)
	$0 = substr($0, RLENGTH + 1)
	match($0, /[[:alnum:]_]+/)
	name = substr($0, 0, RLENGTH)
	$0 = substr($0, RLENGTH + 1)
	match($0, /[[:space:]]+/)
	$0 = substr($0, RLENGTH + 1)
	match($0, /(auth|account|session|password)/)
	type = substr($0, 0, RLENGTH)
	$0 = substr($0, RLENGTH + 1)
	match($0, /[[:space:]]+/)
	arg = substr($0, RLENGTH + 1)

	line = prefix type
	tabs = ((16 - length(line)) / 8)
	for (i = 0; i < tabs; i++)
		line = line "\t"
	if ((name, type) in content)
		content[name, type] = content[name, type] "\n" line arg
	else
		content[name, type] = line arg
	services[name] = name
}

END {
	'fdir=\"$dir\"'

	split("auth account session password", types, " ")
	for (service in services) {
		fname = fdir "/" service
		system("rm -f " fname)
		print "#\n# $DragonFly: src/etc/pam.d/convert.sh,v 1.1 2005/07/22 18:20:43 joerg Exp $\n#\n# PAM configuration for the \"" service "\" service\n#\n" >> fname
		for (type in types)
			if ((service, types[type]) in content)
				print content[service, types[type]] >> fname
		close(fname)
	}
}' < $file
