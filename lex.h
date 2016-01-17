#ifndef LEX_H_
#define LEX_H_

extern int lex_lineno;

int lex_init(char *file_path);
int lex(void);
int lex_finish(void);

#endif
