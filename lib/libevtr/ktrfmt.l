%{

#include <assert.h>
//#define YYSTYPE struct token
#define YY_NO_INPUT
#include <stdlib.h>
#include "internal.h"
#include "ktrfmt.tab.h"
#include "tok.h"

enum {
	NR_TOKENS = 18,
};

/* XXX: need to switch to reentrant lexer */
static struct token tokens[NR_TOKENS];
static int curr_tok;
static struct symtab *strtab;

static
struct token *
tok_new(void)
{
	++curr_tok;
	if (curr_tok == NR_TOKENS) {
		/* can't happen */
		fprintf(stderr, "Reached max number of tokens\n");
		abort();
	}
	return &tokens[curr_tok];
}

void
tok_free(struct token *tok)
{
	assert(&tokens[curr_tok] == tok);
	--curr_tok;
}

/*
 * We keep track of strings we've seen before so string comparison
 * can be done w/ a simple pointer comparison
 */
static
char *
newstr(const char *s)
{
	void *r;
	if (!strtab)
		strtab = symtab_new();
	if ((r = symtab_find(strtab, s)))
		return r;
	if (!(r = strdup(s)))
		return r;
	symtab_insert(strtab, r, r);
	return r;
}

%}

%option prefix="__ktrfmt"
%option outfile="ktrfmt.yy.c"
%option bison-bridge
%option noyywrap
%option nounput

INT	[0-9]+
HEX	0x[0-9a-fA-F]+
WHITE	[ \t\r]
ID	[a-z_$][a-zA-Z0-9_]*
CTOR	[A-Z][a-zA-Z0-9_]*

%%
{WHITE}+ { /* ignore */ }
\"(\\\"|[^"\n])+\" {
	size_t len;
	yylval->tok = tok_new();
	yylval->tok->type = TOK_STR;
	len = strlen(yytext);
	assert(yytext[len - 1] == '"');
	yytext[len - 1] = '\0';	/* kill trailing quote */
	printd(LEX, "newstr(\"%s\")\n", yytext + 1);
	yylval->tok->str = newstr(yytext + 1);	/* parser detects oom */
	yytext[len - 1] = '"';	/* restore quote */
	printd(LEX, "TOK_STR: \"%s\"\n", yylval->tok->str);
	return TOK_STR;
	}
{ID} {
	yylval->tok = tok_new();
	yylval->tok->type = TOK_ID;
	printd(LEX, "tok %p TOK_ID %p:%s\n", yylval->tok, yytext, yytext);
	yylval->tok->str = newstr(yytext);	/* parser detects oom */
	return TOK_ID;
	}
{CTOR} {
	yylval->tok = tok_new();
	yylval->tok->type = TOK_CTOR;
	printd(LEX, "tok %p TOK_CTOR %p:%s\n", yylval->tok, yytext, yytext);
	yylval->tok->str = newstr(yytext);	/* parser detects oom */
	return TOK_CTOR;
	}
{INT} {
	yylval->tok = tok_new();
	yylval->tok->type = TOK_INT;
	yylval->tok->str = strdup(yytext);	/* parser detects oom */
	printd(LEX, "TOK_INT\n");
	return TOK_INT;
	}
{HEX} {
	yylval->tok = tok_new();
	yylval->tok->type = TOK_INT;
	yylval->tok->str = strdup(yytext);	/* parser detects oom */
	printd(LEX, "TOK_INT\n");
	return TOK_INT;
	}
"=" {
	yylval = NULL;
	printd(LEX, "TOK_EQ\n");
	return TOK_EQ;
	}
"." {
	yylval = NULL;
	printd(LEX, "TOK_DOT\n");
	return TOK_DOT;
	}
"[" {
	yylval = NULL;
	printd(LEX, "TOK_LEFT_BRACK\n");
	return TOK_LEFT_BRACK;
	}
"]" {
	yylval = NULL;
	printd(LEX, "TOK_RIGHT_BRACK\n");
	return TOK_RIGHT_BRACK;
	}

%%
