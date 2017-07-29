/*
 * SYS/KCOLLECT.H
 */

#ifndef _SYS_KCOLLECT_H_
#define _SYS_KCOLLECT_H_

#define KCOLLECT_ENTRIES        31

/*
 * Record format.
 *
 * Note that the first record returned by the sysctl contains scale and
 * format data in the data[0] fields.  The format is stored in the low
 * 8-bits and the scale in the remaining bits, unsigned.  ticks is set to
 * the current ticks as-of when the sysctl was issues and hz is set to hz
 * for the machine, to interpret ticks.  Caller can calculate dates from
 * that.
 *
 * The second record stores identifying strings in the data field,
 * up to 8 characters per entry.  An 8-character-long string will not be
 * zero terminated.  The ticks and hz fields will be 0.
 *
 * All remaining records contain data going backwards in time.  The ticks
 * field will be set as-of when the data is collected, hz will be 0, and
 * the data[] fields will contain the raw values according to the format.
 */
typedef struct {
	uint32_t	ticks;
	uint32_t	hz;			/* record #0 only */
	uint64_t	data[KCOLLECT_ENTRIES];
} kcollect_t;

#define KCOLLECT_LOAD		0	/* machine load 1.0 = 1 cpu @ 100%  */
#define KCOLLECT_USERPCT	1	/* whole machine user % */
#define KCOLLECT_SYSTPCT	2	/* whole machine sys % */
#define KCOLLECT_IDLEPCT	3	/* whole machine idle % */
#define KCOLLECT_SWAPPCT	4	/* total swap used % */
#define KCOLLECT_SWAPANO	5	/* anonymous swap used MB */
#define KCOLLECT_SWAPCAC	6	/* swapcache swap used MB */
#define KCOLLECT_VMFAULT	7	/* all vm faults incl zero-fill */
#define KCOLLECT_ZFILL		8	/* zero-fill faults */
#define KCOLLECT_MEMFRE		9	/* amount of free memory, bytes */
#define KCOLLECT_MEMCAC		10	/* amount of almost free memory */
#define KCOLLECT_MEMINA		11	/* amount of inactive memory */
#define KCOLLECT_MEMACT		12	/* amount of active memory */
#define KCOLLECT_MEMWIR		13	/* amount of wired/kernel memory */

#define KCOLLECT_DYNAMIC_START	16	/* dynamic entries */

#define KCOLLECT_LOAD_FORMAT	'2'	/* N.NN (modulo 100) */
#define KCOLLECT_USERPCT_FORMAT	'p'	/* percentage of single cpu x 100 */
#define KCOLLECT_SYSTPCT_FORMAT	'p'	/* percentage of single cpu x 100 */
#define KCOLLECT_IDLEPCT_FORMAT	'p'	/* percentage of single cpu x 100 */
#define KCOLLECT_SWAPPCT_FORMAT	'p'	/* percentage of single cpu x 100 */
#define KCOLLECT_SWAPANO_FORMAT	'm'	/* in megabytes (1024*1024) */
#define KCOLLECT_SWAPCAC_FORMAT	'm'	/* in megabytes (1024*1024) */
#define KCOLLECT_VMFAULT_FORMAT	'c'	/* count over period */
#define KCOLLECT_ZFILL_FORMAT 	'c'	/* count over period */
#define KCOLLECT_MEMFRE_FORMAT 	'b'	/* total bytes */
#define KCOLLECT_MEMCAC_FORMAT 	'b'	/* total bytes */
#define KCOLLECT_MEMINA_FORMAT 	'b'	/* total bytes */
#define KCOLLECT_MEMACT_FORMAT 	'b'	/* total bytes */
#define KCOLLECT_MEMWIR_FORMAT 	'b'	/* total bytes */

#define KCOLLECT_SCALE(fmt, scale)	((fmt) | ((uint64_t)(scale) << 8))
#define KCOLLECT_GETFMT(scale)		((char)(scale))
#define KCOLLECT_GETSCALE(scale)	((scale) >> 8)

#ifdef _KERNEL

typedef uint64_t (*kcallback_t)(int n);

int kcollect_register(int which, const char *id,
			kcallback_t func, uint64_t scale);
void kcollect_unregister(int n);
void kcollect_setvalue(int n, uint64_t value);

#endif

#endif
