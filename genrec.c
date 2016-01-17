/*
    Generic LL(1) Recognizer.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include "lex.h"

#define TRUE            1
#define FALSE           0
#define MAX_LEXEME      512
#define HASH_SIZE       1009
#define HASH(s)         (hash(s)%HASH_SIZE)
#define MAX_RULES       256

typedef struct Node Node;
typedef struct NodeChain NodeChain;

typedef enum {
    TOK_DOT,
    TOK_SEMI,
    TOK_HASH,
    TOK_EQ,
    TOK_ID,
    TOK_NUM,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_STAR,
    TOK_ALTER,      /* | */
    TOK_CONCAT,     /* , */
    /* --- */
    TOK_REPET,      /* {} */
    TOK_OPTION,     /* [] */
    TOK_TOKEN,      /* #NUM */
    TOK_DUMMY,
} Token;

static char *prog_name;
static char *grammar_file, *string_file;
static char *buf, *curr, lexeme[MAX_LEXEME];
static Token genrec_curr_tok;
static int line_number = 1;
static int verbose;
static int curr_tok;
static int ident_level;

static char *read_file(char *path);
static void err(int level, char *fmt, ...);
static Token get_token(void);
static void match(Token expected);
static unsigned hash(char *s);
static int str2int(char *s);

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
} *rules[MAX_RULES];

static int rule_counter, nundef;
static char *rule_names[MAX_RULES];
static int start_symbol = -1;

static struct NodeChain {
    int num;
    char *name;
    Node *rule;
    NodeChain *next;
} *rule_table[HASH_SIZE];

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
        err(0, "rule `%s' redefined", name);
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

static Node *nonterm(void);

/* term = "#" NUM */
static Node *term(void)
{
    int num;
    Node *n;

    match(TOK_HASH);
    if (genrec_curr_tok == TOK_NUM)
        num = str2int(lexeme);
    match(TOK_NUM);
    n = new_node(TermKind);
    n->attr.tok_num = num;
    return n;
}

/* concat = ID | "(" nonterm ")" | "{" nonterm "}" | "[" nonterm "]" */
static Node *concat(void)
{
    Node *n;

    switch (genrec_curr_tok) {
    case TOK_ID:
        n = new_node(NonTermKind);
        n->attr.rule_num = lookup_rule(lexeme, NULL);
        match(TOK_ID);
        break;
    case TOK_LPAREN:
        match(TOK_LPAREN);
        n = nonterm();
        match(TOK_RPAREN);
        break;
    case TOK_LBRACE:
        match(TOK_LBRACE);
        n = new_node(OpKind);
        n->attr.op.tok = TOK_REPET;
        n->attr.op.child[0] = nonterm();
        match(TOK_RBRACE);
        break;
    case TOK_LBRACKET:
        match(TOK_LBRACKET);
        n = new_node(OpKind);
        n->attr.op.tok = TOK_OPTION;
        n->attr.op.child[0] = nonterm();
        match(TOK_RBRACKET);
        break;
    default:
        match(-1);
        break;
    }
    return n;
}

/* option =  concat { "," concat } */
static Node *option(void)
{
    Node *n, *q;

    n = concat();
    while (genrec_curr_tok == TOK_CONCAT) {
        q = new_node(OpKind);
        q->attr.op.tok = TOK_CONCAT;
        match(TOK_CONCAT);
        q->attr.op.child[0] = n;
        q->attr.op.child[1] = concat();
        n = q;
    }
    return n;
}

/* nonterm = option { "|" option } */
Node *nonterm(void)
{
    Node *n, *q;

    n = option();
    while (genrec_curr_tok == TOK_ALTER) {
        q = new_node(OpKind);
        q->attr.op.tok = TOK_ALTER;
        match(TOK_ALTER);
        q->attr.op.child[0] = n;
        q->attr.op.child[1] = option();
        n = q;
    }
    return n;
}

/* rule = ID [ "*" ] "=" ( nonterm | term ) ";" */
static void rule(void)
{
    Node *n;
    int is_start;
    char id[MAX_LEXEME];

    is_start = FALSE;
    strcpy(id, lexeme);
    match(TOK_ID);
    if (genrec_curr_tok == TOK_STAR) {
        match(TOK_STAR);
        is_start = TRUE;
    }
    match(TOK_EQ);
    if (genrec_curr_tok == TOK_HASH)
        n = term();
    else
        n = nonterm();
    match(TOK_SEMI);
    if (is_start) {
        if (start_symbol != -1)
            err(0, "more than one start symbol");
        start_symbol = lookup_rule(id, n);
    } else {
        (void)lookup_rule(id, n);
    }
}

/* grammar = rule { rule } "." */
static void grammar(void)
{
    rule();
    while (genrec_curr_tok != TOK_DOT)
        rule();
    match(TOK_DOT);
}

/* compute the FIRST(n) set */
static uint64_t get_first(Node *n)
{
    /* TODO: cache results */
    switch (n->kind) {
    case TermKind:
        return (1ULL << n->attr.tok_num);
    case NonTermKind:
        return get_first(rules[n->attr.rule_num]);
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            return get_first(n->attr.op.child[0])|get_first(n->attr.op.child[1]);
        case TOK_CONCAT:     /* , */
        case TOK_REPET:      /* {} */
        case TOK_OPTION:     /* [] */
            return get_first(n->attr.op.child[0]);
        }
    }
    assert(0);
}

static void recognize(Node *n)
{
    switch (n->kind) {
    case TermKind:
        if (curr_tok != n->attr.tok_num)
            err(2, "expecting #%d, but got #%d", n->attr.tok_num, curr_tok);
        if (verbose) {
            int i;

            for (i = ident_level; i; i--)
                printf("--");
            printf("<< matched #%d (%s:%d)\n", curr_tok, string_file, lex_lineno);
        }
        curr_tok = lex();
        break;
    case NonTermKind:
        if (verbose) {
            int i;

            for (i = ident_level; i; i--)
                printf("--");
            printf(">> replacing `%s' (%s:%d)\n", rule_names[n->attr.rule_num], string_file, lex_lineno);
        }
        ++ident_level;
        recognize(rules[n->attr.rule_num]);
        --ident_level;
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            if (get_first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                recognize(n->attr.op.child[0]);
            else
                recognize(n->attr.op.child[1]);
            break;
        case TOK_CONCAT:     /* , */
            recognize(n->attr.op.child[0]);
            recognize(n->attr.op.child[1]);
            break;
        case TOK_REPET:      /* {} */
            while (get_first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                recognize(n->attr.op.child[0]);
            break;
        case TOK_OPTION:     /* [] */
            if (get_first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                recognize(n->attr.op.child[0]);
            break;
        }
        break;
    }
}

/* check for left-recursion */
static void validate_grammar(uint64_t rule_msk, Node *n)
{
    switch (n->kind) {
    case TermKind:
        break;
    case NonTermKind:
        if (rule_msk & (1ULL<<n->attr.rule_num))
            err(0, "the grammar contains left-recursion");
        validate_grammar(rule_msk|(1ULL<<n->attr.rule_num), rules[n->attr.rule_num]);
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            validate_grammar(rule_msk, n->attr.op.child[0]);
            validate_grammar(rule_msk, n->attr.op.child[1]);
            break;
        case TOK_CONCAT:     /* , */
        case TOK_REPET:      /* {} */
        case TOK_OPTION:     /* [] */
            validate_grammar(rule_msk, n->attr.op.child[0]);
            break;
        }
        break;
    }
}

static void print_first_sets(void)
{
    int i;

    for (i = 0; i < rule_counter; i++) {
        int j, com;
        uint64_t first;

        printf("FIRST(%s) = {", rule_names[i]);
        first = get_first(rules[i]);
        com = FALSE;
        for (j = 0; j < 64; j++) {
            if (first & (1ULL<<j)) {
                printf("%s #%d", com?",":"", j);
                com = TRUE;
            }
        }
        printf(" }\n");
    }
}

static void usage(int ext)
{
    fprintf(stderr, "usage: %s [ options ] <grammar_file> <string_file>\n", prog_name);
    if (ext)
        exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    int i;
    int print_first, validate;

    prog_name = argv[0];
    validate = print_first = FALSE;
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
        case 'f':
            print_first = TRUE;
            break;
        case 'c':
            validate = TRUE;
            break;
        case 'v':
            verbose = TRUE;
            break;
        case 'h':
            usage(FALSE);
            printf("\noptions:\n"
                   "  -f: print first sets\n"
                   "  -c: check the grammar for left recursion\n"
                   "  -v: verbose mode\n"
                   "  -h: print this help\n");
            exit(EXIT_SUCCESS);
        default:
            fprintf(stderr, "%s: unknown option `%s'\n", prog_name, argv[i]);
            exit(EXIT_FAILURE);
        }
    }
    if (grammar_file==NULL || string_file==NULL)
        usage(TRUE);

    if ((buf=read_file(grammar_file)) == NULL) {
        fprintf(stderr, "%s: cannot read file `%s'\n", prog_name, grammar_file);
        exit(EXIT_FAILURE);
    }
    curr = buf;
    genrec_curr_tok = get_token();
    grammar();
    if (start_symbol == -1)
        err(0, "start symbol not defined");
    if (nundef != 0)
        err(0, "the grammar contains undefined symbols");
    if (validate) {
        if (rule_counter <= 64)
            validate_grammar(1<<start_symbol, rules[start_symbol]);
        else
            err(0, "cannot validate the grammar (more than 64 rules)");
    }
    if (print_first)
        print_first_sets();
    if (lex_init(string_file) == -1) {
        fprintf(stderr, "%s: lex_init() failed!\n", prog_name);
        exit(EXIT_FAILURE);
    }
    curr_tok = lex();
    if (verbose) {
        printf(">> replacing `%s' << (%s:%d)\n", rule_names[start_symbol], string_file, lex_lineno);
        ++ident_level;
    }
    recognize(rules[start_symbol]);
    if (lex_finish() == -1)
        ;

    return 0;
}

Token get_token(void)
{
    enum {
        START,
        INCOMMENT,
        INID,
        INNUM,
        DONE,
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
                case '#':
                    tok = TOK_HASH;
                    break;
                case ',':
                    tok = TOK_CONCAT;
                    break;
                case '.':
                    tok = TOK_DOT;
                    break;
                case ';':
                    tok = TOK_SEMI;
                    break;
                case '(':
                    tok = TOK_LPAREN;
                    break;
                case ')':
                    tok = TOK_RPAREN;
                    break;
                case '{':
                    tok = TOK_LBRACE;
                    break;
                case '}':
                    tok = TOK_RBRACE;
                    break;
                case '[':
                    tok = TOK_LBRACKET;
                    break;
                case ']':
                    tok = TOK_RBRACKET;
                    break;
                case '|':
                    tok = TOK_ALTER;
                    break;
                case '=':
                    tok = TOK_EQ;
                    break;
                case '*':
                    tok = TOK_STAR;
                    break;
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

#undef FINISH

        case DONE:
        default:
            assert(0);
            break;
        } /* switch (state) */
        if (save)
            lexeme[cindx++] = (char)c;
        if (state == DONE)
            lexeme[cindx] = '\0';
    }
    return tok;
}

void match(Token expected)
{
    if (genrec_curr_tok != expected) {
        if (isprint(lexeme[0]))
            err(1, "unexpected `%s'", lexeme);
        else
            err(1, "unexpected character byte `0x%02x'", lexeme[0]);
    }
    genrec_curr_tok = get_token();
}

void err(int level, char *fmt, ...)
{
    va_list args;

    switch (level) {
    case 1:
        fprintf(stderr, "%s: %s:%d: error: ", prog_name, grammar_file, line_number);
        break;
    case 2:
        fprintf(stderr, "%s: %s:%d: error: ", prog_name, string_file, lex_lineno);
        break;
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

unsigned hash(char *s)
{
    unsigned hash_val;

    for (hash_val = 0; *s != '\0'; s++)
        hash_val = (unsigned)*s + 31*hash_val;
    return hash_val;
}

char *read_file(char *path)
{
    FILE *fp;
    char *buf;
    unsigned len;

    if ((fp=fopen(path, "rb")) == NULL)
        return NULL;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);
    buf = malloc(len+1);
    len = fread(buf, 1, len, fp);
    buf[len] = '\0';
    fclose(fp);
    return buf;
}

int str2int(char *s)
{
    char *ep;

    return strtol(s, &ep, 10);
}
