#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "util.h"

typedef struct Keyword Keyword;
typedef struct LexState LexState;

static int lineno = 1;
static char *buf, *curr;
static char token_string[MAX_TOKSTR_LEN];

const char *lex_token_string(void)
{
    return token_string;
}

struct LexState {
    int lineno;
    char *curr;
    char token_string[MAX_TOKSTR_LEN];
};

void *lex_get_state(void)
{
    LexState *s;

    s = malloc(sizeof(*s));
    s->lineno = lineno;
    s->curr = curr;
    strcpy(s->token_string, token_string);
    return s;
}

void lex_set_state(void *state)
{
    LexState *s;

    s = state;
    lineno = s->lineno;
    curr = s->curr;
    strcpy(token_string, s->token_string);
}

enum {
#define X(a, b) TOK_ ## a,
#include "tokens.def"
#undef X
    START_KW,
};

static struct {
    int num;
    char *str;
    char *name;
} token_table[] = {
#define X(a, b) { TOK_ ## a, b, # a },
#include "tokens.def"
#undef X
    { -1, NULL, NULL },
};

static struct Keyword {
    int num;
    char *str;
    Keyword *next;
} *keywords;

int lex_keyword(const char *str)
{
    Keyword *p, *t;
    static int keyword_counter;

    for (p=NULL, t=keywords; t != NULL; p=t, t=t->next)
        if (strcmp(t->str, str) == 0)
            return t->num;
    t = malloc(sizeof(*t));
    t->num = START_KW+keyword_counter++;
    t->str = strdup(str);
    t->next = NULL;
    if (p != NULL)
        p->next = t;
    else
        keywords = t;
    return t->num;
}

const char *lex_keyword_iterate(int begin)
{
    char *str;
    static Keyword *curr;

    if (begin)
        curr = keywords;
    str = NULL;
    if (curr != NULL) {
        str = curr->str;
        curr = curr->next;
    }
    return str;
}

static int is_id(const char *s)
{
    if (!isalpha(*s) && *s!='_')
        return 0;
    ++s;
    while (isalnum(*s))
        ++s;
    return *s == '\0';
}

int lex_lineno(void)
{
    return lineno;
}

int lex_str2num(const char *str)
{
    int i;

    if (is_id(str))
        return lex_keyword(str);
    for (i = 0; token_table[i].num >= 0; i++) {
        if (token_table[i].str == NULL)
            continue;
        if (strcmp(token_table[i].str, str) == 0)
            return token_table[i].num;
    }
    return -1;
}

int lex_name2num(const char *name)
{
    int i;

    for (i = 0; token_table[i].name != NULL; i++)
        if (strcmp(token_table[i].name, name) == 0)
            return token_table[i].num;
    return -1;
}

const char *lex_num2print(int num)
{
    int i;

    if (num >= START_KW) {
        Keyword *t;

        for (t = keywords; t != NULL; t = t->next)
            if (t->num == num)
                return t->str;
    } else {
        for (i = 0; token_table[i].num >= 0; i++)
            if (token_table[i].num == num)
                return (token_table[i].str!=NULL)?token_table[i].str:token_table[i].name;
    }
    assert(0);
}

const char *lex_num2name(int num)
{
    int i;

    if (num >= START_KW)
        return lex_num2print(num);
    for (i = 0; token_table[i].num >= 0; i++)
        if (token_table[i].num == num)
            return token_table[i].name;
    assert(0);
}

/* recognize the tokens defined in "tokens.def" */
int lex_get_token(void)
{
    enum {
        START,
        INID,
        INNUM,
        INSTR1,
        INSTR2,
    };
    int state;
    int save, cindx;
    char *str_begin;
    int str_line;

    if (*curr == '\0')
        return TOK_EOF;

    cindx = 0;
    state = START;
    token_string[0] = '\0';
    while (1) {
        int c;

        c = *curr++;
        save = 1;
        switch (state) {
        case START:
            if (c==' ' || c=='\t' || c=='\n') {
                save = 0;
                if (c == '\n')
                    ++lineno;
            } else if (isalpha(c) || c=='_') {
                state = INID;
            }/*else if (c == '!') {
                while (*curr != '\n')
                    ++curr;
                ++curr;
                save = 0;
            }*/else if (isdigit(c)) {
                state = INNUM;
            } else if (c == '\'') {
                state = INSTR1;
                str_begin = curr-1;
                str_line = lineno;
            } else if (c == '\"') {
                state = INSTR2;
                str_begin = curr-1;
                str_line = lineno;
            } else {
                switch (c) {
                case '\0':
                    --curr;
                    return TOK_EOF;
                case '(': return TOK_LPAREN;
                case ')': return TOK_RPAREN;
                case '/': return TOK_DIV;
                case '*': return TOK_MUL;
                case '+': return TOK_PLUS;
                case '-': return TOK_MINUS;
                case '#': return TOK_NEQ;
                case '=': return TOK_EQ;
                case ',': return TOK_COMMA;
                case ';': return TOK_SEMI;
                case '.': return TOK_DOT;
                case '|': return TOK_VBAR;
                case '$': return TOK_DOLLAR;
                case '^': return TOK_CARET;
                case '>':
                    if (*curr == '=') {
                        ++curr;
                        return TOK_GET;
                    }
                    return TOK_GT;
                case '<':
                    if (*curr == '=') {
                        ++curr;
                        return TOK_LET;
                    }
                    return TOK_LT;
                case '{':
                    if (*curr == '{') {
                        ++curr;
                        return TOK_LBRACE2;
                    }
                    return TOK_LBRACE;
                case '}':
                    if (*curr == '}') {
                        ++curr;
                        return TOK_RBRACE2;
                    }
                    return TOK_RBRACE;
                case '[':
                    if (*curr == '[') {
                        ++curr;
                        return TOK_LBRACKET2;
                    }
                    return TOK_LBRACKET;
                case ']':
                    if (*curr == ']') {
                        ++curr;
                        return TOK_RBRACKET2;
                    }
                    return TOK_RBRACKET;
                case ':':
                    if (*curr == '=') {
                        ++curr;
                        return TOK_ASSIGN;
                    } else {
                        return TOK_COLON;
                    }
                default:
                    return TOK_UNKNOWN;
                }
            }
            break; /* START */

        case INID:
            if (!isalnum(c) && c!='_') {
                Keyword *t;

                --curr;
                token_string[cindx] = '\0';
                for (t = keywords; t != NULL; t = t->next)
                    if (strcmp(token_string, t->str) == 0)
                        return t->num;
                return TOK_ID;
            }
            break;

        case INNUM:
            if (!isdigit(c)) {
                --curr;
                token_string[cindx] = '\0';
                return TOK_NUM;
            }
            break;

        case INSTR1:
            if (c == '\'') {
                if (curr[-2] != '\\') {
                    token_string[cindx++] = (char)c;
                    token_string[cindx] = '\0';
                    return TOK_STR1;
                } else {
                    c = '\'';
                    --cindx;
                }
            } else if (c == '\n') {
                ++lineno;
            } else if (c == '\0') {
                curr = str_begin;
                lineno = str_line;
                token_string[0] = '\0';
                return TOK_UNKNOWN;
            }
            break;

        case INSTR2:
            if (c == '\"') {
                if (curr[-2] != '\\') {
                    token_string[cindx++] = (char)c;
                    token_string[cindx] = '\0';
                    return TOK_STR2;
                } else {
                    c = '\"';
                    --cindx;
                }
            } else if (c == '\n') {
                ++lineno;
            } else if (c == '\0') {
                curr = str_begin;
                lineno = str_line;
                token_string[0] = '\0';
                return TOK_UNKNOWN;
            }
            break;

        default:
            assert(0);
            break;
        } /* switch (state) */
        if (save)
            token_string[cindx++] = (char)c;
    }
    assert(0);
}

int lex_init(char *file_path)
{
    if ((buf=read_file(file_path)) == NULL)
        return -1;
    curr = buf;
    return 0;
}

int lex_finish(void)
{
    free(buf);
    return 0;
}
