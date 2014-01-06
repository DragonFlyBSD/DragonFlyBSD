%{

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "evtr.h"
#include "tok.h"
#include "internal.h"
#include "ktrfmt.tab.h"

int __ktrfmtlex(YYSTYPE *);
#define __ktrfmt_lex __ktrfmtlex

void __ktrfmt_error (struct ktrfmt_parse_ctx *, const char *);

static void do_parse_err(struct ktrfmt_parse_ctx *, const char *, ...)
		__printflike(2, 3);

static
void
do_parse_err(struct ktrfmt_parse_ctx *ctx, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(ctx->errbuf, ctx->errbufsz, fmt, ap);
	va_end(ap);
	ctx->err = !0;
}

#define parse_err(fmt, ...)			\
	do {					\
		do_parse_err(ctx, fmt, ##__VA_ARGS__);	\
		YYABORT;				\
 	} while (0)

static
struct evtr_variable *
evtr_var_new(const char *name)
{
	struct evtr_variable *var;

	var = calloc(1, sizeof(*var));
	if (var) {
		if (!(var->name = strdup(name))) {
			free(var);
			return NULL;
		}
		var->val.type = EVTR_VAL_NIL;
	}
	return var;
}

/*
 * XXX: should be reentrant
 */
static
char *
uniq_varname(void)
{
	static long serno;
	static char buf[100];

	serno++;
	snprintf(buf, sizeof(buf), "@%ld", serno);
	return &buf[0];
}

static
int
index_hash(struct ktrfmt_parse_ctx *ctx, const char *hashname,
	   evtr_variable_value_t val, evtr_var_t *_var)
{
	evtr_var_t hsh, var;
	uintptr_t ret, key;
	hsh = symtab_find(ctx->symtab, hashname);
	if (hsh->val.type == EVTR_VAL_NIL) {
		/* it's probably the first time we see this "variable" */
		printd(PARSE, "creating hash for %s\n", hsh->name);
		hsh->val.type = EVTR_VAL_HASH;
		hsh->val.hashtab = hash_new();
	} else if (hsh->val.type != EVTR_VAL_HASH) {
		printd(PARSE, "trying to use type %d as hash\n", hsh->val.type);
		return !0;
	}
	if (val->type == EVTR_VAL_INT) {
		key = val->num;
		printd(PARSE, "looking up %s[%jd] in %p\n", hsh->name,
		       val->num, hsh->val.hashtab);
	} else if (val->type == EVTR_VAL_STR) {
		key = (uintptr_t)val->str;
		printd(PARSE, "looking up %s[\"%s\"] in %p\n", hsh->name,
		       val->str, hsh->val.hashtab);
	} else {
		do_parse_err(ctx, "trying to index hash '%s' with "
			     "non-supported value", hashname);
		return !0;
	}

      	if (hash_find(hsh->val.hashtab, key, &ret)) {
		printd(PARSE, "didn't find it\n");
		var = evtr_var_new(uniq_varname());
		if (var) {
			printd(PARSE, "inserting it as %s\n", var->name);
			if (!hash_insert(hsh->val.hashtab, key,
					 (uintptr_t)var)) {
				do_parse_err(ctx, "can't insert temporary "
					"variable into hash\n");
				return !0;
			}
			symtab_insert(ctx->symtab, var->name, var);
		} else {
			do_parse_err(ctx, "out of memory");
		}
	} else {
		var = (struct evtr_variable *)ret;
	}
	if (!var) {
		fprintf(stderr, "no var!\n");
		return !0;
		/* XXX */
	}
	*_var = var;
	return 0;
}

%}

%verbose
%error-verbose
%debug
%name-prefix "__ktrfmt_"
%define api.pure
%parse-param{struct ktrfmt_parse_ctx *ctx}

%union {
	struct token *tok;
	struct evtr_variable *var;
	struct evtr_variable_value *val;
	void *na;
}

%token<tok> TOK_ID
%token<tok> TOK_CTOR
%token<tok> TOK_INT
%token<tok> TOK_STR

%token<na> TOK_EQ
%token<na> TOK_LEFT_BRACK
%token<na> TOK_RIGHT_BRACK
%token<na> TOK_DOT

%type<var> constant
%type<var> ctor_args
%type<var> construct_expr
%type<var> primary_expr
%type<var> postfix_expr
%type<var> unary_expr
%type<na> assign_expr
%type<na> expr

%%

input: stmt

stmt: unary_expr {
	ctx->var = $1;
 }
     | expr
     ;
constant: TOK_INT {
	evtr_var_t var;
	if (!$1->str)
		parse_err("out of memory");
	var = evtr_var_new(uniq_varname());
	var->val.type = EVTR_VAL_INT;
	errno = 0;
	var->val.num = strtoll($1->str, NULL, 0);
	if (errno) {
		parse_err("Can't parse numeric constant '%s'", $1->str);
	}
	$$ = var;
	tok_free($1);
	}
	| TOK_STR {
	evtr_var_t var;
	if (!$1->str)
		parse_err("out of memory");
	var = evtr_var_new(uniq_varname());
	var->val.type = EVTR_VAL_STR;
	var->val.str = $1->str;
	if (!var->val.str) {
		parse_err("out of memory");
	}
	$$ = var;
	tok_free($1);
	}
	;
ctor_args: constant {
	evtr_var_t ctor;
	ctor = evtr_var_new(uniq_varname());
	ctor->val.type = EVTR_VAL_CTOR;
	ctor->val.ctor.name = NULL;
	TAILQ_INIT(&ctor->val.ctor.args);
	TAILQ_INSERT_HEAD(&ctor->val.ctor.args, &$1->val, link);
	$$ = ctor;
 }
| constant ctor_args {
	TAILQ_INSERT_HEAD(&$2->val.ctor.args, &$1->val, link);
	$$ = $2;
 }
;
construct_expr: TOK_CTOR {
	evtr_var_t var;
	if (!$1->str)
		parse_err("out of memory");
	printd(PARSE, "TOK_CTOR\n");
	printd(PARSE, "tok: %p, str = %p\n", $1, $1->str);
	var = evtr_var_new(uniq_varname());
	var->val.type = EVTR_VAL_CTOR;
	var->val.ctor.name = $1->str;
	TAILQ_INIT(&var->val.ctor.args);
	tok_free($1);
	$$ = var;
 }
| TOK_CTOR ctor_args {
	evtr_variable_value_t val;
	if (!$1->str)
		parse_err("out of memory");
	printd(PARSE, "TOK_CTOR\n");
	printd(PARSE, "tok: %p, str = %p\n", $1, $1->str);
	$2->val.ctor.name = $1->str;
	$$ = $2;
	printd(PARSE, "CTOR: %s\n", $1->str);
	TAILQ_FOREACH(val, &$2->val.ctor.args, link) {
		switch (val->type) {
		case EVTR_VAL_INT:
			printd(PARSE, "\t%jd\n", val->num);
			break;
		case EVTR_VAL_STR:
			printd(PARSE, "\t\"%s\"\n", val->str);
			break;
		case EVTR_VAL_NIL:
			assert(!"can't get here");
		default:
			;
		}
	}
 }
;
primary_expr: TOK_ID {
	evtr_var_t var;
	if (!$1->str)
		parse_err("out of memory");
	printd(PARSE, "TOK_ID\n");
	printd(PARSE, "tok: %p, str = %p\n", $1, $1->str);
	var = symtab_find(ctx->symtab, $1->str);
	if (!var) {
		if (!(var = evtr_var_new($1->str))) {
			tok_free($1);
			parse_err("out of memory");
		}
		printd(PARSE, "creating var %s\n", $1->str);
		symtab_insert(ctx->symtab, $1->str, var);
	}
	$$ = var;
	tok_free($1);
 }
| constant {
	$$ = $1;
  }
;
postfix_expr: postfix_expr TOK_LEFT_BRACK postfix_expr TOK_RIGHT_BRACK {
	evtr_var_t var;

	if (index_hash(ctx, $1->name, &$3->val, &var))
		YYABORT;
	$$ = var;
 }
| postfix_expr TOK_DOT TOK_ID {
	evtr_var_t var, tmp;
	if (!$3->str)
		parse_err("out of memory");
	tmp = evtr_var_new(uniq_varname());
	tmp->val.type = EVTR_VAL_STR;
	tmp->val.str = $3->str;

	if (index_hash(ctx, $1->name, &tmp->val, &var))
		YYABORT;
	tok_free($3);
	$$ = var;
 }
            | primary_expr {
	$$ = $1;
 }
;
unary_expr: postfix_expr {
	$$ = $1;
 }
;
assign_expr: unary_expr TOK_EQ constant {
	$1->val = $3->val;
	ctx->ev->type = EVTR_TYPE_STMT;
	ctx->ev->stmt.var = $1;
	ctx->ev->stmt.val = &$3->val;
	ctx->ev->stmt.op = EVTR_OP_SET;
 }
| unary_expr TOK_EQ construct_expr {
	$1->val = $3->val;
	ctx->ev->type = EVTR_TYPE_STMT;
	ctx->ev->stmt.var = $1;
	ctx->ev->stmt.val = &$3->val;
	ctx->ev->stmt.op = EVTR_OP_SET;
 }
;
expr: assign_expr {
	$$ = $1;
 }
;

%%

void * __ktrfmt_scan_string(const char *);
void __ktrfmt_delete_buffer(void *);

void
__ktrfmt_error (struct ktrfmt_parse_ctx *ctx, const char *s)
{
	do_parse_err(ctx, "%s", s);
}

int
parse_string(evtr_event_t ev, struct symtab *symtab, const char *str,
	     char *errbuf, size_t errbufsz)
{
	void *bufstate;
	int ret;
	struct ktrfmt_parse_ctx ctx;

	printd(PARSE, "parsing \"%s\"\n", str);
	ctx.ev = ev;
	ctx.symtab = symtab;
	ctx.errbuf = errbuf;
	ctx.errbuf[0] = '\0';
	ctx.errbufsz = errbufsz;
	ctx.err = 0;
	bufstate = __ktrfmt_scan_string(str);
	ret = __ktrfmt_parse(&ctx);
	__ktrfmt_delete_buffer(bufstate);

	return ret;
}

int
parse_var(const char *str, struct symtab *symtab, struct evtr_variable **var,
	  char *errbuf, size_t errbufsz)
{
	void *bufstate;
	int ret;
	struct ktrfmt_parse_ctx ctx;

	printd(PARSE, "parsing \"%s\"\n", str);
	ctx.ev = NULL;
	ctx.symtab = symtab;
	ctx.var = NULL;
	ctx.errbuf = errbuf;
	ctx.errbuf[0] = '\0';
	ctx.errbufsz = errbufsz;
	ctx.err = 0;
	bufstate = __ktrfmt_scan_string(str);
	ret = __ktrfmt_parse(&ctx);
	__ktrfmt_delete_buffer(bufstate);

	*var = ctx.var;
	return ret;
}
