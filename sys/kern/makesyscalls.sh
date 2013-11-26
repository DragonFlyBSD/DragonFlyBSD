#! /bin/sh -
#	@(#)makesyscalls.sh	8.1 (Berkeley) 6/10/93
# $FreeBSD: src/sys/kern/makesyscalls.sh,v 1.39.2.4 2001/10/20 09:01:24 marcel Exp $

set -e

# name of compat option:
compat=COMPAT_43
# name of DragonFly 1.2 compat option
compatdf12=COMPAT_DF12

# output files:
sysnames="syscalls.c"
sysproto="../sys/sysproto.h"
sysunion="../sys/sysunion.h"
sysproto_h=_SYS_SYSPROTO_H_
syshdr="../sys/syscall.h"
sysmk="../sys/syscall.mk"
syssw="init_sysent.c"
syscallprefix="SYS_"
switchname="sysent"
namesname="syscallnames"

# tmp files:
sysdcl="sysent.dcl.$$"
syscompat="sysent.compat.$$"
syscompatdf12="sysent.compatdf12.$$"
syscompatdcl="sysent.compatdcl.$$"
syscompatdcldf12="sysent.compatdcldf12.$$"
sysent="sysent.switch.$$"
sysinc="sysinc.switch.$$"
sysarg="sysarg.switch.$$"
sysun="sysunion.switch.$$"

trap "rm $sysdcl $syscompat $syscompatdf12 $syscompatdcl $syscompatdcldf12 $sysent $sysinc $sysarg $sysun" 0

touch $sysdcl $syscompat $syscompatdf12 $syscompatdcl $syscompatdcldf12 $sysent $sysinc $sysarg $sysun

case $# in
    0)	echo "Usage: $0 input-file <config-file>" 1>&2
	exit 1
	;;
esac

if [ -n "$2" -a -f "$2" ]; then
	. $2
fi

sed -e '
s/\$//g
:join
	/\\$/{a\

	N
	s/\\\n//
	b join
	}
2,${
	/^#/!s/\([{}()*,]\)/ \1 /g
}
' < $1 | awk "
	BEGIN {
		sysdcl = \"$sysdcl\"
		sysproto = \"$sysproto\"
		sysproto_h = \"$sysproto_h\"
		syscompat = \"$syscompat\"
		syscompatdf12 = \"$syscompatdf12\"
		syscompatdcl = \"$syscompatdcl\"
		syscompatdcldf12 = \"$syscompatdcldf12\"
		sysent = \"$sysent\"
		syssw = \"$syssw\"
		sysinc = \"$sysinc\"
		sysarg = \"$sysarg\"
		sysun = \"$sysun\"
		sysnames = \"$sysnames\"
		syshdr = \"$syshdr\"
		sysmk = \"$sysmk\"
		compat = \"$compat\"
		compatdf12 = \"$compatdf12\"
		syscallprefix = \"$syscallprefix\"
		switchname = \"$switchname\"
		namesname = \"$namesname\"
		infile = \"$1\"
		"'

		printf "/*\n * System call switch table.\n *\n" > syssw
		printf " * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed\n" > syssw
		printf " *               by running make sysent in the same directory.\n" > syssw
		printf " */\n\n" > syssw

		printf "/*\n * System call prototypes.\n *\n" > sysarg
		printf " * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed\n" > sysarg
		printf " *               by running make sysent in the same directory.\n" > sysarg
		printf " */\n\n" > sysarg
		printf "#ifndef %s\n", sysproto_h > sysarg
		printf "#define\t%s\n\n", sysproto_h > sysarg
		printf "#include <sys/select.h>\n\n" > sysarg
		printf "#include <sys/signal.h>\n\n" > sysarg
		printf "#include <sys/acl.h>\n\n" > sysarg
		printf "#include <sys/msgport.h>\n\n" > sysarg
		printf "#include <sys/sysmsg.h>\n\n" > sysarg
		printf "#include <sys/syslink.h>\n\n" > sysarg
		printf "#define\tPAD_(t)\t(sizeof(register_t) <= sizeof(t) ? \\\n" > sysarg
		printf "\t\t0 : sizeof(register_t) - sizeof(t))\n\n" > sysarg

		printf "\n#ifdef %s\n\n", compat > syscompat
		printf "\n#ifdef %s\n\n", compatdf12 > syscompatdf12

		printf "/*\n * System call names.\n *\n" > sysnames
		printf " * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed\n" > sysnames
		printf " *               by running make sysent in the same directory.\n" > sysnames
		printf " */\n\n" > sysnames
		printf "const char *%s[] = {\n", namesname > sysnames

		printf "/*\n * System call numbers.\n *\n" > syshdr
		printf " * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed\n" > syshdr
		printf " *               by running make sysent in the same directory.\n" > syshdr
		printf " */\n\n" > syshdr

		printf "# DragonFly system call names.\n" > sysmk
		printf "# DO NOT EDIT-- To regenerate this file, edit syscalls.master followed\n" > sysmk
		printf "#               by running make sysent in the same directory.\n" > sysmk
		printf "MIASM = " > sysmk

		printf "/*\n * Union of syscall args for messaging.\n *\n" > sysun
		printf " * DO NOT EDIT-- To regenerate this file, edit syscalls.master followed\n" > sysun
		printf " *               by running make sysent in the same directory.\n" > sysun
		printf " */\n\n" > sysun
		printf "union sysunion {\n" > sysun
		printf "#ifdef _KERNEL /* header only applies in kernel */\n" > sysun
		printf "\tstruct\tlwkt_msg lmsg;\n" > sysun
		printf "\tstruct\tsysmsg sysmsg;\n" > sysun
		printf "#endif\n" > sysun

		printf "\n/* The casts are bogus but will do for now. */\n" > sysent
		printf "struct sysent %s[] = {\n",switchname > sysent

		printf "\n#ifdef _KERNEL\n\n" > sysdcl
		printf "\n#ifdef _KERNEL\n\n" > syscompatdcl
		printf "\n#ifdef _KERNEL\n\n" > syscompatdcldf12
	}
	NF == 0 || $1 ~ /^;/ {
		next
	}
	$1 ~ /^#[ 	]*include/ {
		print > sysinc
		next
	}
	$1 ~ /^#[ 	]*if/ {
		print > sysent
		print > sysdcl
		print > sysarg
		print > syscompat
		print > syscompatdf12
		print > sysnames
		print > sysun
		savesyscall = syscall
		next
	}
	$1 ~ /^#[ 	]*else/ {
		print > sysent
		print > sysdcl
		print > sysarg
		print > sysun
		print > syscompat
		print > syscompatdf12
		print > sysnames
		syscall = savesyscall
		next
	}
	$1 ~ /^#/ {
		print > sysent
		print > sysdcl
		print > sysarg
		print > sysun
		print > syscompat
		print > syscompatdf12
		print > sysnames
		next
	}
	syscall != $1 {
		printf "%s: line %d: syscall number out of sync at %d\n",
		    infile, NR, syscall
		printf "line is:\n"
		print
		exit 1
	}
	function align_sysent_comment(column) {
		printf("\t") > sysent
		column = column + 8 - column % 8
		while (column < 56) {
			printf("\t") > sysent
			column = column + 8
		}
	}
	function parserr(was, wanted) {
		printf "%s: line %d: unexpected %s (expected %s)\n",
		    infile, NR, was, wanted
		exit 1
	}
	function parseline() {
		f=3			# toss number and type
		argc= 0;
		argssize = "0"
		if ($NF != "}") {
			funcalias=$(NF-2)
			argalias=$(NF-1)
			rettype=$NF
			end=NF-3
		} else {
			funcalias=""
			argalias=""
			rettype="int"
			end=NF
		}
		if ($2 == "NODEF") {
			funcname=$3
			argssize = "AS(" $5 ")"
			return
		}
		if ($f != "{")
			parserr($f, "{")
		f++
		if ($end != "}")
			parserr($end, "}")
		end--
		if ($end != ";")
			parserr($end, ";")
		end--
		if ($end != ")")
			parserr($end, ")")
		end--

		f++	#function return type

		funcname=$f
		usefuncname=$f
		if (funcalias == "")
			funcalias = funcname
		if (argalias == "") {
			argalias = funcname "_args"
			if ($2 == "COMPAT") {
				argalias = "o" argalias
				usefuncname = "sys_o" funcname
			}
			if ($2 == "COMPAT_DF12") {
				argalias = "dfbsd12_" argalias
				usefuncname = "sys_dfbsd12_" funcname
			}
		}
		f++

		if ($f != "(")
			parserr($f, ")")
		f++

		if (f == end) {
			if ($f != "void")
				parserr($f, "argument definition")
			return
		}

		while (f <= end) {
			argc++
			argtype[argc]=""
			oldf=""
			while (f < end && $(f+1) != ",") {
				if (argtype[argc] != "" && oldf != "*")
					argtype[argc] = argtype[argc]" ";
				argtype[argc] = argtype[argc]$f;
				oldf = $f;
				f++
			}
			if (argtype[argc] == "")
				parserr($f, "argument definition")
			argname[argc]=$f;
			f += 2;			# skip name, and any comma
		}
		if (argc != 0)
			argssize = "AS(" argalias ")"
	}
	{	comment = $3
		if (NF < 6)
			for (i = 4; i <= NF; i++)
				comment = comment " " $i
	}
	$2 == "STD" || $2 == "NODEF" || $2 == "NOARGS"  || $2 == "NOPROTO" \
	    || $2 == "NOIMPL" {
		parseline()
		if ((!nosys || funcname != "nosys") && \
		    (funcname != "lkmnosys")) {
			if (argc != 0 && $2 != "NOARGS" && $2 != "NOPROTO") {
				printf("\tstruct\t%s %s;\n", argalias, usefuncname) > sysun
				printf("struct\t%s {\n", argalias) > sysarg
				printf("#ifdef _KERNEL\n") > sysarg
				printf("\tstruct sysmsg sysmsg;\n") > sysarg
				printf("#endif\n") > sysarg
				for (i = 1; i <= argc; i++)
					printf("\t%s\t%s;\tchar %s_[PAD_(%s)];\n",
					    argtype[i], argname[i],
					    argname[i], argtype[i]) > sysarg
				printf("};\n") > sysarg
			}
			else if ($2 != "NOARGS" && $2 != "NOPROTO" && \
			    $2 != "NODEF") {
				printf("\tstruct\t%s %s;\n", argalias, usefuncname) > sysun
				printf("struct\t%s {\n", argalias) > sysarg
				printf("#ifdef _KERNEL\n") > sysarg
				printf("\tstruct sysmsg sysmsg;\n") > sysarg
				printf("#endif\n") > sysarg
				printf("\tregister_t dummy;\n") > sysarg
				printf("};\n") > sysarg
			}
		}
		if (($2 != "NOPROTO" && $2 != "NODEF" && \
		    (funcname != "nosys" || !nosys)) || \
		    (funcname == "lkmnosys" && !lkmnosys) || \
		    funcname == "lkmressys") {
			printf("%s\tsys_%s (struct %s *)",
			    rettype, funcname, argalias) > sysdcl
			printf(";\n") > sysdcl
		}
		if (funcname == "nosys")
			nosys = 1
		if (funcname == "lkmnosys")
			lkmnosys = 1
		printf("\t{ %s, (sy_call_t *)", argssize) > sysent
		column = 8 + 2 + length(argssize) + 15
	 	if ($2 != "NOIMPL") {
			printf("sys_%s },", funcname) > sysent
			column = column + length(funcname) + 7
		} else {
			printf("sys_%s },", "nosys") > sysent
			column = column + length("nosys") + 7
		}
		align_sysent_comment(column)
		printf("/* %d = %s */\n", syscall, funcalias) > sysent
		printf("\t\"%s\",\t\t\t/* %d = %s */\n",
		    funcalias, syscall, funcalias) > sysnames
		if ($2 != "NODEF") {
			printf("#define\t%s%s\t%d\n", syscallprefix,
		    	    funcalias, syscall) > syshdr
			printf(" \\\n\t%s.o", funcalias) > sysmk
		}
		syscall++
		next
	}
	$2 == "COMPAT" || $2 == "CPT_NOA" {
		ncompat++
		parseline()
		if (argc != 0 && $2 != "CPT_NOA") {
			printf("#ifdef %s\n", compat) > sysun
			printf("\tstruct\t%s %s;\n", argalias, usefuncname) > sysun
			printf("#endif\n") > sysun
			printf("struct\t%s {\n", argalias) > syscompat
			printf("#ifdef _KERNEL\n") > syscompat
			printf("\tstruct sysmsg sysmsg;\n") > syscompat
			printf("#endif\n") > syscompat
			for (i = 1; i <= argc; i++)
				printf("\t%s\t%s;\tchar %s_[PAD_(%s)];\n",
				    argtype[i], argname[i],
				    argname[i], argtype[i]) > syscompat
			printf("};\n") > syscompat
		}
		else if($2 != "CPT_NOA") {
			printf("\tstruct\t%s %s;\n", argalias, usefuncname) > sysun
			printf("struct\t%s {\n", argalias) > sysarg
			printf("#ifdef _KERNEL\n") > sysarg
			printf("\tstruct sysmsg sysmsg;\n") > sysarg
			printf("#endif\n") > sysarg
			printf("\tregister_t dummy;\n") > sysarg
			printf("};\n") > sysarg
		}
		printf("%s\tsys_o%s (struct %s *);\n",
		    rettype, funcname, argalias) > syscompatdcl
		printf("\t{ compat(%s,%s) },",
		    argssize, funcname) > sysent
		align_sysent_comment(8 + 9 + \
		    length(argssize) + 1 + length(funcname) + 4)
		printf("/* %d = old %s */\n", syscall, funcalias) > sysent
		printf("\t\"old.%s\",\t\t/* %d = old %s */\n",
		    funcalias, syscall, funcalias) > sysnames
		printf("\t\t\t\t/* %d is old %s */\n",
		    syscall, funcalias) > syshdr
		syscall++
		next
	}
	$2 == "COMPAT_DF12" {
		ncompatdf12++
		parseline()
		if (argc != 0) {
			printf("#ifdef %s\n", compatdf12) > sysun
			printf("\tstruct\t%s %s;\n", argalias, usefuncname) > sysun
			printf("#endif\n") > sysun
			printf("struct\t%s {\n", argalias) > syscompatdf12
			printf("#ifdef _KERNEL\n") > syscompatdf12
			printf("\tstruct sysmsg sysmsg;\n") > syscompatdf12
			printf("#endif\n") > syscompatdf12
			for (i = 1; i <= argc; i++)
				printf("\t%s\t%s;\tchar %s_[PAD_(%s)];\n",
				    argtype[i], argname[i],
				    argname[i], argtype[i]) > syscompatdf12
			printf("};\n") > syscompatdf12
		}
		else {
			printf("\tstruct\t%s %s;\n", argalias, usefuncname) > sysun
			printf("struct\t%s {\n", argalias) > sysarg
			printf("#ifdef _KERNEL\n") > sysarg
			printf("\tstruct sysmsg sysmsg;\n") > sysarg
			printf("#endif\n") > sysarg
			printf("\tregister_t dummy;\n") > sysarg
			printf("};\n") > sysarg
		}
		printf("%s\tsys_dfbsd12_%s (struct %s *);\n",
		    rettype, funcname, argalias) > syscompatdcldf12
		printf("\t{ compatdf12(%s,%s) },",
		    argssize, funcname) > sysent
		align_sysent_comment(8 + 9 + \
		    length(argssize) + 1 + length(funcname) + 4)
		printf("/* %d = old %s */\n", syscall, funcalias) > sysent
		printf("\t\"old.%s\",\t\t/* %d = old %s */\n",
		    funcalias, syscall, funcalias) > sysnames
		printf("\t\t\t\t/* %d is old %s */\n",
		    syscall, funcalias) > syshdr
		syscall++
		next
	}
	$2 == "LIBCOMPAT" {
		ncompat++
		parseline()
		printf("%s\tsys_o%s();\n", rettype, funcname) > syscompatdcl
		printf("\t{ compat(%s,%s) },",
		    argssize, funcname) > sysent
		align_sysent_comment(8 + 9 + \
		    length(argssize) + 1 + length(funcname) + 4)
		printf("/* %d = old %s */\n", syscall, funcalias) > sysent
		printf("\t\"old.%s\",\t\t/* %d = old %s */\n",
		    funcalias, syscall, funcalias) > sysnames
		printf("#define\t%s%s\t%d\t/* compatibility; still used by libc */\n",
		    syscallprefix, funcalias, syscall) > syshdr
		printf(" \\\n\t%s.o", funcalias) > sysmk
		syscall++
		next
	}
	$2 == "OBSOL" {
		printf("\t{ 0, (sy_call_t *)sys_nosys },") > sysent
		align_sysent_comment(37)
		printf("/* %d = obsolete %s */\n", syscall, comment) > sysent
		printf("\t\"obs_%s\",\t\t\t/* %d = obsolete %s */\n",
		    $3, syscall, comment) > sysnames
		printf("\t\t\t\t/* %d is obsolete %s */\n",
		    syscall, comment) > syshdr
		syscall++
		next
	}
	$2 == "UNIMPL" {
		printf("\t{ 0, (sy_call_t *)sys_nosys },\t\t\t/* %d = %s */\n",
		    syscall, comment) > sysent
		printf("\t\"#%d\",\t\t\t/* %d = %s */\n",
		    syscall, syscall, comment) > sysnames
		syscall++
		next
	}
	{
		printf "%s: line %d: unrecognized keyword %s\n", infile, NR, $2
		exit 1
	}
	END {
		printf "\n#define AS(name) ((sizeof(struct name) - sizeof(struct sysmsg)) / sizeof(register_t))\n" > sysinc
		if (ncompat != 0) {
			printf "#include \"opt_compat.h\"\n\n" > syssw
			printf "\n#ifdef %s\n", compat > sysinc
			printf "#define compat(n, name) n, (sy_call_t *)__CONCAT(sys_,__CONCAT(o,name))\n" > sysinc
			printf "#else\n" > sysinc
			printf "#define compat(n, name) 0, (sy_call_t *)sys_nosys\n" > sysinc
			printf "#endif\n" > sysinc
		}

		if (ncompatdf12 != 0) {
			printf "#ifdef __i386__\n" > syssw
			printf "#include \"opt_compatdf12.h\"\n" > syssw
			printf "#endif\n\n" > syssw
			printf "\n#ifdef %s\n", compatdf12 > sysinc
			printf "#define compatdf12(n, name) n, (sy_call_t *)__CONCAT(sys_,__CONCAT(dfbsd12_,name))\n" > sysinc
			printf "#else\n" > sysinc
			printf "#define compatdf12(n, name) 0, (sy_call_t *)sys_nosys\n" > sysinc
			printf "#endif\n" > sysinc
		}

		printf("\n#endif /* _KERNEL */\n") > syscompatdcl
		printf("\n#endif /* _KERNEL */\n") > syscompatdcldf12
		printf("\n#endif /* %s */\n\n", compat) > syscompatdcl
		printf("\n#endif /* %s */\n\n", compatdf12) > syscompatdcldf12

		printf("\n") > sysmk
		printf("};\n") > sysent
		printf("};\n") > sysnames
		printf("};\n") > sysun
		printf("\n#endif /* !%s */\n", sysproto_h) > sysdcl
		printf("#undef PAD_\n") > sysdcl
		printf("\n#endif /* _KERNEL */\n") > sysdcl
		printf("#define\t%sMAXSYSCALL\t%d\n", syscallprefix, syscall) \
		    > syshdr
	} '

cat $sysinc $sysent >> $syssw
cat $sysarg $syscompat $syscompatdcl $syscompatdf12 $syscompatdcldf12 $sysdcl > $sysproto
cat $sysun > $sysunion
