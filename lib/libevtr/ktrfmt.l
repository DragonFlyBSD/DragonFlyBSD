%{

#include <assert.h>
//#define YYSTYPE struct token
#include "ktrfmt.tab.h"
#include "tok.h"
#include "internal.h"

enum {
	NR_TOKENS = 8,
};

static struct token tokens[NR_TOKENS];
static int curr_tok;

struct token *
tok_new(void)
{
	++curr_tok;
	if (curr_tok == NR_TOKENS) {
		/* can't happen */
		fprintf(stderr, "Reached max number of tokens\n");
		exit(2);
	}
	return &tokens[curr_tok];
}

void
tok_free(struct token *tok)
{
	assert(&tokens[curr_tok] == tok);
	free(tok->str);
	--curr_tok;
}

%}

%option prefix="__ktrfmt"
%option outfile="ktrfmt.yy.c"
%option bison-bridge
%option noyywrap
%option nounput

INT	[0-9]+
WHITE	[ \t\r]
ID	[a-zA-Z_$][a-zA-Z0-9_]*

%%
{WHITE}+ { /* ignore */ }
{ID} {
	yylval->tok = tok_new();
	yylval->tok->type = TOK_ID;
	printd(LEX, "tok %p TOK_ID %p:%s\n", yylval->tok, yytext, yytext);
	yylval->tok->str = strdup(yytext);
	return TOK_ID;
	}
{INT} {
	yylval->tok = tok_new();
	yylval->tok->type = TOK_INT;
	yylval->tok->str = strdup(yytext);
	printd(LEX, "TOK_INT\n");
	return TOK_INT;
	}
"=" {
	yylval = NULL;
	printd(LEX, "TOK_EQ\n");
	return TOK_EQ;
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