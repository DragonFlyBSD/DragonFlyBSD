dnl MODULE_HELPER(NAME, HELP, DEFAULT, COMMANDS)
AC_DEFUN([MODULE_HELPER],[
	unset have_module
	AC_ARG_ENABLE([$1], [$2],,[
		if test "x${enable_all}" = "xdefault"; then
			enable_[$1]=[$3]
		else
			enable_[$1]="${enable_all}"
		fi
	])
	if test "x${enable_[$1]}" != "xno"; then
		$4
		AC_MSG_CHECKING([whether to build $1 module])
		if test -n "${have_module+set}"; then
			if test "x${enable_[$1]}" = "xauto"; then
				if test "x${enable_plugins}" != "xno"; then
					AC_MSG_RESULT([yes, as plugin]) 
					build_static=no
					build_shared=yes
				else
					AC_MSG_RESULT([yes]) 
					build_static=yes
					build_shared=no
				fi
			elif test "x${enable_[$1]}" = "xshared"; then
				if test "x${enable_plugins}" != "xno"; then
					AC_MSG_RESULT([yes, as plugin]) 
					build_static=no
					build_shared=yes
				else
					AC_MSG_RESULT([no]) 
					AC_MSG_ERROR([Can't build [$1] module, plugins are disabled])
				fi
			else
				AC_MSG_RESULT([yes]) 
				build_static=yes
				build_shared=no
			fi
		elif test "x${enable_[$1]}" != "xauto"; then
			AC_MSG_RESULT([no]) 
			AC_MSG_ERROR([Unable to build $1 plugin, see messages above])
		else	
			AC_MSG_RESULT([no]) 
			build_static=no
			build_shared=no
		fi
	else
		AC_MSG_CHECKING([whether to build $1 module])
		AC_MSG_RESULT([no]) 
		build_static=no
		build_shared=no
	fi
])
