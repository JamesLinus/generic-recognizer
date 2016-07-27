/*
    Generic LL(1) Recognizer/Generator.

    grammar = rule { rule } "." ;
    rule = ID [ "*" ] "=" expr ";" ;
    expr = term { "|" term } ;
    term = factor { factor } ;
    factor =  ID | "#" ID | STR | "(" expr ")" | "{" expr "}" | "[" expr "]" ;
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include "util.h"
#include "lex.h"

#define TRUE            1
#define FALSE           0
#define MAX_TOKSTR      512
#define HASH_SIZE       1009
#define HASH(s)         (hash(s)%HASH_SIZE)
#define MAX_RULES       256
#define EMPTY_SET       ((uint64_t)0)
#define SET_SIZE        63
#define EMPTY           (1ULL << 63) /* ε */
#define GRA_ERR         0
#define GRA_SYN_ERR     1
#define STR_ERR         2

typedef struct Node Node;
typedef struct NodeChain NodeChain;

typedef enum {
    TOK_DOT,
    TOK_SEMI,
    TOK_HASH,
    TOK_EQ,
    TOK_ID,
    TOK_NUM,
    TOK_STR,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_STAR,
    TOK_ALTER,      /* | */
    TOK_CONCAT,     /*   */
    TOK_REPET,      /* {} */
    TOK_OPTION,     /* [] */
} Token;

static char *prog_name;
static char *grammar_file, *string_file;
static char *buf, *curr, token_string[MAX_TOKSTR];
static Token gra_curr_tok;
static int line_number = 1;
static int verbose;
static int curr_tok;
static int ident_level;
static FILE *rec_file;

typedef enum {
    TermKind,
    NonTermKind,
    OpKind,
} NodeKind;

static struct Node {
    NodeKind kind;
    union {
        int tok_num;
        int rule_num;
        struct {
            Token tok;
            Node *child[2];
        } op;
    } attr;
    uint64_t first, follow;
} *rules[MAX_RULES];

static int rule_counter, nundef;
static char *rule_names[MAX_RULES];
static int start_symbol = -1;
static uint64_t follows[MAX_RULES];
static int follow_changed;
static int have_follow;
static uint64_t grammar_tokens;

static struct NodeChain {
    int num;
    char *name;
    Node *rule;
    NodeChain *next;
} *rule_table[HASH_SIZE];

static const char *strset(uint64_t s)
{
    int i, com;
    static char buf[1024];

    com = FALSE;
    buf[0] = '\0';
    for (i = 0; i < SET_SIZE; i++) {
        if (s & (1ULL<<i)) {
            if (com)
                strcat(buf, ", ");
            strcat(buf, lex_num2print(i));
            com = TRUE;
        }
    }
    return buf;
}

static void err(int level, char *fmt, ...)
{
    va_list args;

    switch (level) {
    case GRA_SYN_ERR:
        fprintf(stderr, "%s: %s:%d: error: ", prog_name, grammar_file, line_number);
        break;
    case STR_ERR:
        fprintf(stderr, "%s: %s:%d: error: ", prog_name, string_file, lex_lineno());
        break;
    case GRA_ERR:
    default:
        fprintf(stderr, "%s: %s: ", prog_name, grammar_file);
        break;
    }
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static Token get_token(void)
{
    enum {
        START, INCOMMENT, INID, INNUM, INSTR, DONE,
    };
    Token tok;
    int state;
    int save, cindx;
    static int eof_reached = FALSE;

    if (eof_reached)
        return -1;

    cindx = 0;
    state = START;
    while (state != DONE) {
        int c;

        c = *curr++;
        save = TRUE;
        switch (state) {
        case START:
            if (c==' ' || c=='\t' || c=='\n') {
                save = FALSE;
                if (c == '\n')
                    ++line_number;
            } if (isalpha(c) || c=='_') {
                state = INID;
            } else if (isdigit(c)) {
                state = INNUM;
            } else if (c == '"') {
                save = FALSE;
                state = INSTR;
            } else if (c == '!') {
                save = FALSE;
                state = INCOMMENT;
            } else {
                state = DONE;
                switch (c) {
                case '\0':
                    tok = -1;
                    save = FALSE;
                    eof_reached = TRUE;
                    break;
                case '#': tok = TOK_HASH;   break;
                case '.': tok = TOK_DOT;    break;
                case ';': tok = TOK_SEMI;   break;
                case '(': tok = TOK_LPAREN; break;
                case ')': tok = TOK_RPAREN; break;
                case '{': tok = TOK_LBRACE; break;
                case '}': tok = TOK_RBRACE; break;
                case '[': tok = TOK_LBRACKET; break;
                case ']': tok = TOK_RBRACKET; break;
                case '|': tok = TOK_ALTER;  break;
                case '=': tok = TOK_EQ;     break;
                case '*': tok = TOK_STAR;   break;
                default:
                    save = FALSE;
                    state = START;
                    break;
                }
            }
            break; /* START */

        case INCOMMENT:
            save = FALSE;
            if (c=='\n' || c=='\0') {
                --curr;
                state = START;
            }
            break;

#define FINISH(T)\
    do {\
        save = FALSE;\
        --curr;\
        tok = T;\
        state = DONE;\
    } while (0)

        case INID:
            if (!isalnum(c) && c!='_')
                FINISH(TOK_ID);
            break;

        case INNUM:
            if (!isdigit(c))
                FINISH(TOK_NUM);
            break;

        case INSTR:
            if (c == '"') {
                FINISH(TOK_STR);
                ++curr;
            }
            break;

#undef FINISH

        case DONE:
        default:
            assert(0);
            break;
        } /* switch (state) */
        if (save)
            token_string[cindx++] = (char)c;
        if (state == DONE)
            token_string[cindx] = '\0';
    }
    return tok;
}

static void match(Token expected)
{
    if (gra_curr_tok != expected) {
        if (isprint(token_string[0]))
            err(GRA_SYN_ERR, "unexpected `%s'", token_string);
        else
            err(GRA_SYN_ERR, "unexpected character byte `0x%02x'", (unsigned char)token_string[0]);
    }
    gra_curr_tok = get_token();
}

static int lookup_rule(char *name, Node *rule)
{
    unsigned h;
    NodeChain *np;

    h = HASH(name);
    for (np = rule_table[h]; np != NULL; np = np->next)
        if (strcmp(name, np->name) == 0)
            break;
    if (np == NULL) {
        np = malloc(sizeof(NodeChain));
        np->num = rule_counter;
        np->name = strdup(name);
        if ((np->rule=rule) == NULL)
            ++nundef;
        np->next = rule_table[h];
        rule_table[h] = np;
        rule_names[rule_counter] = np->name;
        rules[rule_counter++] = rule;
    } else if (np->rule == NULL) {
        if (rule != NULL) {
            rules[np->num] = np->rule = rule;
            --nundef;
        }
    } else if (rule != NULL) {
        err(GRA_ERR, "rule `%s' redefined", name);
    }
    return np->num;
}

static Node *new_node(NodeKind kind)
{
    Node *n;

    n = calloc(1, sizeof(Node));
    n->kind = kind;
    return n;
}

static Node *expr(void);

/* factor =  ID | "#" ID | STR | "(" expr ")" | "{" expr "}" | "[" expr "]" */
static Node *factor(void)
{
    Node *n;

    switch (gra_curr_tok) {
    case TOK_ID:
        n = new_node(NonTermKind);
        n->attr.rule_num = lookup_rule(token_string, NULL);
        match(TOK_ID);
        break;
    case TOK_HASH:
        match(TOK_HASH);
        if (gra_curr_tok == TOK_ID) {
            n = new_node(TermKind);
            if ((n->attr.tok_num=lex_name2num(token_string)) == -1)
                err(GRA_SYN_ERR, "unknown token name `%s'", token_string);
        }
        match(TOK_ID);
        grammar_tokens |= 1ULL<<n->attr.tok_num;
        break;
    case TOK_STR:
        n = new_node(TermKind);
        if ((n->attr.tok_num=lex_str2num(token_string)) == -1)
            err(GRA_SYN_ERR, "unknown token spelling `%s'", token_string);
        match(TOK_STR);
        grammar_tokens |= 1ULL<<n->attr.tok_num;
        break;
    case TOK_LPAREN:
        match(TOK_LPAREN);
        n = expr();
        match(TOK_RPAREN);
        break;
    case TOK_LBRACE:
        match(TOK_LBRACE);
        n = new_node(OpKind);
        n->attr.op.tok = TOK_REPET;
        n->attr.op.child[0] = expr();
        match(TOK_RBRACE);
        break;
    case TOK_LBRACKET:
        match(TOK_LBRACKET);
        n = new_node(OpKind);
        n->attr.op.tok = TOK_OPTION;
        n->attr.op.child[0] = expr();
        match(TOK_RBRACKET);
        break;
    default:
        match(-1);
        break;
    }
    return n;
}

/* term = factor { factor } */
static Node *term(void)
{
    Node *n, *q;

    n = factor();
    while (gra_curr_tok==TOK_ID || gra_curr_tok==TOK_HASH
    || gra_curr_tok==TOK_STR || gra_curr_tok==TOK_LPAREN
    || gra_curr_tok==TOK_LBRACE || gra_curr_tok==TOK_LBRACKET) {
        q = new_node(OpKind);
        q->attr.op.tok = TOK_CONCAT;
        q->attr.op.child[0] = n;
        q->attr.op.child[1] = factor();
        n = q;
    }
    return n;
}

/* expr = term { "|" term } */
Node *expr(void)
{
    Node *n, *q;

    n = term();
    while (gra_curr_tok == TOK_ALTER) {
        q = new_node(OpKind);
        q->attr.op.tok = TOK_ALTER;
        match(TOK_ALTER);
        q->attr.op.child[0] = n;
        q->attr.op.child[1] = term();
        n = q;
    }
    return n;
}

/* rule = ID [ "*" ] "=" expr ";" */
static void rule(void)
{
    Node *n;
    int is_start;
    char id[MAX_TOKSTR];

    is_start = FALSE;
    strcpy(id, token_string);
    match(TOK_ID);
    if (gra_curr_tok == TOK_STAR) {
        match(TOK_STAR);
        is_start = TRUE;
    }
    match(TOK_EQ);
    n = expr();
    match(TOK_SEMI);
    if (is_start) {
        if (start_symbol != -1)
            err(GRA_ERR, "more than one start symbol");
        start_symbol = lookup_rule(id, n);
    } else {
        (void)lookup_rule(id, n);
    }
}

/* grammar = rule { rule } "." */
static void grammar(void)
{
    rule();
    while (gra_curr_tok != TOK_DOT)
        rule();
    match(TOK_DOT);
}

static uint64_t first(Node *n)
{
    if (n->first != EMPTY_SET)
        return n->first;
    switch (n->kind) {
    case TermKind:
        n->first = 1ULL<<n->attr.tok_num;
        break;
    case NonTermKind:
        n->first = first(rules[n->attr.rule_num]);
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            n->first = first(n->attr.op.child[0])|first(n->attr.op.child[1]);
            break;
        case TOK_CONCAT:     /*   */
            if ((n->first=first(n->attr.op.child[0])) & EMPTY) {
                n->first &= ~EMPTY;
                n->first |= first(n->attr.op.child[1]);
            }
            break;
        case TOK_REPET:      /* {} */
        case TOK_OPTION:     /* [] */
            n->first = first(n->attr.op.child[0])|EMPTY;
            break;
        }
    }
    return n->first;
}

static void compute_follow(Node *n, uint64_t in)
{
    switch (n->kind) {
    case TermKind:
        break;
    case NonTermKind:
        if ((in & ~follows[n->attr.rule_num]) != EMPTY_SET) {
            follow_changed = TRUE;
            follows[n->attr.rule_num] |= in;
        }
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            compute_follow(n->attr.op.child[0], in);
            compute_follow(n->attr.op.child[1], in);
            break;
        case TOK_CONCAT:     /*   */
            compute_follow(n->attr.op.child[0], first(n->attr.op.child[1]));
            compute_follow(n->attr.op.child[1], in);
            break;
        case TOK_REPET:      /* {} */
            compute_follow(n->attr.op.child[0], first(n)|in);
            break;
        case TOK_OPTION:     /* [] */
            compute_follow(n->attr.op.child[0], in);
            break;
        }
    }
    n->follow = in;
}

/* fixed-point computation of Follow sets */
static void compute_follow_sets(void)
{
    int i;

    if (have_follow)
        return;
    follows[start_symbol] |= 1ULL<<lex_name2num("EOF");
    follow_changed = TRUE;
    while (follow_changed) {
        follow_changed = FALSE;
        for (i = 0; i < rule_counter; i++)
            compute_follow(rules[i], follows[i]);
    }
    have_follow = TRUE;
}

/* check for First/First, First/Follow conflicts */
static void conflict(Node *n, int rule_num)
{
    uint64_t s;

    if (n->kind != OpKind)
        return;
    switch (n->attr.op.tok) {
    case TOK_ALTER:      /* | */
        s = first(n->attr.op.child[0])&first(n->attr.op.child[1]);
        s &= ~EMPTY;
        if (s != EMPTY_SET)
            err(GRA_ERR, "Rule `%s': First/First conflict: { %s }", rule_names[rule_num], strset(s));
    case TOK_CONCAT:     /*   */
        conflict(n->attr.op.child[0], rule_num);
        conflict(n->attr.op.child[1], rule_num);
        break;
    case TOK_REPET:      /* {} */
    case TOK_OPTION:     /* [] */
        s = first(n)&n->follow;
        s &= ~EMPTY;
        if (s != EMPTY_SET)
            err(GRA_ERR, "Rule `%s': First/Follow conflict: { %s }", rule_names[rule_num], strset(s));
        conflict(n->attr.op.child[0], rule_num);
        break;
    }
}

static void check_for_left_rec(uint64_t rule_msk, Node *n)
{
    switch (n->kind) {
    case TermKind:
        break;
    case NonTermKind:
        if (rule_msk & (1ULL<<n->attr.rule_num))
            err(GRA_ERR, "rule `%s' contains left-recursion", rule_names[n->attr.rule_num]);
        check_for_left_rec(rule_msk|(1ULL<<n->attr.rule_num), rules[n->attr.rule_num]);
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            check_for_left_rec(rule_msk, n->attr.op.child[0]);
            check_for_left_rec(rule_msk, n->attr.op.child[1]);
            break;
        case TOK_CONCAT:     /*   */
            check_for_left_rec(rule_msk, n->attr.op.child[0]);
            if (first(n->attr.op.child[0]) & EMPTY)
                check_for_left_rec(rule_msk, n->attr.op.child[1]);
            break;
        case TOK_REPET:      /* {} */
        case TOK_OPTION:     /* [] */
            check_for_left_rec(rule_msk, n->attr.op.child[0]);
            break;
        }
        break;
    }
}

/* check for LL(1) conflicts */
static void conflicts(void)
{
    int i;

    check_for_left_rec(1<<start_symbol, rules[start_symbol]);
    compute_follow_sets();
    for (i = 0; i < rule_counter; i++)
        conflict(rules[i], i);
}

static void recognize(Node *n)
{
    switch (n->kind) {
    case TermKind:
        if (curr_tok != n->attr.tok_num)
            err(STR_ERR, "unexpected `%s'", lex_num2print(curr_tok));
        if (verbose) {
            int i;

            for (i = ident_level; i; i--)
                printf("--");
            printf("<< matched `%s' (%s:%d)\n", lex_num2print(curr_tok), string_file, lex_lineno());
        }
        curr_tok = lex_get_token();
        break;
    case NonTermKind:
        if (verbose) {
            int i;

            for (i = ident_level; i; i--)
                printf("--");
            printf(">> replacing `%s' (%s:%d)\n", rule_names[n->attr.rule_num], string_file, lex_lineno());
        }
        ++ident_level;
        recognize(rules[n->attr.rule_num]);
        --ident_level;
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            if (first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                recognize(n->attr.op.child[0]);
            else
                recognize(n->attr.op.child[1]);
            break;
        case TOK_CONCAT:     /*   */
            recognize(n->attr.op.child[0]);
            recognize(n->attr.op.child[1]);
            break;
        case TOK_REPET:      /* {} */
            while (first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                recognize(n->attr.op.child[0]);
            break;
        case TOK_OPTION:     /* [] */
            if (first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                recognize(n->attr.op.child[0]);
            break;
        }
        break;
    }
}

static void print_first_sets(void)
{
    int i;
    uint64_t s;

    for (i = 0; i < rule_counter; i++) {
        s = first(rules[i]);
        printf("FIRST(%s) = { %s%s }\n", rule_names[i], strset(s),
        (s&EMPTY)?", epsilon":"");
    }
}

static void print_follow_sets(void)
{
    int i;

    compute_follow_sets();
    for (i = 0; i < rule_counter; i++)
        printf("FOLLOW(%s) = { %s }\n", rule_names[i], strset(follows[i]));
}

/* ============================================================ */
/* Source code emitters */
/* ============================================================ */

static void emit(int ident, int new_line, char *fmt, ...)
{
    va_list args;

    while (ident--)
        fprintf(rec_file, "    ");
    va_start(args, fmt);
    vfprintf(rec_file, fmt, args);
    va_end(args);
    if (new_line)
        fprintf(rec_file, "\n");
}

#define EMIT(ident, ...)   emit(ident, 0, __VA_ARGS__)
#define EMITLN(ident, ...) emit(ident, 1, __VA_ARGS__)

static void write_first_test(uint64_t s)
{
    int i, start;

    start = TRUE;
    for (i = 0; i < SET_SIZE; i++) {
        if (s & (1ULL<<i)) {
            if (!start)
                fprintf(rec_file, " || ");
            fprintf(rec_file, "LA(T_%s)", lex_num2name(i));
            start = FALSE;
        }
    }
}

static void write_rule(Node *n, int in_alter, int in_else, int ident)
{
    switch (n->kind) {
    case TermKind:
        if (in_alter) {
            if (in_else)
                fprintf(rec_file, "if (LA(T_%s)) {\n", lex_num2name(n->attr.tok_num));
            else
                EMITLN(ident, "if (LA(T_%s)) {", lex_num2name(n->attr.tok_num));
            EMITLN(ident+1, "match(T_%s);", lex_num2name(n->attr.tok_num));
            EMIT(ident, "}");
        } else {
            EMIT(ident, "match(T_%s);", lex_num2name(n->attr.tok_num));
        }
        break;
    case NonTermKind:
        if (in_alter) {
            if (in_else)
                fprintf(rec_file, "if (");
            else
                EMIT(ident, "if (");
            write_first_test(first(rules[n->attr.rule_num]));
            fprintf(rec_file, ") {\n");
            EMITLN(ident+1, "%s();", rule_names[n->attr.rule_num]);
            EMIT(ident, "}");
        } else {
            EMIT(ident, "%s();", rule_names[n->attr.rule_num]);
        }
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            write_rule(n->attr.op.child[0], TRUE, FALSE, ident);
            fprintf(rec_file, " else ");
            write_rule(n->attr.op.child[1], TRUE, TRUE, ident);
            if (!in_alter) {
                fprintf(rec_file, " else {\n");
                EMITLN(ident+1, "error();");
                EMIT(ident, "}");
            }
            break;
        case TOK_CONCAT:     /*   */
            if (in_alter) {
                if (in_else)
                    fprintf(rec_file, "if (");
                else
                    EMIT(ident, "if (");
                write_first_test(first(n));
                fprintf(rec_file, ") {\n");
                write_rule(n->attr.op.child[0], FALSE, FALSE, ident+1); fprintf(rec_file, "\n");
                write_rule(n->attr.op.child[1], FALSE, FALSE, ident+1); fprintf(rec_file, "\n");
                EMIT(ident, "}");
            } else {
                write_rule(n->attr.op.child[0], FALSE, FALSE, ident); fprintf(rec_file, "\n");
                write_rule(n->attr.op.child[1], FALSE, FALSE, ident);
            }
            break;
        case TOK_REPET:      /* {} */
            if (in_alter) {
                if (in_else)
                    fprintf(rec_file, "if (");
                else
                    EMIT(ident, "if (");
                write_first_test(first(n->attr.op.child[0]));
                fprintf(rec_file, ") {\n");
                EMIT(ident+1, "while (");
                write_first_test(first(n->attr.op.child[0]));
                fprintf(rec_file, ") {\n");
                write_rule(n->attr.op.child[0], FALSE, FALSE, ident+2); fprintf(rec_file, "\n");
                EMITLN(ident+1, "}");
                EMIT(ident, "}");
            } else {
                EMIT(ident, "while (");
                write_first_test(first(n->attr.op.child[0]));
                fprintf(rec_file, ") {\n");
                write_rule(n->attr.op.child[0], FALSE, FALSE, ident+1); fprintf(rec_file, "\n");
                EMIT(ident, "}");
            }
            break;
        case TOK_OPTION:     /* [] */
            if (in_else)
                fprintf(rec_file, "if (");
            else
                EMIT(ident, "if (");
            write_first_test(first(n->attr.op.child[0]));
            fprintf(rec_file, ") {\n");
            write_rule(n->attr.op.child[0], FALSE, FALSE, ident+1); fprintf(rec_file, "\n");
            EMIT(ident, "}");
            break;
        }
        break;
    }
}

static void generate_recognizer(void)
{
    int i;
    const char *kw;

    fprintf(rec_file,
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include \"lex.h\"\n");

    for (i = 0; i < SET_SIZE; i++)
        if (grammar_tokens & (1ULL<<i))
            fprintf(rec_file, "#define T_%s %d\n", lex_num2name(i), i);

    fprintf(rec_file,
    "static int curr_tok;\n"
    "static char *prog_name, *string_file;\n"
    "#define LA(x) (curr_tok == (x))\n"
    "static void error(void)\n"
    "{\n"
    "    fprintf(stderr, \"%%s: %%s:%%d: error: unexpected `%%s'\\n\", prog_name,\n"
    "    string_file, lex_lineno(), lex_num2print(curr_tok));\n"
    "    exit(EXIT_FAILURE);\n"
    "}\n"
    "static void match(int expected)\n"
    "{\n"
    "    if (curr_tok == expected)\n"
    "        curr_tok = lex_get_token();\n"
    "    else\n"
    "        error();\n"
    "}\n"
    );

    for (i = 0; i < rule_counter; i++)
        EMITLN(0, "static void %s(void);", rule_names[i]);

    for (i = 0; i < rule_counter; i++) {
        EMITLN(0, "void %s(void) {", rule_names[i]);
        write_rule(rules[i], FALSE, FALSE, 1);
        EMITLN(0, "\n}");
    }

    fprintf(rec_file,
    "int main(int argc, char *argv[])\n"
    "{\n"
    "    prog_name = argv[0];\n"
    "    string_file = argv[1];\n"
    "    lex_init(string_file);\n");

    for (kw = lex_keyword_iterate(TRUE); kw != NULL; kw = lex_keyword_iterate(FALSE))
        fprintf(rec_file, "    lex_keyword(\"%s\");\n", kw);

    fprintf(rec_file,
    "    curr_tok = lex_get_token();\n"
    "    %s();\n"
    "    lex_finish();\n"
    "    return 0;\n"
    "}\n",
    rule_names[start_symbol]);
}
/* ============================================================ */

static void usage(int ext)
{
    fprintf(stderr, "usage: %s [ options ] <grammar_file> [ <string_file> ]\n", prog_name);
    if (ext)
        exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    int i;
    int print_first, print_follow, validate, generate;
    char *outfile;

    prog_name = argv[0];
    outfile = NULL;
    validate = print_first = print_follow = generate = FALSE;
    if (argc == 1)
        usage(TRUE);
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            if (grammar_file == NULL)
                grammar_file = argv[i];
            else
                string_file = argv[i];
            continue;
        }
        switch (argv[i][1]) {
        case 'o':
            if (argv[i][2] != '\0') {
                outfile = argv[i]+2;
            } else if (argv[i+1] != NULL) {
                outfile = argv[++i];
            } else {
                fprintf(stderr, "%s: missing argument for -o option\n", prog_name);
                exit(EXIT_FAILURE);
            }
            break;
        case 'f':
            print_first = TRUE;
            break;
        case 'l':
            print_follow = TRUE;
            break;
        case 'c':
            validate = TRUE;
            break;
        case 'g':
            generate = TRUE;
            break;
        case 'v':
            verbose = TRUE;
            break;
        case 'h':
            usage(FALSE);
            printf("\noptions:\n"
                   "  -o<file>: write recognizer (-g) to <file> (default stdout)\n"
                   "  -f: print first sets\n"
                   "  -l: print follow sets\n"
                   "  -c: check the grammar for LL(1) conflicts\n"
                   "  -g: generate a recognizer in C\n"
                   "  -v: verbose mode\n"
                   "  -h: print this help\n");
            exit(EXIT_SUCCESS);
        default:
            fprintf(stderr, "%s: unknown option `%s'\n", prog_name, argv[i]);
            exit(EXIT_FAILURE);
        }
    }
    if (grammar_file==NULL || (!generate && string_file==NULL))
        usage(TRUE);

    if ((buf=read_file(grammar_file)) == NULL) {
        fprintf(stderr, "%s: cannot read file `%s'\n", prog_name, grammar_file);
        exit(EXIT_FAILURE);
    }
    curr = buf;
    gra_curr_tok = get_token();
    grammar();
    if (start_symbol == -1)
        err(GRA_ERR, "start symbol not defined");
    if (nundef != 0) {
        char buf[256];

        buf[0] = '\0';
        for (i = 0; i < rule_counter; i++) {
            if (rules[i] == NULL) {
                if (buf[0] != '\0')
                    strcat(buf, ", ");
                strcat(buf, "`");
                strcat(buf, rule_names[i]);
                strcat(buf, "'");
            }
        }
        err(GRA_ERR, "the grammar contains the following undefined symbols: %s", buf);
    }
    if (validate) {
        if (rule_counter <= 64)
            conflicts();
        else
            err(GRA_ERR, "cannot validate the grammar (more than 64 rules)");
    }
    if (print_first)
        print_first_sets();
    if (print_follow)
        print_follow_sets();
    if (generate) {
        rec_file = (outfile!=NULL)?fopen(outfile, "wb"):stdout;
        generate_recognizer();
        if (outfile != NULL)
            fclose(rec_file);
    }
    if (string_file != NULL) {
        if (lex_init(string_file) == -1) {
            fprintf(stderr, "%s: lex_init() failed!\n", prog_name);
            exit(EXIT_FAILURE);
        }
        curr_tok = lex_get_token();
        if (verbose) {
            printf(">> replacing `%s' (%s:%d)\n", rule_names[start_symbol], string_file, lex_lineno());
            ++ident_level;
        }
        recognize(rules[start_symbol]);
        if (lex_finish() == -1)
            ;
    }
    return 0;
}
