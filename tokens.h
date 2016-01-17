#ifndef TOKENS_H_
#define TOKENS_H_

enum {
    LEX_PLUS    = 1,
    LEX_MINUS   = 2,
    LEX_DIV     = 3,
    LEX_MUL     = 4,
    LEX_GT      = 5,
    LEX_GET     = 6,
    LEX_LT      = 7,
    LEX_LET     = 8,
    LEX_EQ      = 9,
    LEX_NEQ     = 10,
    LEX_ODD     = 11,

    LEX_BEGIN   = 12,
    LEX_END     = 13,
    LEX_WHILE   = 14,
    LEX_DO      = 15,
    LEX_IF      = 16,
    LEX_THEN    = 17,
    LEX_CALL    = 18,
    LEX_ASSIGN  = 19,

    LEX_PROCEDURE   = 20,
    LEX_VAR         = 21,
    LEX_CONST       = 22,

    LEX_SEMI        = 23,
    LEX_COMMA       = 24,
    LEX_EOF         = 25,
    LEX_LPAREN      = 26,
    LEX_RPAREN      = 27,
    LEX_DOT         = 28,

    LEX_IDENT       = 29,
    LEX_NUMBER      = 30,

    LEX_VBAR        = 31,
    LEX_LBRACE      = 32,
    LEX_RBRACE      = 33,
    LEX_LBRACKET    = 34,
    LEX_RBRACKET    = 35,

    LEX_UNKNOWN = 0,
};

#endif
