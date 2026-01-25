#ifndef _MACHINE_PTRACE_H_
#define _MACHINE_PTRACE_H_

#define __HAVE_PTRACE_MACHDEP

/*
 * Machine dependent trace commands.
 */
#define PT_GETREGS	(PT_FIRSTMACH + 1)
#define PT_SETREGS	(PT_FIRSTMACH + 2)
#define PT_GETFPREGS	(PT_FIRSTMACH + 3)
#define PT_SETFPREGS	(PT_FIRSTMACH + 4)
/* No debug registers on arm64 yet */

#define PT_GETVFPREGS32	(PT_FIRSTMACH + 10)
#define PT_SETVFPREGS32	(PT_FIRSTMACH + 11)

#endif /* _MACHINE_PTRACE_H_ */
