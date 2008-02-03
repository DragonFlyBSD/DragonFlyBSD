%{
/* $DragonFly: src/libexec/dma/aliases_scan.l,v 1.2 2008/02/03 11:06:17 matthias Exp $ */

#include <string.h>
#include "aliases_parse.h"

int yylex(void);
%}

%option yylineno
%option nounput

%%

[^:,#[:space:][:cntrl:]]+	{yylval.ident = strdup(yytext); return T_IDENT;}
[:,\n]				return yytext[0];
^([[:blank:]]*(#.*)?\n)+	;/* ignore empty lines */
(\n?[[:blank:]]+|#.*)+		;/* ignore whitespace and continuation */
\\\n				;/* ignore continuation.  not allowed in comments */
.				return T_ERROR;
<<EOF>>				return T_EOF;

%%
