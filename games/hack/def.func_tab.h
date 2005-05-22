/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* def.func_tab.h - version 1.0.2 */
/* $DragonFly: src/games/hack/def.func_tab.h,v 1.2 2005/05/22 03:37:05 y0netan1 Exp $ */

struct func_tab {
	char f_char;
	int (*f_funct)();
};

extern struct func_tab cmdlist[];

struct ext_func_tab {
	const char *ef_txt;
	int (*ef_funct)();
};

extern struct ext_func_tab extcmdlist[];
