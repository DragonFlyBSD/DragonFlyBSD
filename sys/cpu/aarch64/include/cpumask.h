/*
 * Placeholder for arm64 cpumask definition.
 */

#ifndef _CPU_CPUMASK_H_
#define _CPU_CPUMASK_H_

typedef unsigned long __cpumask_t;

#define __CPU_ZERO(set)         do { *(set) = 0; } while (0)
#define __CPU_SET(cpu, set)     do { *(set) |= (1UL << (cpu)); } while (0)
#define __CPU_CLR(cpu, set)     do { *(set) &= ~(1UL << (cpu)); } while (0)
#define __CPU_ISSET(cpu, set)   ((*(set) & (1UL << (cpu))) != 0)
#define __CPU_COUNT(set)        __builtin_popcountl(*(set))
#define __CPU_AND(dst, set1, set2) do { *(dst) = (*(set1) & *(set2)); } while (0)
#define __CPU_OR(dst, set1, set2)  do { *(dst) = (*(set1) | *(set2)); } while (0)
#define __CPU_XOR(dst, set1, set2) do { *(dst) = (*(set1) ^ *(set2)); } while (0)
#define __CPU_EQUAL(set1, set2)    (*(set1) == *(set2))

#endif /* !_CPU_CPUMASK_H_ */
