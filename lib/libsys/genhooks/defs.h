/*
 * DEFS.H
 *
 * $DragonFly: src/lib/libsys/genhooks/defs.h,v 1.2 2005/12/05 16:48:22 dillon Exp $
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

#define TOK_SEMI	';'
#define TOK_COMMA	','
#define TOK_OBRACE	'{'
#define TOK_CBRACE	'}'
#define TOK_OPAREN	'('
#define TOK_CPAREN	')'
#define TOK_EOF		0x00000100
#define TOK_UNKNOWN	0x00000101
#define TOK_INTEGER	0x00000102
#define TOK_BASE	0x00000103
#define TOK_SYMBOL	0x04000000

#define TOK_ADD			(TOK_SYMBOL|0x00000001)
#define TOK_FUNCTION		(TOK_SYMBOL|0x00000002)
#define TOK_IMPLEMENTATION	(TOK_SYMBOL|0x00000003)
#define TOK_DIRECT		(TOK_SYMBOL|0x00000004)
#define TOK_SIMULATED		(TOK_SYMBOL|0x00000005)

typedef struct lex_token {
    struct lex_info *info;
    int		type;
    int		index;
    int		len;
    const char	*sym;
    int		value;
} lex_token;

typedef struct lex_info {
    char	*path;
    int		fd;
    const char	*base;
    int		size;
    int		cache_line;	/* cached line number for index */
    int		cache_index;	/* index to base of cached line */
    int		cache_len;	/* size of cached line, including newline */
} lex_info;

typedef struct sys_type {
    char        *type_name;
    char        *var_name;
} sys_type;

typedef struct sys_info {
    struct sys_type *func_ret;
    struct sys_type **func_args;
    int         nargs;          /* 0 if takes void */
    int		sysno;		/* syscall number */
} sys_info;

extern struct sys_info **sys_array;
extern int sys_count;
extern char *sys_sectname;

void *zalloc(int bytes);

void lex_open(const char *path, lex_token *tok);
void lex_close(lex_token *tok);
int lex_gettoken(lex_token *tok);
int lex_skip_token(lex_token *tok, int type);
void lex_error(lex_token *tok, const char *ctl, ...);
const char *lex_string_quick(lex_token *tok);
char *lex_string(lex_token *tok);

void parse_file(const char *path);

void output_user(FILE *fo);
void output_lib(FILE *fo);
void output_standalone(FILE *fo, const char *list_prefix);

