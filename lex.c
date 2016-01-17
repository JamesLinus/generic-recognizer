/*
    Example lexer that implements the interface required by the parser.
    It is used by all the grammars in the 'examples/' directory.
*/
#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "tokens.h"

int lex_lineno = 1;
static char *lex_buf, *lex_curr;

#define NKEYWORDS 11
struct {
    int tok;
    char *str;
} keywords[] = { /* PL/0 keywords */
    { LEX_ODD, "odd" },
    { LEX_BEGIN, "begin" },
    { LEX_END, "end" },
    { LEX_WHILE, "while" },
    { LEX_DO, "do" },
    { LEX_IF, "if" },
    { LEX_THEN, "then" },
    { LEX_CALL, "call" },
    { LEX_PROCEDURE, "procedure" },
    { LEX_VAR, "var" },
    { LEX_CONST, "const" },
};

int lex(void)
{
    enum {
        START,
        INID,
        INNUM,
    };
    int state;
    int save, cindx;
    static int eof_reached = 0;
    char lexeme[512];

    if (eof_reached)
        return LEX_EOF;

    cindx = 0;
    state = START;
    while (1) {
        int c;

        c = *lex_curr++;
        save = 1;
        switch (state) {
        case START:
            if (c==' ' || c=='\n') {
                save = 0;
                if (c == '\n')
                    ++lex_lineno;
            } else if (isalpha(c) || c=='_') {
                state = INID;
            }/*else if (c == '!') {
                while (*lex_curr != '\n')
                    ++lex_curr;
                ++lex_curr;
                save = 0;
            }*/else if (isdigit(c)) {
                state = INNUM;
            } else {
                switch (c) {
                case '\0':
                    eof_reached = 1;
                    return LEX_EOF;
                case '(':
                    return LEX_LPAREN;
                case ')':
                    return LEX_RPAREN;
                case '/':
                    return LEX_DIV;
                case '*':
                    return LEX_MUL;
                case '+':
                    return LEX_PLUS;
                case '-':
                    return LEX_MINUS;
                case '>':
                    if (*lex_curr == '=') {
                        ++lex_curr;
                        return LEX_GET;
                    }
                    return LEX_GT;
                case '<':
                    if (*lex_curr == '=') {
                        ++lex_curr;
                        return LEX_LET;
                    }
                    return LEX_LT;
                case '#':
                    return LEX_NEQ;
                case '=':
                    return LEX_EQ;
                case ',':
                    return LEX_COMMA;
                case ';':
                    return LEX_SEMI;
                case '.':
                    return LEX_DOT;
                case '|':
                    return LEX_VBAR;
                case '{':
                    return LEX_LBRACE;
                case '}':
                    return LEX_RBRACE;
                case '[':
                    return LEX_LBRACKET;
                case ']':
                    return LEX_RBRACKET;
                case ':':
                    if (*lex_curr == '=') {
                        ++lex_curr;
                        return LEX_ASSIGN;
                    }
                default:
                    return LEX_UNKNOWN;
                }
            }
            break; /* START */

        case INID:
            if (!isalnum(c) && c!='_') {
                int i;

                --lex_curr;
                lexeme[cindx] = '\0';
                for (i = 0; i < NKEYWORDS; i++)
                    if (strcmp(lexeme, keywords[i].str) == 0)
                        return keywords[i].tok;
                return LEX_IDENT;
            }
            break;

        case INNUM:
            if (!isdigit(c)) {
                --lex_curr;
                return LEX_NUMBER;
            }
            break;

        default:
            assert(0);
            break;
        } /* switch (state) */
        if (save)
            lexeme[cindx++] = (char)c;
    }
    assert(0);
}

int lex_init(char *file_path)
{
    FILE *fp;
    unsigned len;

    if ((fp=fopen(file_path, "rb")) == NULL)
        return -1;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    lex_buf = malloc(len+1);
    len = fread(lex_buf, 1, len, fp);
    lex_buf[len] = '\0';
    fclose(fp);
    lex_curr = lex_buf;
    return 0;
}

int lex_finish(void)
{
    free(lex_buf);
    return 0;
}
