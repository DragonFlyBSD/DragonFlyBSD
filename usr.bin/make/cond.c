/*-
 * Copyright (c) 1988, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#)cond.c	8.2 (Berkeley) 1/2/94
 * $FreeBSD: src/usr.bin/make/cond.c,v 1.39 2005/02/07 07:49:16 harti Exp $
 * $DragonFly: src/usr.bin/make/cond.c,v 1.51 2005/09/24 07:38:03 okumoto Exp $
 */

/*
 * Functions to handle conditionals in a makefile.
 *
 * Interface:
 *	Cond_Eval	Evaluate the conditional in the passed line.
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "buf.h"
#include "cond.h"
#include "dir.h"
#include "globals.h"
#include "GNode.h"
#include "make.h"
#include "parse.h"
#include "str.h"
#include "targ.h"
#include "util.h"
#include "var.h"

/*
 * The parsing of conditional expressions is based on this grammar:
 *	E -> F || E
 *	E -> F
 *	F -> T && F
 *	F -> T
 *	T -> defined(variable)
 *	T -> make(target)
 *	T -> exists(file)
 *	T -> empty(varspec)
 *	T -> target(name)
 *	T -> symbol
 *	T -> $(varspec) op value
 *	T -> $(varspec) == "string"
 *	T -> $(varspec) != "string"
 *	T -> ( E )
 *	T -> ! T
 *	op -> == | != | > | < | >= | <=
 *
 * 'symbol' is some other symbol to which the default function (condDefProc)
 * is applied.
 *
 * Tokens are scanned from the 'condExpr' string. The scanner (CondToken)
 * will return And for '&' and '&&', Or for '|' and '||', Not for '!',
 * LParen for '(', RParen for ')' and will evaluate the other terminal
 * symbols, using either the default function or the function given in the
 * terminal, and return the result as either True or False.
 *
 * All Non-Terminal functions (CondE, CondF and CondT) return Err on error.
 */
typedef enum {
	And,
	Or,
	Not,
	True,
	False,
	LParen,
	RParen,
	EndOfFile,
	None,
	Err
} Token;

typedef bool CondProc(Parser *, int, char *);

/*-
 * Structures to handle elegantly the different forms of #if's. The
 * last two fields are stored in condInvert and condDefProc, respectively.
 */
static void CondPushBack(Token);
static int CondGetArg(char **, char **, const char *, bool);
static CondProc	CondDoDefined;
static CondProc	CondDoMake;
static CondProc	CondDoExists;
static CondProc	CondDoTarget;
static char *CondCvtArg(char *, double *);
static Token CondToken(Parser *, bool);
static Token CondT(Parser *, bool);
static Token CondF(Parser *, bool);
static Token CondE(Parser *, bool);

static const struct If {
	bool	doNot;		/* true if default function should be negated */
	CondProc *defProc;	/* Default function to apply */
	bool	isElse;		/* actually el<XXX> */
} ifs[] = {
	[COND_IF] =		{ false,	CondDoDefined,	false },
	[COND_IFDEF] =		{ false,	CondDoDefined,	false },
	[COND_IFNDEF] =		{ true,		CondDoDefined,	false },
	[COND_IFMAKE] =		{ false,	CondDoMake,	false },
	[COND_IFNMAKE] =	{ true,		CondDoMake,	false },
	[COND_ELIF] =		{ false,	CondDoDefined,	true },
	[COND_ELIFDEF] =	{ false,	CondDoDefined,	true },
	[COND_ELIFNDEF] =	{ true,		CondDoDefined,	true },
	[COND_ELIFMAKE] =	{ false,	CondDoMake,	true },
	[COND_ELIFNMAKE] =	{ true,		CondDoMake,	true },
};

static bool	condInvert;	/* Invert the default function */
static CondProc	*condDefProc;	/* default function to apply */
static char	*condExpr;	/* The expression to parse */
static Token	condPushBack = None; /* Single push-back token in parsing */

#define MAXIF	30	/* greatest depth of #if'ing */

static bool	condStack[MAXIF];	/* Stack of conditionals's values */
static int	condLineno[MAXIF];	/* Line numbers of the opening .if */
static int	condTop = MAXIF;	/* Top-most conditional */
static int	skipIfLevel = 0;	/* Depth of skipped conditionals */
static int	skipIfLineno[MAXIF];	/* Line numbers of skipped .ifs */
bool		skipLine = false;	/* Whether the parse module is skipping
					 * lines */

/**
 * CondPushBack
 *	Push back the most recent token read. We only need one level of
 *	this, so the thing is just stored in 'condPushback'.
 *
 * Side Effects:
 *	condPushback is overwritten.
 */
static void
CondPushBack(Token t)
{

	condPushBack = t;
}

/**
 * CondGetArg
 *	Find the argument of a built-in function.  parens is set to true
 *	if the arguments are bounded by parens.
 *
 * Results:
 *	The length of the argument and the address of the argument.
 *
 * Side Effects:
 *	The pointer is set to point to the closing parenthesis of the
 *	function call.
 */
static int
CondGetArg(char **linePtr, char **argPtr, const char *func, bool parens)
{
	char	*cp;
	size_t	argLen;
	Buffer	*buf;

	cp = *linePtr;
	if (parens) {
		while (*cp != OPEN_PAREN && *cp != '\0') {
			cp++;
		}
		if (*cp == OPEN_PAREN) {
			cp++;
		}
	}

	if (*cp == '\0') {
		/*
		 * No arguments whatsoever. Because 'make' and 'defined'
		 * aren't really "reserved words", we don't print a message.
		 * I think this is better than hitting the user with a warning
		 * message every time s/he uses the word 'make' or 'defined'
		 * at the beginning of a symbol...
		 */
		*argPtr = cp;
		return (0);
	}

	while (*cp == ' ' || *cp == '\t') {
		cp++;
	}

	/*
	 * Create a buffer for the argument and start it out at 16 characters
	 * long. Why 16? Why not?
	 */
	buf = Buf_Init(16);

	while ((strchr(" \t)&|", *cp) == NULL) && (*cp != '\0')) {
		if (*cp == '$') {
			/*
			 * Parse the variable spec and install it as part of
			 * the argument if it's valid. We tell Var_Parse to
			 * complain on an undefined variable, so we don't do
			 * it too. Nor do we return an error, though perhaps
			 * we should...
			 */
			char	*cp2;
			size_t	len = 0;
			bool	doFree;

			cp2 = Var_Parse(cp, VAR_CMD, true, &len, &doFree);

			Buf_Append(buf, cp2);
			if (doFree) {
				free(cp2);
			}
			cp += len;
		} else {
			Buf_AddByte(buf, *cp);
			cp++;
		}
	}

	while (*cp == ' ' || *cp == '\t') {
		cp++;
	}

	Buf_AddByte(buf, '\0');
	*argPtr = Buf_Data(buf);
	argLen = Buf_Size(buf);
	Buf_Destroy(buf, false);

	if (parens) {
		if (*cp == CLOSE_PAREN) {
			/*
			 * Advance pointer past close parenthesis.
			 */
			cp++;
			*linePtr = cp;
			return (argLen);
		} else {
			Parse_Error(PARSE_WARNING,
			    "Missing closing parenthesis for %s()", func);
			return (0);
			/* XXX memory leak of argPtr? */
		}
	} else {
		*linePtr = cp;
		return (argLen);
	}
}

/**
 * CondDoDefined
 *	Handle the 'defined' function for conditionals.
 *
 * Results:
 *	true if the given variable is defined.
 */
static bool
CondDoDefined(Parser *parser __unused, int argLen, char *arg)
{
	char	savec = arg[argLen];
	bool	result;

	arg[argLen] = '\0';
	if (Var_Value(arg, VAR_CMD) != NULL) {
		result = true;
	} else {
		result = false;
	}
	arg[argLen] = savec;
	return (result);
}

/**
 * CondDoMake
 *	Handle the 'make' function for conditionals.
 *
 * Results:
 *	true if the given target is being made.
 */
static bool
CondDoMake(Parser *parser, int argLen, char *arg)
{
	char	savec = arg[argLen];
	bool	result;
	const LstNode *ln;

	arg[argLen] = '\0';
	result = false;
	LST_FOREACH(ln, parser->create) {
		if (Str_Match(Lst_Datum(ln), arg)) {
			result = true;
			break;
		}
	}
	arg[argLen] = savec;
	return (result);
}

/**
 * CondDoExists
 *	See if the given file exists.
 *
 * Results:
 *	true if the file exists and false if it does not.
 */
static bool
CondDoExists(Parser *parser __unused, int argLen, char *arg)
{
	char	savec = arg[argLen];
	bool	result;
	char	*path;

	arg[argLen] = '\0';
	path = Path_FindFile(arg, &dirSearchPath);
	if (path != NULL) {
		result = true;
		free(path);
	} else {
		result = false;
	}
	arg[argLen] = savec;
	return (result);
}

/**
 * CondDoTarget
 *	See if the given node exists and is an actual target.
 *
 * Results:
 *	true if the node exists as a target and false if it does not.
 */
static bool
CondDoTarget(Parser *parser __unused, int argLen, char *arg)
{
	char	savec = arg[argLen];
	bool	result;
	GNode	*gn;

	arg[argLen] = '\0';
	gn = Targ_FindNode(arg, TARG_NOCREATE);
	if ((gn != NULL) && !OP_NOP(gn->type)) {
		result = true;
	} else {
		result = false;
	}
	arg[argLen] = savec;
	return (result);
}

/**
 * CondCvtArg
 *	Convert the given number into a double. If the number begins
 *	with 0x, it is interpreted as a hexadecimal integer
 *	and converted to a double from there. All other strings just have
 *	strtod called on them.
 *
 * Results:
 *	Sets 'value' to double value of string.
 *	Returns address of the first character after the last valid
 *	character of the converted number.
 *
 * Side Effects:
 *	Can change 'value' even if string is not a valid number.
 */
static char *
CondCvtArg(char *str, double *value)
{

	if ((*str == '0') && (str[1] == 'x')) {
		long i;

		for (str += 2, i = 0; ; str++) {
			int x;

			if (isdigit((unsigned char)*str))
				x  = *str - '0';
			else if (isxdigit((unsigned char)*str))
				x = 10 + *str -
				    isupper((unsigned char)*str) ? 'A' : 'a';
			else {
				*value = (double)i;
				return (str);
			}
			i = (i << 4) + x;
		}

	} else {
		char *eptr;

		*value = strtod(str, &eptr);
		return (eptr);
	}
}

/**
 * CondToken
 *	Return the next token from the input.
 *
 * Results:
 *	A Token for the next lexical token in the stream.
 *
 * Side Effects:
 *	condPushback will be set back to None if it is used.
 */
static Token
CondToken(Parser *parser, bool doEval)
{
	Token	t;

	if (condPushBack != None) {
		t = condPushBack;
		condPushBack = None;
		return (t);
	}

	while (*condExpr == ' ' || *condExpr == '\t') {
		condExpr++;
	}

	switch (*condExpr) {
	case OPEN_PAREN:
		t = LParen;
		condExpr++;
		break;
	case CLOSE_PAREN:
		t = RParen;
		condExpr++;
		break;
	case '|':
		if (condExpr[1] == '|') {
			condExpr++;
		}
		condExpr++;
		t = Or;
		break;
	case '&':
		if (condExpr[1] == '&') {
			condExpr++;
		}
		condExpr++;
		t = And;
		break;
	case '!':
		t = Not;
		condExpr++;
		break;
	case '\n':
	case '\0':
		t = EndOfFile;
		break;
	case '$':{
			char		*lhs;
			const char	*op;
			char		*rhs;
			char		zero[] = "0";
			size_t		varSpecLen = 0;
			bool		doFree;

			/*
			 * Parse the variable spec and skip over it, saving
			 * its value in lhs.
			 */
			t = Err;
			lhs = Var_Parse(condExpr, VAR_CMD, doEval,
					&varSpecLen, &doFree);
			if (lhs == var_Error) {
				/*
				 * Even if !doEval, we still report syntax
				 * errors, which is what getting var_Error
				 * back with !doEval means.
				 */
				return (Err);
			}
			condExpr += varSpecLen;

			if (!isspace((unsigned char)*condExpr) &&
			    strchr("!=><", *condExpr) == NULL) {
				Buffer *buf;

				buf = Buf_Init(0);

				Buf_Append(buf, lhs);

				if (doFree)
					free(lhs);

				for (; *condExpr &&
				     !isspace((unsigned char)*condExpr);
				     condExpr++)
					Buf_AddByte(buf, *condExpr);

				Buf_AddByte(buf, '\0');
				lhs = Buf_Data(buf);
				Buf_Destroy(buf, false);

				doFree = true;
			}

			/*
			 * Skip whitespace to get to the operator
			 */
			while (isspace((unsigned char)*condExpr))
				condExpr++;

			/*
			 * Make sure the operator is a valid one. If it isn't
			 * a known relational operator, pretend we got a != 0
			 * comparison.
			 */
			op = condExpr;
			switch (*condExpr) {
			case '!':
			case '=':
			case '<':
			case '>':
				if (condExpr[1] == '=') {
					condExpr += 2;
				} else {
					condExpr += 1;
				}
				while (isspace((unsigned char)*condExpr)) {
					condExpr++;
				}
				if (*condExpr == '\0') {
					Parse_Error(PARSE_WARNING,
					     "Missing right-hand-side of operator");
					goto error;
				}
				rhs = condExpr;
				break;
			default:
				op = "!=";
				rhs = zero;
				break;
			}

			if (*rhs == '"') {
				/*
				 * Doing a string comparison. Only allow ==
				 * and != for * operators.
				 */
				char   *string;
				char   *cp, *cp2;
				int	qt;
				Buffer *buf;

		do_string_compare:
				if (((*op != '!') && (*op != '=')) ||
				    (op[1] != '=')) {
					Parse_Error(PARSE_WARNING,
					"String comparison operator should "
						    "be either == or !=");
					goto error;
				}
				buf = Buf_Init(0);
				qt = *rhs == '"' ? 1 : 0;

				for (cp = &rhs[qt];
				     ((qt && (*cp != '"')) ||
				    (!qt && strchr(" \t)", *cp) == NULL)) &&
				     (*cp != '\0'); cp++) {
					if ((*cp == '\\') && (cp[1] != '\0')) {
						/*
						 * Backslash escapes things
						 * -- skip over next
						 * character,							 *
						 * if it exists.
						 */
						cp++;
						Buf_AddByte(buf, *cp);

					} else if (*cp == '$') {
						size_t	len = 0;
						bool	freeIt;

						cp2 = Var_Parse(cp, VAR_CMD,
						     doEval, &len, &freeIt);
						if (cp2 != var_Error) {
							Buf_Append(buf, cp2);
							if (freeIt) {
								free(cp2);
							}
							cp += len - 1;
						} else {
							Buf_AddByte(buf, *cp);
						}
					} else {
						Buf_AddByte(buf, *cp);
					}
				}

				string = Buf_Peel(buf);

				DEBUGF(COND, ("lhs = \"%s\", rhs = \"%s\", "
					   "op = %.2s\n", lhs, string, op));
				/*
				 * Null-terminate rhs and perform the
				 * comparison. t is set to the result.
				 */
				if (*op == '=') {
					t = strcmp(lhs, string) ? False : True;
				} else {
					t = strcmp(lhs, string) ? True : False;
				}
				free(string);
				if (rhs == condExpr) {
					if (*cp == '\0' || (!qt && *cp == CLOSE_PAREN))
						condExpr = cp;
					else
						condExpr = cp + 1;
				}
			} else {
				/*
				 * rhs is either a float or an integer.
				 * Convert both the lhs and the rhs to a
				 * double and compare the two.
				 */
				double	left, right;
				char   *string;

				if (*CondCvtArg(lhs, &left) != '\0')
					goto do_string_compare;
				if (*rhs == '$') {
					size_t	len = 0;
					bool	freeIt;

					string = Var_Parse(rhs, VAR_CMD, doEval,
							   &len, &freeIt);
					if (string == var_Error) {
						right = 0.0;
					} else {
						if (*CondCvtArg(string,
							  &right) != '\0') {
							if (freeIt)
								free(string);
							goto do_string_compare;
						}
						if (freeIt)
							free(string);
						if (rhs == condExpr)
							condExpr += len;
					}
				} else {
					char   *c = CondCvtArg(rhs, &right);

					if (c == rhs)
						goto do_string_compare;
					if (rhs == condExpr) {
						/*
						 * Skip over the right-hand
						 * side
						 */
						condExpr = c;
					}
				}

				DEBUGF(COND, ("left = %f, right = %f, "
					   "op = %.2s\n", left, right, op));
				switch (op[0]) {
				case '!':
					if (op[1] != '=') {
						Parse_Error(PARSE_WARNING,
							"Unknown operator");
						goto error;
					}
					t = (left != right ? True : False);
					break;
				case '=':
					if (op[1] != '=') {
						Parse_Error(PARSE_WARNING,
							"Unknown operator");
						goto error;
					}
					t = (left == right ? True : False);
					break;
				case '<':
					if (op[1] == '=') {
						t = (left <= right ? True : False);
					} else {
						t = (left < right ? True : False);
					}
					break;
				case '>':
					if (op[1] == '=') {
						t = (left >= right ? True : False);
					} else {
						t = (left > right ? True : False);
					}
					break;
				default:
					break;
				}
			}
	error:
			if (doFree)
				free(lhs);
			break;
		}

	default:{
			CondProc *evalProc;
			bool	invert = false;
			char   *arg;
			int	arglen;

			if (strncmp(condExpr, "defined", 7) == 0) {
				/*
				 * Use CondDoDefined to evaluate the argument
				 * and CondGetArg to extract the argument
				 * from the 'function call'.
				 */
				evalProc = CondDoDefined;
				condExpr += 7;
				arglen = CondGetArg(&condExpr, &arg,
						    "defined", true);
				if (arglen == 0) {
					condExpr -= 7;
					goto use_default;
				}
			} else if (strncmp(condExpr, "make", 4) == 0) {
				/*
				 * Use CondDoMake to evaluate the argument
				 * and CondGetArg to extract the argument
				 * from the 'function call'.
				 */
				evalProc = CondDoMake;
				condExpr += 4;
				arglen = CondGetArg(&condExpr, &arg,
						    "make", true);
				if (arglen == 0) {
					condExpr -= 4;
					goto use_default;
				}
			} else if (strncmp(condExpr, "exists", 6) == 0) {
				/*
				 * Use CondDoExists to evaluate the argument
				 * and CondGetArg to extract the argument
				 * from the 'function call'.
				 */
				evalProc = CondDoExists;
				condExpr += 6;
				arglen = CondGetArg(&condExpr, &arg,
						    "exists", true);
				if (arglen == 0) {
					condExpr -= 6;
					goto use_default;
				}
			} else if (strncmp(condExpr, "empty", 5) == 0) {
				/*
				 * Use Var_Parse to parse the spec in parens
				 * and return True if the resulting string is
				 * empty.
				 */
				size_t	length;
				bool	doFree;
				char   *val;

				condExpr += 5;

				for (arglen = 0;
				     condExpr[arglen] != OPEN_PAREN &&
				     condExpr[arglen] != '\0'; arglen += 1)
					continue;

				if (condExpr[arglen] != '\0') {
					length = 0;
					val = Var_Parse(&condExpr[arglen - 1],
					  VAR_CMD, false, &length, &doFree);
					if (val == var_Error) {
						t = Err;
					} else {
						/*
						 * A variable is empty when
						 * it just contains spaces...
						 * 4/15/92, christos
						 */
						char   *p;

						for (p = val;
						     *p &&
						 isspace((unsigned char)*p);
						     p++)
							continue;
						t = (*p == '\0') ? True : False;
					}
					if (doFree) {
						free(val);
					}
					/*
					 * Advance condExpr to beyond the
					 * closing ). Note that we subtract
					 * one from arglen + length b/c
					 * length is calculated from
					 * condExpr[arglen - 1].
					 */
					condExpr += arglen + length - 1;
				} else {
					condExpr -= 5;
					goto use_default;
				}
				break;

			} else if (strncmp(condExpr, "target", 6) == 0) {
				/*
				 * Use CondDoTarget to evaluate the argument
				 * and CondGetArg to extract the argument
				 * from the 'function call'.
				 */
				evalProc = CondDoTarget;
				condExpr += 6;
				arglen = CondGetArg(&condExpr, &arg,
						    "target", true);
				if (arglen == 0) {
					condExpr -= 6;
					goto use_default;
				}
			} else {
				/*
				 * The symbol is itself the argument to the
				 * default function. We advance condExpr to
				 * the end of the symbol by hand (the next
				 * whitespace, closing paren or binary
				 * operator) and set to invert the evaluation
				 * function if condInvert is true.
				 */
		use_default:
				invert = condInvert;
				evalProc = condDefProc;
				arglen = CondGetArg(&condExpr, &arg, "", false);
			}

			/*
			 * Evaluate the argument using the set function. If
			 * invert is true, we invert the sense of the
			 * function.
			 */
			t = (!doEval || evalProc(parser, arglen, arg) ?
			     (invert ? False : True) :
			     (invert ? True : False));
			free(arg);
			break;
		}
	}
	return (t);
}

/**
 * CondT
 *	Parse a single term in the expression. This consists of a terminal
 *	symbol or Not and a terminal symbol (not including the binary
 *	operators):
 *	    T -> defined(variable) | make(target) | exists(file) | symbol
 *	    T -> ! T | ( E )
 *
 * Results:
 *	True, False or Err.
 *
 * Side Effects:
 *	Tokens are consumed.
 */
static Token
CondT(Parser *parser, bool doEval)
{
	Token	t;

	t = CondToken(parser, doEval);
	if (t == EndOfFile) {
		/*
		 * If we reached the end of the expression, the expression
		 * is malformed...
		 */
		t = Err;
	} else if (t == LParen) {
		/*
		 * T -> ( E )
		 */
		t = CondE(parser, doEval);
		if (t != Err) {
			if (CondToken(parser, doEval) != RParen) {
				t = Err;
			}
		}
	} else if (t == Not) {
		t = CondT(parser, doEval);
		if (t == True) {
			t = False;
		} else if (t == False) {
			t = True;
		}
	}
	return (t);
}

/**
 * CondF --
 *	Parse a conjunctive factor (nice name, wot?)
 *	    F -> T && F | T
 *
 * Results:
 *	True, False or Err
 *
 * Side Effects:
 *	Tokens are consumed.
 */
static Token
CondF(Parser *parser, bool doEval)
{
	Token	l, o;

	l = CondT(parser, doEval);
	if (l != Err) {
		o = CondToken(parser, doEval);

		if (o == And) {
			/*
			 * F -> T && F
			 *
			 * If T is False, the whole thing will be False, but
			 * we have to parse the r.h.s. anyway (to throw it
			 * away). If T is True, the result is the r.h.s.,
			 * be it an Err or no.
			 */
			if (l == True) {
				l = CondF(parser, doEval);
			} else {
				CondF(parser, false);
			}
		} else {
			/*
			 * F -> T
			 */
			CondPushBack(o);
		}
	}
	return (l);
}

/**
 * CondE --
 *	Main expression production.
 *	    E -> F || E | F
 *
 * Results:
 *	True, False or Err.
 *
 * Side Effects:
 *	Tokens are, of course, consumed.
 */
static Token
CondE(Parser *parser, bool doEval)
{
	Token   l, o;

	l = CondF(parser, doEval);
	if (l != Err) {
		o = CondToken(parser, doEval);

		if (o == Or) {
			/*
			 * E -> F || E
			 *
			 * A similar thing occurs for ||, except that here we
			 * make sure the l.h.s. is False before we bother to
			 * evaluate the r.h.s. Once again, if l is False, the
			 * result is the r.h.s. and once again if l is True,
			 * we parse the r.h.s. to throw it away.
			 */
			if (l == False) {
				l = CondE(parser, doEval);
			} else {
				CondE(parser, false);
			}
		} else {
			/*
			 * E -> F
			 */
			CondPushBack(o);
		}
	}
	return (l);
}

/**
 * Cond_If
 *	Handle .if<X> and .elif<X> directives.
 *	This function is called even when we're skipping.
 */
void
Cond_If(Parser *parser, char *line, int code, int lineno)
{
	const struct If	*ifp;
	bool value;

	ifp = &ifs[code];

	if (ifp->isElse) {
		if (condTop == MAXIF) {
			Parse_Error(PARSE_FATAL, "if-less elif");
			return;
		}
		if (skipIfLevel != 0) {
			/*
			 * If skipping this conditional, just ignore
			 * the whole thing. If we don't, the user
			 * might be employing a variable that's
			 * undefined, for which there's an enclosing
			 * ifdef that we're skipping...
			 */
			skipIfLineno[skipIfLevel - 1] = lineno;
			return;
		}

	} else if (skipLine) {
		/*
		 * Don't even try to evaluate a conditional that's
		 * not an else if we're skipping things...
		 */
		skipIfLineno[skipIfLevel] = lineno;
		skipIfLevel += 1;
		return;
	}

	/*
	 * Initialize file-global variables for parsing
	 */
	condDefProc = ifp->defProc;
	condInvert = ifp->doNot;

	while (*line == ' ' || *line == '\t') {
		line++;
	}

	condExpr = line;
	condPushBack = None;

	switch (CondE(parser, true)) {
	  case True:
		if (CondToken(parser, true) != EndOfFile)
			goto err;
		value = true;
		break;

	  case False:
		if (CondToken(parser, true) != EndOfFile)
			goto err;
		value = false;
		break;

	  case Err:
  err:		Parse_Error(PARSE_FATAL, "Malformed conditional (%s)", line);
		return;

	  default:
		abort();
	}

	if (!ifp->isElse) {
		/* push this value */
		condTop -= 1;

	} else if (skipIfLevel != 0 || condStack[condTop]) {
		/*
		 * If this is an else-type conditional, it should only take
		 * effect if its corresponding if was evaluated and false.
		 * If its if was true or skipped, we return COND_SKIP (and
		 * start skipping in case we weren't already), leaving the
		 * stack unmolested so later elif's don't screw up...
		 */
		skipLine = true;
		return;
	}

	if (condTop < 0) {
		/*
		 * This is the one case where we can definitely proclaim a fatal
		 * error. If we don't, we're hosed.
		 */
		Parse_Error(PARSE_FATAL, "Too many nested if's. %d max.",MAXIF);
		return;
	}

	/* push */
	condStack[condTop] = value;
	condLineno[condTop] = lineno;
	skipLine = !value;
}

/**
 * Cond_Else
 *	Handle .else statement.
 */
void
Cond_Else(Parser *parser __unused, char *line __unused, int code __unused, int lineno __unused)
{

	while (isspace((u_char)*line))
		line++;

	if (*line != '\0' && (warn_flags & WARN_DIRSYNTAX)) {
		Parse_Error(PARSE_WARNING, "junk after .else ignored '%s'",
		    line);
	}

	if (condTop == MAXIF) {
		Parse_Error(PARSE_FATAL, "if-less else");
		return;
	}
	if (skipIfLevel != 0)
		return;

	if (skipIfLevel != 0 || condStack[condTop]) {
		/*
		 * An else should only take effect if its corresponding if was
		 * evaluated and false.
		 * If its if was true or skipped, we return COND_SKIP (and
		 * start skipping in case we weren't already), leaving the
		 * stack unmolested so later elif's don't screw up...
		 * XXX How does this work with two .else's?
		 */
		skipLine = true;
		return;
	}

	/* inverse value */
	condStack[condTop] = !condStack[condTop];
	skipLine = !condStack[condTop];
}

/**
 * Cond_Endif
 *	Handle .endif statement.
 */
void
Cond_Endif(Parser *parser __unused, char *line __unused, int code __unused, int lineno __unused)
{

	while (isspace((u_char)*line))
		line++;

	if (*line != '\0' && (warn_flags & WARN_DIRSYNTAX)) {
		Parse_Error(PARSE_WARNING, "junk after .endif ignored '%s'",
		    line);
	}

	/*
	 * End of a conditional section. If skipIfLevel is non-zero,
	 * that conditional was skipped, so lines following it should
	 * also be skipped. Hence, we return COND_SKIP. Otherwise,
	 * the conditional was read so succeeding lines should be
	 * parsed (think about it...) so we return COND_PARSE, unless
	 * this endif isn't paired with a decent if.
	 */
	if (skipIfLevel != 0) {
		skipIfLevel -= 1;
		return;
	}

	if (condTop == MAXIF) {
		Parse_Error(PARSE_FATAL, "if-less endif");
		return;
	}

	/* pop */
	skipLine = false;
	condTop += 1;
}

/**
 * Cond_End
 *	Make sure everything's clean at the end of a makefile.
 *
 * Side Effects:
 *	Parse_Error will be called if open conditionals are around.
 */
void
Cond_End(Parser *parser __unused, char *line __unused, int code __unused, int lineno __unused)
{
	int level;

	if (condTop != MAXIF) {
		Parse_Error(PARSE_FATAL, "%d open conditional%s:",
		    MAXIF - condTop + skipIfLevel,
		    MAXIF - condTop + skipIfLevel== 1 ? "" : "s");

		for (level = skipIfLevel; level > 0; level--)
			Parse_Error(PARSE_FATAL, "\t%*sat line %d (skipped)",
			    MAXIF - condTop + level + 1, "",
			    skipIfLineno[level - 1]);
		for (level = condTop; level < MAXIF; level++)
			Parse_Error(PARSE_FATAL, "\t%*sat line %d "
			    "(evaluated to %s)", MAXIF - level + skipIfLevel,
			    "", condLineno[level],
			    condStack[level] ? "true" : "false");
	}
	condTop = MAXIF;
}
