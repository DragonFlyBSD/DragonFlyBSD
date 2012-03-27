#ifndef PREFIX2
#define PREFIX2	PREFIX
#endif

#define LINK_LIBGCC_SPEC ""
#define LIBGCC_SPEC "%{shared: -lgcc_pic} %{!shared: -lgcc}"

#undef	LINK_SPEC
#define LINK_SPEC DFBSD_LINK_SPEC \
"%{pg: -L"PREFIX2"/lib/gcc"GCCSHORTVER"/profile \
  %{!static: -rpath /usr/lib/gcc"GCCSHORTVER"/profile} \
 } \
 -L"PREFIX2"/lib/gcc"GCCSHORTVER" \
 %{!static: -rpath /usr/lib/gcc"GCCSHORTVER"} \
 %{pg: \
  %{!nostdlib: \
   %{!nodefaultlibs: -L"PREFIX2"/lib/profile \
    %{!static: -rpath /usr/lib/profile} \
   } \
  } \
 }"

/*#define TARGET_SYSTEM_ROOT_RELOCATABLE */
/*#define TARGET_SYSTEM_ROOT		"/"*/
#define	STANDARD_EXEC_PREFIX		PREFIX"/libexec/gcc"GCCSHORTVER"/"
#define STANDARD_LIBEXEC_PREFIX		STANDARD_EXEC_PREFIX
#define STANDARD_BINDIR_PREFIX		STANDARD_EXEC_PREFIX
#define MD_EXEC_PREFIX			STANDARD_EXEC_PREFIX
#define STANDARD_STARTFILE_PREFIX	0
#define TOOLDIR_BASE_PREFIX		"./"

#define STARTFILE_PREFIX_SPEC		PREFIX2"/lib/ "PREFIX2"/lib/gcc"GCCSHORTVER"/"

#define GPLUSPLUS_INCLUDE_DIR		PREFIX2"/include/c++/"GCCPOINTVER
#undef	GPLUSPLUS_TOOL_INCLUDE_DIR
#define	GPLUSPLUS_BACKWARD_INCLUDE_DIR	PREFIX2"/include/c++/"GCCPOINTVER"/backward"
#undef	LOCAL_INCLUDE_DIR
#define	GCC_INCLUDE_DIR			PREFIX2"/libdata/gcc"GCCSHORTVER
#undef	FIXED_INCLUDE_DIR
#undef	CORSS_INCLUDE_DIR
#undef	TOOL_INCLUDE_DIR
#undef	SYSTEM_INCLUDE_DIR
#define	STANDARD_INCLUDE_DIR			PREFIX2"/include"
