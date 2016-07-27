#ifndef LEX_H_
#define LEX_H_

int lex_init(char *file_path);
int lex_get_token(void);
int lex_finish(void);
int lex_lineno(void);

int lex_name2num(const char *name); /* e.g. "PLUS" -> 1 */
int lex_str2num(const char *str);   /* e.g. "+" -> 1 */
const char *lex_num2print(int num); /* e.g. 1 -> "+" */
const char *lex_num2name(int num);  /* e.g. 1 -> "PLUS" */

int lex_keyword(const char *str);
const char *lex_keyword_iterate(int begin);

#endif
