/*
 * Freestanding setjmp.h for aarch64 EFI loader build.
 * Provides minimal jmp_buf and function declarations.
 */

#ifndef _SETJMP_H_
#define _SETJMP_H_

/*
 * AArch64 jmp_buf size:
 * - x19-x30: 12 callee-saved registers
 * - sp: 1 register
 * - d8-d15: 8 SIMD callee-saved registers (optional, included for safety)
 * Total: 22 longs (176 bytes on LP64)
 */
#define _JBLEN	22

typedef long jmp_buf[_JBLEN];
typedef long sigjmp_buf[_JBLEN];

int	_setjmp(jmp_buf);
void	_longjmp(jmp_buf, int);
int	setjmp(jmp_buf);
void	longjmp(jmp_buf, int);

#endif /* !_SETJMP_H_ */
