#ifndef _TOK_H_
#define _TOK_H_

struct token {
	int type;
	union {
		char *str;
	};
};

void tok_free(struct token *);
#endif /* _TOK_H_ */
