#ifndef _SYS_CPPUTIL_H_
#define _SYS_CPPUTIL_H_

#include <sys/cdefs.h>
/*
 * This file includes macros that abuse the preprocessor --
 * we don't want to have to parse these except when compiling
 * the files that actually use them.
 */

/*
 * Returns the number of args in __VA_ARGS__.
 * Note: returns 1 if __VA_ARGS__ is empty!
 */

#define VA_NARGS(...) __VA_ARGS_SIZE(__VA_ARGS__, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,)

#define __VA_ARGS_SIZE(e0, e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17, e18, e19, e20, e21, e22, e23, e24, e25, e26, e27, e28, e29, e30, e31, e32, e33, e34, e35, e36, e37, e38, e39, e40, e41, e42, e43, e44, e45, e46, e47, e48, e49, e50, e51, e52, e53, e54, e55, e56, e57, e58, e59, e60, e61, e62, e63, size, ...) size

/*
 * Generate a struction declaration using the arguments passed in.
 * E.g. __GENSTRUCT(mystruct, int a, char *b); expands to
 * struct mystruct {
 * 	int a;
 *	char *b;
 * };
 */

#define __GENSTRUCT(tag, args...)\
	__CONCAT(__GENSTRUCT,VA_NARGS(args))(tag, args)


#define __GENSTRUCT1(tag, arg1) struct tag {    \
        arg1;   \
	}
#define __GENSTRUCT2(tag, arg1, arg2)   struct tag {    \
        arg1;   \
        arg2;   \
	}
#define __GENSTRUCT3(tag, arg1, arg2, arg3)     struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
	}
#define __GENSTRUCT4(tag, arg1, arg2, arg3, arg4)       struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
	}
#define __GENSTRUCT5(tag, arg1, arg2, arg3, arg4, arg5) struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
	}
#define __GENSTRUCT6(tag, arg1, arg2, arg3, arg4, arg5, arg6)   struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
	}
#define __GENSTRUCT7(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7)     struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
	}
#define __GENSTRUCT8(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)       struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
	}
#define __GENSTRUCT9(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9) struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
	}
#define __GENSTRUCT10(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10) struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
	}
#define __GENSTRUCT11(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11)  struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
	}
#define __GENSTRUCT12(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12)   struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
	}
#define __GENSTRUCT13(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13)    struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
        arg13;  \
	}
#define __GENSTRUCT14(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14)     struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
        arg13;  \
        arg14;  \
	}
#define __GENSTRUCT15(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14, arg15)      struct tag {  \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
        arg13;  \
        arg14;  \
        arg15;  \
	}
#define __GENSTRUCT16(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14, arg15, arg16)       struct tag {   \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
        arg13;  \
        arg14;  \
        arg15;  \
        arg16;  \
	}
#define __GENSTRUCT17(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17)      struct tag {     \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
        arg13;  \
        arg14;  \
        arg15;  \
        arg16;  \
        arg17;  \
	}
#define __GENSTRUCT18(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18)struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
        arg13;  \
        arg14;  \
        arg15;  \
        arg16;  \
        arg17;  \
        arg18;  \
	}
#define __GENSTRUCT19(tag, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18, arg19) struct tag {    \
        arg1;   \
        arg2;   \
        arg3;   \
        arg4;   \
        arg5;   \
        arg6;   \
        arg7;   \
        arg8;   \
        arg9;   \
        arg10;  \
        arg11;  \
        arg12;  \
        arg13;  \
        arg14;  \
        arg15;  \
        arg16;  \
        arg17;  \
        arg18;  \
        arg19;  \
	}
#endif	/* _SYS_CPPUTIL_H_ */
