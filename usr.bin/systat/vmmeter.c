#define _KERNEL_STRUCTURES
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include "symbols.h"

#include <err.h>
#include <kinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <devstat.h>

#include "systat.h"
#include "extern.h"

#define X_START		1
#define CPU_START	1
#define CPU_STARTX	(3 + vmm_ncpus)
#define CPU_LABEL_W	7

#define DRAW_ROW(n, y, w, fmt, args...) \
do { \
	mvprintw(y, n, fmt, w - 1, args); \
	n += w; \
} while (0)

#define DRAW_ROW2(n, y, w, fmt, args...) \
do { \
	mvprintw(y, n, fmt, w - 1, w - 1, args); \
	n += w; \
} while (0)

static int vmm_ncpus;
static int vmm_fetched;
static struct vmmeter *vmm_cur, *vmm_prev;
static struct kinfo_cputime *vmm_cptime_cur, *vmm_cptime_prev;
#if 0
static struct save_ctx symctx;
#endif
static int symbols_read;

static void
getvmm(void)
{
	size_t sz;
	int i;

	for (i = 0; i < vmm_ncpus; ++i) {
		struct vmmeter *vmm = &vmm_cur[i];
		char buf[64];

		sz = sizeof(*vmm);
		snprintf(buf, sizeof(buf), "vm.cpu%d.vmmeter", i);
		if (sysctlbyname(buf, vmm, &sz, NULL, 0))
			err(1, "sysctlbyname(cpu%d)", i);

		vmm->v_intr -= (vmm->v_timer + vmm->v_ipi);
	}

	sz = vmm_ncpus * sizeof(struct kinfo_cputime);
	if (sysctlbyname("kern.cputime", vmm_cptime_cur, &sz, NULL, 0))
		err(1, "kern.cputime");
}

int
initvmm(void)
{
	return 1;
}

void
showvmm(void)
{
	int i, n;

	if (!vmm_fetched)
		return;

	for (i = 0; i < vmm_ncpus; ++i) {
		struct kinfo_cputime d;
		uint64_t cp_total = 0;

		n = X_START + CPU_LABEL_W;

#define D(idx, field) \
	(vmm_cur[idx].field - vmm_prev[idx].field) / (u_int)naptime

		DRAW_ROW(n, CPU_START + i, 6, "%*u", D(i, v_timer));
		DRAW_ROW(n, CPU_START + i, 8, "%*u", D(i, v_ipi));
		DRAW_ROW(n, CPU_START + i, 8, "%*u", D(i, v_intr));

#define CPUD(dif, idx, field) \
do { \
	dif.cp_##field = vmm_cptime_cur[idx].cp_##field - \
			 vmm_cptime_prev[idx].cp_##field; \
	cp_total += dif.cp_##field; \
} while (0)

#define CPUV(dif, field) \
	(dif.cp_##field * 100.0) / cp_total

		CPUD(d, i, user);
		CPUD(d, i, idle);
		CPUD(d, i, intr);
		CPUD(d, i, nice);
		CPUD(d, i, sys);

		if (cp_total == 0)
			cp_total = 1;

		DRAW_ROW(n, CPU_START + i, 6, "%*.1f",
			 CPUV(d, user) + CPUV(d, nice));
/*		DRAW_ROW(n, CPU_START + i, 6, "%*.1f", CPUV(d, nice));*/
		DRAW_ROW(n, CPU_START + i, 6, "%*.1f", CPUV(d, sys));
		DRAW_ROW(n, CPU_START + i, 6, "%*.1f", CPUV(d, intr));
		DRAW_ROW(n, CPU_START + i, 6, "%*.1f", CPUV(d, idle));

		/*
		 * Display token collision count and the last-colliding
		 * token name.
		 */
		if (D(i, v_lock_colls) > 9999999)
			DRAW_ROW(n, CPU_START + i, 8, "%*u", 9999999);
		else
			DRAW_ROW(n, CPU_START + i, 8, "%*u",
				 D(i, v_lock_colls));

		if (D(i, v_lock_colls) == 0) {
			DRAW_ROW2(n, CPU_START + i, 18, "%*.*s", "");
		} else {
			DRAW_ROW2(n, CPU_START + i, 18, "%*.*s",
				  vmm_cur[i].v_lock_name);
		}

#undef D
#undef CPUV
#undef CPUD
#define CPUC(idx, field) vmm_cptime_cur[idx].cp_##field

#if 0
		n = X_START + CPU_LABEL_W;

		DRAW_ROW(n, CPU_STARTX + i, 15, "%-*s", CPUC(i, msg));
		DRAW_ROW(n, CPU_STARTX + i, 35, "%-*s",
			address_to_symbol((void *)(intptr_t)CPUC(i, stallpc),
					  &symctx));
#endif
#undef CPUC
	}
}

void
fetchvmm(void)
{
	vmm_fetched = 1;

	memcpy(vmm_prev, vmm_cur, sizeof(struct vmmeter) * vmm_ncpus);
	memcpy(vmm_cptime_prev, vmm_cptime_cur,
	       sizeof(struct kinfo_cputime) * vmm_ncpus);
	getvmm();
}

void
labelvmm(void)
{
	int i, n;

	clear();

	n = X_START + CPU_LABEL_W;

	DRAW_ROW(n, CPU_START - 1, 6, "%*s", "timer");
	DRAW_ROW(n, CPU_START - 1, 8, "%*s", "ipi");
	DRAW_ROW(n, CPU_START - 1, 8, "%*s", "extint");
	DRAW_ROW(n, CPU_START - 1, 6, "%*s", "user%");
/*	DRAW_ROW(n, CPU_START - 1, 6, "%*s", "nice%");*/
	DRAW_ROW(n, CPU_START - 1, 6, "%*s", "sys%");
	DRAW_ROW(n, CPU_START - 1, 6, "%*s", "intr%");
	DRAW_ROW(n, CPU_START - 1, 6, "%*s", "idle%");
	DRAW_ROW(n, CPU_START - 1, 8, "%*s", "smpcol");
	DRAW_ROW(n, CPU_START - 1, 18, "%*s", "label");

	for (i = 0; i < vmm_ncpus; ++i)
		mvprintw(CPU_START + i, X_START, "cpu%d", i);

#if 0
	n = X_START + CPU_LABEL_W;
	DRAW_ROW(n, CPU_STARTX - 1, 15, "%-*s", "contention");
	DRAW_ROW(n, CPU_STARTX - 1, 35, "%-*s", "function");

	for (i = 0; i < vmm_ncpus; ++i)
		mvprintw(CPU_STARTX + i, X_START, "cpu%d", i);
#endif
}

WINDOW *
openvmm(void)
{
	if (symbols_read == 0) {
		symbols_read = 1;
		read_symbols(NULL);
	}

	if (kinfo_get_cpus(&vmm_ncpus))
		err(1, "kinfo_get_cpus");

	vmm_cur = calloc(vmm_ncpus, sizeof(*vmm_cur));
	if (vmm_cur == NULL)
		err(1, "calloc vmm_cur");

	vmm_prev = calloc(vmm_ncpus, sizeof(*vmm_prev));
	if (vmm_prev == NULL)
		err(1, "calloc vmm_prev");

	vmm_cptime_cur = calloc(vmm_ncpus, sizeof(*vmm_cptime_cur));
	if (vmm_cptime_cur == NULL)
		err(1, "calloc vmm_cptime_cur");

	vmm_cptime_prev = calloc(vmm_ncpus, sizeof(*vmm_cptime_prev));
	if (vmm_cptime_prev == NULL)
		err(1, "calloc vmm_cptime_prev");

	getvmm();

	return (stdscr);
}

void
closevmm(WINDOW *w)
{
	if (vmm_cur != NULL)
		free(vmm_cur);
	if (vmm_prev != NULL)
		free(vmm_prev);

	if (vmm_cptime_cur != NULL)
		free(vmm_cptime_cur);
	if (vmm_cptime_prev != NULL)
		free(vmm_cptime_prev);

	vmm_fetched = 0;

	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
}
