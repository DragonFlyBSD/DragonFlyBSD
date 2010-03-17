%{

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "evtr.h"
#include "tok.h"
#include "ktrfmt.tab.h"
#include "internal.h"

struct ktrfmt_parse_ctx {
	struct symtab *symtab;
	struct evtr_variable *var;
	struct evtr_variable_value *val;
	evtr_event_t ev;
};

int __ktrfmtlex(YYSTYPE *);
#define __ktrfmt_lex __ktrfmtlex

void __ktrfmt_error (struct ktrfmt_parse_ctx *, const char *);

static
struct evtr_variable *
evtr_var_new(const char *name)
{
	struct evtr_variable *var;

	var = calloc(1, sizeof(*var));
	if (var) {
		var->name = strdup(name);
		/* XXX: oom */
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
%token<tok> TOK_INT
%token<tok> TOK_STR

%token<na> TOK_EQ
%token<na> TOK_LEFT_BRACK
%token<na> TOK_RIGHT_BRACK
%token<na> TOK_DOT

%type<var> constant
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
	var = evtr_var_new(uniq_varname());
	var->val.type = EVTR_VAL_INT;
	var->val.num =
		atoll($1->str); /* XXX */
	$$ = var;
	tok_free($1);
	}
	| TOK_STR {
	evtr_var_t var;
	var = evtr_var_new(uniq_varname());
	var->val.type = EVTR_VAL_INT;
	var->val.str = strdup($1->str);
	if (!var->val.str) {
		fprintf(stderr, "oom\n");
		YYABORT;
	}
	$$ = var;
	tok_free($1);
	}
	;

primary_expr: TOK_ID {
	evtr_var_t var;
	printd(PARSE, "TOK_ID\n");
	printd(PARSE, "tok: %p, str = %p\n", $1, $1->str);
	var = symtab_find(ctx->symtab, $1->str);
	if (!var) {
		var = evtr_var_new($1->str); /* XXX: oom */
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
postfix_expr: TOK_ID TOK_LEFT_BRACK postfix_expr TOK_RIGHT_BRACK {
	evtr_var_t hsh, var;
	evtr_variable_value_t val;
	hsh = symtab_find(ctx->symtab, $1->str);
	if (!hsh) {
		printd(PARSE, "creating hash: %s\n", $1->str);
		hsh = evtr_var_new($1->str);
		hsh->val.type = EVTR_VAL_HASH;
		hsh->val.hashtab = hash_new();
		symtab_insert(ctx->symtab, $1->str, hsh);
	}
	if (hsh->val.type != EVTR_VAL_HASH) {
		printd(PARSE, "variable %s does not contain a hash\n", hsh->name);
		YYABORT;
	}
	val = &$3->val;
	if (val->type == EVTR_VAL_INT) {
		uintptr_t ret;
		uintptr_t key = val->num;
		printd(PARSE, "looking up %s[%jd] in %p\n", $1->str, val->num, hsh->val.hashtab);
		/* XXX: should definitely be using uintptr_t for keys/values */
		if (hash_find(hsh->val.hashtab, key, &ret)) {
			printd(PARSE, "didn't find it\n");
			var = evtr_var_new(uniq_varname());
			if (var) {
				printd(PARSE, "inserting it as %s\n", var->name);
				if (!hash_insert(hsh->val.hashtab, key, (uintptr_t)var)) {
					fprintf(stderr, "can't insert tmp "
						"variable into hash\n");
					YYABORT;
				}
			}
		} else {
			var = (struct evtr_variable *)ret;
		}
	} else {
		fprintf(stderr, "trying to index hash w/ non-integral value\n");
		YYABORT;
	}
	if (!var) {
		fprintf(stderr, "no var!\n");
		YYABORT;
		/* XXX */
	}
	tok_free($1);
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
	(void)ctx;
	fprintf(stderr, "%s\n", s);
}

int
parse_string(evtr_event_t ev, struct symtab *symtab, const char *str)
{
	void *bufstate;
	int ret;
	struct ktrfmt_parse_ctx ctx;

	printd(PARSE, "parsing \"%s\"\n", str);
	ctx.ev = ev;
	ctx.symtab = symtab;
	bufstate = __ktrfmt_scan_string(str);
	ret = __ktrfmt_parse(&ctx);
	__ktrfmt_delete_buffer(bufstate);

	return ret;
}

int
parse_var(const char *str, struct symtab *symtab, struct evtr_variable **var)
{
	void *bufstate;
	int ret;
	struct ktrfmt_parse_ctx ctx;

	ctx.ev = NULL;
	ctx.symtab = symtab;
	ctx.var = NULL;
	bufstate = __ktrfmt_scan_string(str);
	ret = __ktrfmt_parse(&ctx);
	__ktrfmt_delete_buffer(bufstate);

	*var = ctx.var;
	return ret;
}
