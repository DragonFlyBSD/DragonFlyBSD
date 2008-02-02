%{
/* $DragonFly: src/libexec/dma/aliases_scan.l,v 1.1 2008/02/02 18:20:51 matthias Exp $ */

#include <string.h>
#include "aliases_parse.h"
%}

%option yylineno

%%

[^:,#[:space:][:cntrl:]]+	{yylval.ident = strdup(yytext); return T_IDENT;}
[:,\n]				return yytext[0];
^([[:blank:]]*(#.*)?\n)+	;/* ignore empty lines */
(\n?[[:blank:]]+|#.*)+		;/* ignore whitespace and continuation */
\\\n				;/* ignore continuation.  not allowed in comments */
.				return T_ERROR;
<<EOF>>				return T_EOF;

%%
