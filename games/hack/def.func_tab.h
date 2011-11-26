/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* def.func_tab.h - version 1.0.2 */
/* $DragonFly: src/games/hack/def.func_tab.h,v 1.3 2006/08/21 19:45:32 pavalos Exp $ */

struct func_tab {
	char f_char;
	int (*f_funct)(void);
};

struct ext_func_tab {
	const char *ef_txt;
	int (*ef_funct)(void);
};
