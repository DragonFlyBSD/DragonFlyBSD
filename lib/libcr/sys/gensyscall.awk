# Copyright (c) 2004 Eirik Nygaard.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
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
# $DragonFly: src/lib/libcr/sys/Attic/gensyscall.awk,v 1.1 2004/08/12 19:59:30 eirikn Exp $

BEGIN {
	FS = "[\t]*";
	linenum = 0;
}
NF == 0 || $1 ~ /^;/ {
	linenum++;
	next;
}
$1 ~ /^#.*/ || $1 ~ /^[ 	]*$/ || $1 ~ /^;/ {
	linenum++;
	next;
}
$2 == syscall {
	outputfile = $2 ".c"
	linenum++;

	fields = NF
	functype = $1
	funcname = $2
	usefuncname = $3
	argalias = $4
	numargs = 0
	for (i = 5; i <= fields; i++) {
		argtype[numargs] = $i
		if (debug) {
			printf "Argtype found: %s\n", $i
		}
		i++
		argname[numargs] = $i
		if (debuf) {
			printf "Argname found: %s\n", $i
		}
		numargs++
	}
	if (usefuncname == "o" funcname) # Compat code
		realfuncname = usefuncname
	else
		realfuncname = funcname
	
#	printf "Processing %s.c\n", realfuncname
	if (NF < 2) {
		printf "<#> Error in input file at line %d\n", linenum
		exit 1
	}
	print "/*" > outputfile
	print " * Automatically generated and so on..." > outputfile
	printf " * Userland part of syscall: %s()\n", realfuncname > outputfile
	print " */\n" > outputfile

	print "#include <syscall.h>\n" > outputfile

# Actual syscall
	printf "%s\n", functype > outputfile
	printf "_%s", realfuncname > outputfile
	printf "(" > outputfile
	if (fields == 2) {
		printf "void" > outputfile
	}
	else {
		for (i = 0; i < numargs; i++) {
			printf "%s ", argtype[i] > outputfile
			printf "%s", argname[i] > outputfile
			if (i != numargs - 1)
				printf(", ") > outputfile
		}
	}
	print ")" > outputfile
	print "{" > outputfile
	printf "\tstruct %s %smsg;\n", argalias, funcname > outputfile
	print "\tint error;\n" > outputfile
	printf "\tINITMSGSYSCALL(%s, 0);\n", funcname > outputfile
	if (fields > 2) {
		for (i = 0; i < numargs; i++) {
			printf "\t%smsg.%s = %s;\n", funcname, argname[i], argname[i] > outputfile
		}
	}
	printf "\tDOMSGSYSCALL(%s);\n", funcname > outputfile
	printf "\tFINISHMSGSYSCALL(%s, error);\n", funcname > outputfile
	print "}" > outputfile

# Weak function
	printf "__attribute__((weak))\n" > outputfile
	printf "%s\n", functype > outputfile
	printf "%s", realfuncname > outputfile
	printf "(" > outputfile
	if (fields == 2) {
		printf "void" > outputfile
	}
	else {
		for (i = 0; i < numargs; i++) {
			printf "%s ", argtype[i] > outputfile
			printf "%s", argname[i] > outputfile
			if (i != numargs - 1)
				printf(", ") > outputfile
		}
	}
	print ")" > outputfile
	print "{" > outputfile
	printf "\treturn(_%s(", realfuncname > outputfile
	for (i = 0; i < numargs; i++) {
		printf "%s", argname[i] > outputfile
		if (i != numargs - 1)
			printf(", ") > outputfile
	}
	printf "));\n" > outputfile
	print "}\n" > outputfile

	close(outputfile)
}
