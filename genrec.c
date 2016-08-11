/*
    Generic LL(1) Recognizer/Generator.

    grammar = rule { rule } "." ;
    rule = ID [ "*" ] "=" expr ";" ;
    expr = term { "|" term } ;
    term = factor { factor } ;
    factor = ID
           | "#" ID
           | STR
           | "(" expr ")"
           | "{" expr "}"
           | "[" expr "]"
           | "<" "#" ID ":" ID ">"
           | "[[" expr "]]"
           | output ;
    output = "{{" outexpr { outexpr } "}}" ;
    outexpr = STR | "*" [ NUM ] | ID | ";" | "+" | "-" ;
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

#define HASH_SIZE       1009
#define HASH(s)         (hash(s)%HASH_SIZE)
#define MAX_RULES       256
#define EMPTY_SET       ((uint64_t)0)
#define SET_SIZE        63
#define EMPTY           (1ULL << 63) /* ε */
#define GRA_ERR         0
#define GRA_SYN_ERR     1
#define STR_ERR         2
#define MAX_NAM_TOK     64

typedef struct Node Node;
typedef struct NodeChain NodeChain;
typedef struct OutList OutList;
typedef struct FixUp FixUp;
typedef struct State State;

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
    TOK_LBRACKET2,
    TOK_RBRACKET2,
    TOK_LBRACE2,
    TOK_RBRACE2,
    TOK_LANGLE,
    TOK_RANGLE,
    TOK_COLON,
    TOK_STAR,
    TOK_PLUS,
    TOK_MINUS,
    TOK_ALTER,      /* | */
    TOK_ALTER_BT,   /* [[ | ]] */
    TOK_CONCAT,     /*   */
    TOK_REPET,      /* {} */
    TOK_OPTION,     /* [] */
} Token;

static char *prog_name;
static char *grammar_file_path, *string_file_path;
static char *grammar_buf, *curr_ch, token_string[MAX_TOKSTR_LEN];
static Token grammar_curr_tok;
#define LA grammar_curr_tok
static int line_number = 1;
static int verbose;
static int curr_tok;
static char last_tokstr_buf[MAX_TOKSTR_LEN];
static int out_indent;
static int ver_indent;
static FILE *rec_file;
static StrBuf *outbuf;

static int label_counter = 1;
#define USES_LAB1 0x01
#define USES_LAB2 0x02
static char label_usage[MAX_RULES];
static int uses_lab1, uses_lab2;

enum {
    O_LAST, O_LAB1, O_LAB2, O_END, O_INC, O_DEC, O_VER, O_TN,
};

struct OutList {
    int kind;
    char *val;
    OutList *next;
};

static struct FixUp {
    char **p;
    FixUp *next;
} *fixup_list;

typedef enum {
    TermKind,
    NonTermKind,
    OpKind,
    OutKind,
} NodeKind;

static struct Node {
    NodeKind kind;
    union {
        struct {
            int tok_num;
            int slot;
        } t;
        int rule_num;
        struct {
            Token tok;
            Node *child[2];
        } op;
        OutList *out_list;
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
static struct {
    char *name;
    char *tokstr;
} named_tokens[MAX_NAM_TOK];
static int named_tokens_counter;

struct State {
    void *lex;
    int curr_tok;
    int out_pos;
    int out_indent;
    int ver_indent;
    char last[MAX_TOKSTR_LEN];
    int label_counter;
    /*char (*named_tokens)[MAX_TOKSTR_LEN];*/
};

static void save_state(State *st)
{
    st->lex = lex_get_state();
    st->curr_tok = curr_tok;
    st->out_pos = strbuf_get_pos(outbuf);
    st->out_indent = out_indent;
    st->ver_indent = ver_indent;
    strcpy(st->last, last_tokstr_buf);
    st->label_counter = label_counter;
}

static void restore_state(State *st)
{
    lex_set_state(st->lex);
    curr_tok = st->curr_tok;
    strbuf_set_pos(outbuf, st->out_pos);
    out_indent = st->out_indent;
    ver_indent = st->ver_indent;
    strcpy(last_tokstr_buf, st->last);
    label_counter = st->label_counter;
}

static void dispose_state(State *st)
{
    free(st->lex);
}

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

static void err(int fatal, int level, char *fmt, ...)
{
    va_list args;

    switch (level) {
    case GRA_SYN_ERR:
        fprintf(stderr, "%s: %s:%d: error: ", prog_name, grammar_file_path, line_number);
        break;
    case STR_ERR:
        fprintf(stderr, "%s: %s:%d: error: ", prog_name, string_file_path, lex_lineno());
        break;
    case GRA_ERR:
    default:
        fprintf(stderr, "%s: %s: ", prog_name, grammar_file_path);
        break;
    }
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    if (fatal)
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
    int str_line;
    static int eof_reached = FALSE;

    if (eof_reached)
        return -1;

    cindx = 0;
    state = START;
    while (state != DONE) {
        int c;

        c = *curr_ch++;
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
                str_line = line_number;
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
                case '{':
                    if (*curr_ch == '{') {
                        token_string[cindx++] = (char)c;
                        c = *curr_ch++;
                        tok = TOK_LBRACE2;
                    } else {
                        tok = TOK_LBRACE;
                    }
                    break;
                case '}':
                    if (*curr_ch == '}') {
                        token_string[cindx++] = (char)c;
                        c = *curr_ch++;
                        tok = TOK_RBRACE2;
                    } else {
                        tok = TOK_RBRACE;
                    }
                    break;
                case '[':
                    if (*curr_ch == '[') {
                        token_string[cindx++] = (char)c;
                        c = *curr_ch++;
                        tok = TOK_LBRACKET2;
                    } else {
                        tok = TOK_LBRACKET;
                    }
                    break;
                case ']':
                    if (*curr_ch == ']') {
                        token_string[cindx++] = (char)c;
                        c = *curr_ch++;
                        tok = TOK_RBRACKET2;
                    } else {
                        tok = TOK_RBRACKET;
                    }
                    break;
                case '(': tok = TOK_LPAREN; break;
                case ')': tok = TOK_RPAREN; break;
                case '#': tok = TOK_HASH;   break;
                case '.': tok = TOK_DOT;    break;
                case ';': tok = TOK_SEMI;   break;
                case '|': tok = TOK_ALTER;  break;
                case '=': tok = TOK_EQ;     break;
                case '*': tok = TOK_STAR;   break;
                case '+': tok = TOK_PLUS;   break;
                case '-': tok = TOK_MINUS;  break;
                case '<': tok = TOK_LANGLE; break;
                case '>': tok = TOK_RANGLE; break;
                case ':': tok = TOK_COLON;  break;
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
                --curr_ch;
                state = START;
            }
            break;

#define FINISH(T)\
    do {\
        save = FALSE;\
        --curr_ch;\
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
                if (curr_ch[-2] != '\\') {
                    FINISH(TOK_STR);
                    ++curr_ch;
                } else {
                    c = '\"';
                    --cindx;
                }
            } else if (c == '\n') {
                ++line_number;
            } else if (c == '\0') {
                line_number = str_line;
                err(1, GRA_SYN_ERR, "unterminated string");
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
    if (LA != expected) {
        if (isprint(token_string[0]))
            err(1, GRA_SYN_ERR, "unexpected `%s'", token_string);
        else
            err(1, GRA_SYN_ERR, "unexpected character byte `0x%02x'", (unsigned char)token_string[0]);
    }
    LA = get_token();
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
        err(1, GRA_ERR, "rule `%s' redefined", name);
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

static int new_named_token(char *name)
{
    int i;

    assert(named_tokens_counter < MAX_NAM_TOK);
    for (i = 0; i < named_tokens_counter; i++)
        if (strcmp(named_tokens[i].name, name) == 0)
            err(1, GRA_SYN_ERR, "named token `%s' redefined", name);
    named_tokens[i].name = strdup(name);
    named_tokens[i].tokstr = malloc(MAX_TOKSTR_LEN);
    named_tokens[i].tokstr[0] = '\0';
    return named_tokens_counter++;
}

static Node *expr(int bt);

/*
    factor = ID
           | "#" ID
           | STR
           | "(" expr ")"
           | "{" expr "}"
           | "[" expr "]"
           | "<" "#" ID ":" ID ">"
           | "[[" expr "]]"
           | output ;
    output = "{{" outexpr { outexpr } "}}" ;
    outexpr = STR | "*" [ NUM ] | ID | ";" | "+" | "-" ;
*/
static Node *factor(void)
{
    Node *n;

    switch (LA) {
    case TOK_ID:
        n = new_node(NonTermKind);
        n->attr.rule_num = lookup_rule(token_string, NULL);
        match(TOK_ID);
        break;
    case TOK_HASH:
        match(TOK_HASH);
        if (LA == TOK_ID) {
            n = new_node(TermKind);
            if ((n->attr.t.tok_num=lex_name2num(token_string)) == -1)
                err(1, GRA_SYN_ERR, "unknown token name `%s'", token_string);
            n->attr.t.slot = -1;
        }
        match(TOK_ID);
        grammar_tokens |= 1ULL<<n->attr.t.tok_num;
        break;
    case TOK_STR:
        n = new_node(TermKind);
        if ((n->attr.t.tok_num=lex_str2num(token_string)) == -1)
            err(1, GRA_SYN_ERR, "unknown token spelling `%s'", token_string);
        n->attr.t.slot = -1;
        match(TOK_STR);
        grammar_tokens |= 1ULL<<n->attr.t.tok_num;
        break;
    case TOK_LPAREN:
        match(TOK_LPAREN);
        n = expr(FALSE);
        match(TOK_RPAREN);
        break;
    case TOK_LBRACE:
        match(TOK_LBRACE);
        n = new_node(OpKind);
        n->attr.op.tok = TOK_REPET;
        n->attr.op.child[0] = expr(FALSE);
        match(TOK_RBRACE);
        break;
    case TOK_LBRACKET:
        match(TOK_LBRACKET);
        n = new_node(OpKind);
        n->attr.op.tok = TOK_OPTION;
        n->attr.op.child[0] = expr(FALSE);
        match(TOK_RBRACKET);
        break;
    case TOK_LANGLE:
        match(TOK_LANGLE);
        match(TOK_HASH);
        if (LA == TOK_ID) {
            n = new_node(TermKind);
            if ((n->attr.t.tok_num=lex_name2num(token_string)) == -1)
                err(1, GRA_SYN_ERR, "unknown token name `%s'", token_string);
        }
        match(TOK_ID);
        grammar_tokens |= 1ULL<<n->attr.t.tok_num;
        match(TOK_COLON);
        if (LA == TOK_ID)
            n->attr.t.slot = new_named_token(token_string);
        match(TOK_ID);
        match(TOK_RANGLE);
        break;
    case TOK_LBRACE2: {
        OutList h, *t;

        n = new_node(OutKind);
        match(TOK_LBRACE2);
        t = &h;
        goto first;
        while (LA==TOK_STR || LA==TOK_STAR || LA==TOK_SEMI
        || LA==TOK_PLUS || LA==TOK_MINUS || LA==TOK_ID) {
    first:  t->next = malloc(sizeof(*t));
            t = t->next;
            switch (LA) {
            case TOK_STR:
                t->kind = O_VER;
                t->val = strdup(token_string);
                match(TOK_STR);
                break;
            case TOK_STAR:
                match(TOK_STAR);
                if (LA == TOK_NUM) {
                    switch (atoi(token_string)) {
                    case 1:
                        t->kind = O_LAB1;
                        uses_lab1 = TRUE;
                        break;
                    case 2:
                        t->kind = O_LAB2;
                        uses_lab2 = TRUE;
                        break;
                    default:
                        err(1, GRA_SYN_ERR, "`1' or `2' expected after `*'");
                    }
                    match(TOK_NUM);
                } else {
                    t->kind = O_LAST;
                }
                break;
            case TOK_ID: {
                FixUp *fx;

                t->kind = O_TN;
                t->val = strdup(token_string);
                match(TOK_ID);
                fx = malloc(sizeof(*fx));
                fx->p = &t->val;
                fx->next = fixup_list;
                fixup_list = fx;
            }
                break;
            case TOK_PLUS:
                t->kind = O_INC;
                match(TOK_PLUS);
                break;
            case TOK_MINUS:
                t->kind = O_DEC;
                match(TOK_MINUS);
                break;
            default:
                match(TOK_SEMI);
                t->kind = O_END;
                break;
            }
        }
        match(TOK_RBRACE2);
        t->next = NULL;
        n->attr.out_list = h.next;
    }
        break;
    case TOK_LBRACKET2:
        match(TOK_LBRACKET2);
        n = expr(TRUE);
        match(TOK_RBRACKET2);
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
    while (LA==TOK_ID || LA==TOK_HASH || LA==TOK_STR
    || LA==TOK_LPAREN || LA==TOK_LBRACE || LA==TOK_LBRACKET
    || LA==TOK_LBRACE2 || LA==TOK_LANGLE || LA==TOK_LBRACKET2) {
        q = new_node(OpKind);
        q->attr.op.tok = TOK_CONCAT;
        q->attr.op.child[0] = n;
        q->attr.op.child[1] = factor();
        n = q;
    }
    return n;
}

/* expr = term { "|" term } */
Node *expr(int bt)
{
    Node *n, *q;

    n = term();
    while (LA == TOK_ALTER) {
        q = new_node(OpKind);
        q->attr.op.tok = bt?TOK_ALTER_BT:TOK_ALTER;
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
    char id[MAX_TOKSTR_LEN];
    int num;

    is_start = FALSE;
    strcpy(id, token_string);
    match(TOK_ID);
    if (LA == TOK_STAR) {
        match(TOK_STAR);
        is_start = TRUE;
    }
    match(TOK_EQ);
    n = expr(FALSE);
    match(TOK_SEMI);
    if (is_start) {
        if (start_symbol != -1)
            err(1, GRA_ERR, "more than one start symbol");
        start_symbol = num = lookup_rule(id, n);
    } else {
        num = lookup_rule(id, n);
    }

    if (uses_lab1)
        label_usage[num] |= USES_LAB1;
    if (uses_lab2)
        label_usage[num] |= USES_LAB2;
    uses_lab1 = uses_lab2 = FALSE;
}

/* grammar = rule { rule } "." */
static void grammar(void)
{
    FixUp *fx, *next;

    rule();
    while (LA != TOK_DOT)
        rule();
    match(TOK_DOT);

    for (fx = fixup_list; fx != NULL; fx = next) {
        int i;
        char *name;

        name = *fx->p;
        for (i = 0; i < named_tokens_counter; i++)
            if (strcmp(named_tokens[i].name, name) == 0)
                break;
        if (i >= named_tokens_counter)
            err(1, GRA_ERR, "named token `%s' referenced but never defined", name);
        *fx->p = named_tokens[i].tokstr;
        next = fx->next;
        free(fx);
        free(name);
    }
}

static uint64_t first(Node *n)
{
    if (n->first != EMPTY_SET)
        return n->first;
    switch (n->kind) {
    case OutKind:
        n->first = EMPTY;
        break;
    case TermKind:
        n->first = 1ULL<<n->attr.t.tok_num;
        break;
    case NonTermKind:
        n->first = first(rules[n->attr.rule_num]);
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
        case TOK_ALTER_BT:   /* [[ | ]] */
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
        break;
    }
    return n->first;
}

static void compute_follow(Node *n, uint64_t in)
{
    uint64_t s;

    switch (n->kind) {
    case OutKind:
        break;
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
        case TOK_ALTER_BT:   /* [[ | ]] */
            compute_follow(n->attr.op.child[0], in);
            compute_follow(n->attr.op.child[1], in);
            break;
        case TOK_CONCAT:     /*   */
            s = first(n->attr.op.child[1]);
            compute_follow(n->attr.op.child[0], (s&EMPTY)?s|in:s);
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
    case TOK_ALTER_BT:   /* [[ | ]] */
        s = first(n->attr.op.child[0])&first(n->attr.op.child[1]);
        s &= ~EMPTY;
        if (s != EMPTY_SET)
            err(0, GRA_ERR, "Rule `%s': First/First conflict: { %s }", rule_names[rule_num], strset(s));
    case TOK_CONCAT:     /*   */
        conflict(n->attr.op.child[0], rule_num);
        conflict(n->attr.op.child[1], rule_num);
        break;
    case TOK_REPET:      /* {} */
    case TOK_OPTION:     /* [] */
        s = first(n)&n->follow;
        s &= ~EMPTY;
        if (s != EMPTY_SET)
            err(0, GRA_ERR, "Rule `%s': First/Follow conflict: { %s }", rule_names[rule_num], strset(s));
        conflict(n->attr.op.child[0], rule_num);
        break;
    }
}

static void check_for_left_rec(uint64_t rule_msk, Node *n)
{
    switch (n->kind) {
    case OutKind:
        break;
    case TermKind:
        break;
    case NonTermKind:
        if (rule_msk & (1ULL<<n->attr.rule_num))
            err(1, GRA_ERR, "rule `%s' contains left-recursion", rule_names[n->attr.rule_num]);
        check_for_left_rec(rule_msk|(1ULL<<n->attr.rule_num), rules[n->attr.rule_num]);
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
        case TOK_ALTER_BT:   /* [[ | ]] */
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

    check_for_left_rec(1ULL<<start_symbol, rules[start_symbol]);
    compute_follow_sets();
    for (i = 0; i < rule_counter; i++)
        conflict(rules[i], i);
}

static int recognize(Node *n, int *lab1, int *lab2, int bt)
{
    int res;

    switch (n->kind) {
    case OutKind: {
        int atbeg;
        OutList *t;

        atbeg = TRUE;
        for (t = n->attr.out_list; t != NULL; t = t->next) {
            switch (t->kind) {
            case O_LAST:
                strbuf_printf(outbuf, "%*s%s", (atbeg && out_indent>0)?out_indent:0, "", last_tokstr_buf);
                atbeg = FALSE;
                break;
            case O_LAB1:
                if (*lab1 == -1)
                    *lab1 = label_counter++;
                strbuf_printf(outbuf, "%*sL%d", (atbeg && out_indent>0)?out_indent:0, "", *lab1);
                atbeg = FALSE;
                break;
            case O_LAB2:
                if (*lab2 == -1)
                    *lab2 = label_counter++;
                strbuf_printf(outbuf, "%*sL%d", (atbeg && out_indent>0)?out_indent:0, "", *lab2);
                atbeg = FALSE;
                break;
            case O_INC:
                out_indent += 4;
                break;
            case O_DEC:
                out_indent -= 4;
                break;
            case O_END:
                strbuf_printf(outbuf, "\n");
                atbeg = TRUE;
                break;
            case O_TN:
            case O_VER:
                strbuf_printf(outbuf, "%*s%s", (atbeg && out_indent>0)?out_indent:0, "", t->val);
                atbeg = FALSE;
                break;
            }
        }
        if (!atbeg)
            strbuf_printf(outbuf, "\n");
        if (!bt)
            strbuf_flush(outbuf);
        res = TRUE;
    }
        break;
    case TermKind:
        if (curr_tok != n->attr.t.tok_num) {
            if (!bt)
                err(1, STR_ERR, "unexpected `%s'", lex_num2print(curr_tok));
            res = FALSE;
        } else {
            if (verbose) {
                int i;

                for (i = ver_indent; i; i--)
                    printf("--");
                printf("<< matched `%s' (%s:%d)\n", lex_num2print(curr_tok), string_file_path, lex_lineno());
            }
            strcpy(last_tokstr_buf, lex_token_string());
            if (n->attr.t.slot >= 0)
                strcpy(named_tokens[n->attr.t.slot].tokstr, last_tokstr_buf);
            curr_tok = lex_get_token();
            res = TRUE;
        }
        break;
    case NonTermKind: {
        int _lab1, _lab2;

        if (verbose) {
            int i;

            for (i = ver_indent; i; i--)
                printf("--");
            printf(">> replacing `%s' (%s:%d)\n", rule_names[n->attr.rule_num], string_file_path, lex_lineno());
        }
        ++ver_indent;
        _lab1 = _lab2 = -1;
        res = recognize(rules[n->attr.rule_num], &_lab1, &_lab2, bt);
        --ver_indent;
    }
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            if (first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                res = recognize(n->attr.op.child[0], lab1, lab2, bt);
            else
                res = recognize(n->attr.op.child[1], lab1, lab2, bt);
            break;
        case TOK_ALTER_BT: { /* [[ | ]] */
            State st;

            res = FALSE;
            save_state(&st);
            if ((first(n->attr.op.child[0]) & (1ULL<<curr_tok))
            && !(res=recognize(n->attr.op.child[0], lab1, lab2, TRUE)))
                restore_state(&st);
            if (!res && !(res=recognize(n->attr.op.child[1], lab1, lab2, bt)))
                restore_state(&st);
            dispose_state(&st);
        }
            break;
        case TOK_CONCAT:     /*   */
            if (res = recognize(n->attr.op.child[0], lab1, lab2, bt))
                res = recognize(n->attr.op.child[1], lab1, lab2, bt);
            break;
        case TOK_REPET:      /* {} */
            res = TRUE;
            while (res && (first(n->attr.op.child[0]) & (1ULL<<curr_tok)))
                res = recognize(n->attr.op.child[0], lab1, lab2, bt);
            break;
        case TOK_OPTION:     /* [] */
            res = TRUE;
            if (first(n->attr.op.child[0]) & (1ULL<<curr_tok))
                res = recognize(n->attr.op.child[0], lab1, lab2, bt);
            break;
        }
        break;
    }
    return res;
}

/* ============================================================ */
/* Source code emitters                                         */
/* ============================================================ */

static void emit(int indent, int new_line, char *fmt, ...)
{
    va_list args;

    while (indent--)
        fprintf(rec_file, "    ");
    va_start(args, fmt);
    vfprintf(rec_file, fmt, args);
    va_end(args);
    if (new_line)
        fprintf(rec_file, "\n");
}

#define EMIT(indent, ...)   emit(indent, 0, __VA_ARGS__)
#define EMITLN(indent, ...) emit(indent, 1, __VA_ARGS__)

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

static void write_rule(Node *n, int in_alter, int in_else, int indent)
{
    switch (n->kind) {
    case OutKind: {
        OutList *t;
        char fmtbuf[2048], argbuf[1024];
        int toadd;

        if (in_alter) {
            if (in_else)
                fprintf(rec_file, "if (1) {\n");
            else
                EMITLN(indent, "if (1) {");
            ++indent;
        }

        toadd = 0;
        fmtbuf[0] = argbuf[0] = '\0';
        for (t = n->attr.out_list; t != NULL; t = t->next) {
            switch (t->kind) {
            case O_LAST:
                strcat(fmtbuf, "%s");
                strcat(argbuf, ", last_tokstr");
                break;
            case O_LAB1:
                strcat(fmtbuf, "L%d");
                strcat(argbuf, ", getlab(&lab1)");
                break;
            case O_LAB2:
                strcat(fmtbuf, "L%d");
                strcat(argbuf, ", getlab(&lab2)");
                break;
            case O_INC:
                if (fmtbuf[0] == '\0')
                    EMITLN(indent, "indent += 4;");
                else
                    toadd += 4;
                break;
            case O_DEC:
                if (fmtbuf[0] == '\0')
                    EMITLN(indent, "indent += -4;");
                else
                    toadd -= 4;
                break;
            case O_END:
                EMIT(indent, "printf(\"%%*s%s\\n\", get_indent(), \"\"%s);", fmtbuf, argbuf);
                if (toadd != 0) {
                    fprintf(rec_file, "\n");
                    EMIT(indent, "indent += %d;", toadd);
                    toadd = 0;
                }
                if (t->next != NULL)
                    fprintf(rec_file, "\n");
                fmtbuf[0] = argbuf[0] = '\0';
                break;
            case O_VER: {
                char *x, *y;

                x = fmtbuf+strlen(fmtbuf);
                y = t->val;
                while (*y != '\0') {
                    switch (*y) {
                    case '\n':
                        *x++ = '\\';
                        *x++ = 'n';
                        break;
                    case '\"':
                        *x++ = '\\';
                        *x++ = '\"';
                        break;
                    case '\\':
                        *x++ = '\\';
                        *x++ = '\\';
                        break;
                    default:
                        *x++ = *y;
                        break;
                    }
                    ++y;
                }
                *x = *y;
            }
                break;
            case O_TN: {
                int i;
                char x[32];

                strcat(fmtbuf, "%s");
                for (i = 0; i < named_tokens_counter; i++)
                    if (named_tokens[i].tokstr == t->val)
                        break;
                assert(i < named_tokens_counter);
                sprintf(x, ", named_tokens[%d]", i);
                strcat(argbuf, x);
            }
                break;
            }
        }
        if (fmtbuf[0] != '\0')
            EMIT(indent, "printf(\"%%*s%s\\n\", get_indent(), \"\"%s);", fmtbuf, argbuf);
        if (toadd != 0) {
            fprintf(rec_file, "\n");
            EMIT(indent, "indent += %d;", toadd);
        }

        if (in_alter) {
            fprintf(rec_file, "\n");
            EMIT(indent-1, "}");
        }
    }
        break;
    case TermKind:
        if (in_alter) {
            if (in_else)
                fprintf(rec_file, "if (LA(T_%s)) {\n", lex_num2name(n->attr.t.tok_num));
            else
                EMITLN(indent, "if (LA(T_%s)) {", lex_num2name(n->attr.t.tok_num));
            EMITLN(indent+1, "match(T_%s, %d);", lex_num2name(n->attr.t.tok_num), n->attr.t.slot);
            EMIT(indent, "}");
        } else {
            EMIT(indent, "match(T_%s, %d);", lex_num2name(n->attr.t.tok_num), n->attr.t.slot);
        }
        break;
    case NonTermKind:
        if (in_alter) {
            if (in_else)
                fprintf(rec_file, "if (");
            else
                EMIT(indent, "if (");
            write_first_test(first(rules[n->attr.rule_num]));
            fprintf(rec_file, ") {\n");
            EMITLN(indent+1, "%s();", rule_names[n->attr.rule_num]);
            EMIT(indent, "}");
        } else {
            EMIT(indent, "%s();", rule_names[n->attr.rule_num]);
        }
        break;
    case OpKind:
        switch (n->attr.op.tok) {
        case TOK_ALTER:      /* | */
            write_rule(n->attr.op.child[0], TRUE, FALSE, indent);
            if (in_alter) {
                fprintf(rec_file, " else ");
                write_rule(n->attr.op.child[1], TRUE, TRUE, indent);
            } else {
                fprintf(rec_file, " else {\n");
                write_rule(n->attr.op.child[1], FALSE, FALSE, indent+1); fprintf(rec_file, "\n");
                EMIT(indent, "}");
            }
            break;
        case TOK_ALTER_BT:   /* [[ | ]] */
            assert(!"TODO");
            break;
        case TOK_CONCAT:     /*   */
            if (in_alter) {
                if (in_else)
                    fprintf(rec_file, "if (");
                else
                    EMIT(indent, "if (");
                write_first_test(first(n));
                fprintf(rec_file, ") {\n");
                write_rule(n->attr.op.child[0], FALSE, FALSE, indent+1); fprintf(rec_file, "\n");
                write_rule(n->attr.op.child[1], FALSE, FALSE, indent+1); fprintf(rec_file, "\n");
                EMIT(indent, "}");
            } else {
                write_rule(n->attr.op.child[0], FALSE, FALSE, indent); fprintf(rec_file, "\n");
                write_rule(n->attr.op.child[1], FALSE, FALSE, indent);
            }
            break;
        case TOK_REPET:      /* {} */
            if (in_alter) {
                if (in_else)
                    fprintf(rec_file, "if (");
                else
                    EMIT(indent, "if (");
                write_first_test(first(n->attr.op.child[0]));
                fprintf(rec_file, ") {\n");
                EMIT(indent+1, "while (");
                write_first_test(first(n->attr.op.child[0]));
                fprintf(rec_file, ") {\n");
                write_rule(n->attr.op.child[0], FALSE, FALSE, indent+2); fprintf(rec_file, "\n");
                EMITLN(indent+1, "}");
                EMIT(indent, "}");
            } else {
                EMIT(indent, "while (");
                write_first_test(first(n->attr.op.child[0]));
                fprintf(rec_file, ") {\n");
                write_rule(n->attr.op.child[0], FALSE, FALSE, indent+1); fprintf(rec_file, "\n");
                EMIT(indent, "}");
            }
            break;
        case TOK_OPTION:     /* [] */
            if (in_else)
                fprintf(rec_file, "if (");
            else
                EMIT(indent, "if (");
            write_first_test(first(n->attr.op.child[0]));
            fprintf(rec_file, ") {\n");
            write_rule(n->attr.op.child[0], FALSE, FALSE, indent+1); fprintf(rec_file, "\n");
            EMIT(indent, "}");
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
    "#include <string.h>\n"
    "#include \"lex.h\"\n");

    for (i = 0; i < SET_SIZE; i++)
        if (grammar_tokens & (1ULL<<i))
            fprintf(rec_file, "#define T_%s %d\n", lex_num2name(i), i);

    fprintf(rec_file,
    "static int curr_tok;\n"
    "static char *prog_name, *string_file;\n"
    "static char last_tokstr[MAX_TOKSTR_LEN];\n"
    "static int label_counter = 1;\n"
    "static int indent = 0;\n"
    "#define get_indent() (indent>0?indent:0)\n"
    "#define LA(x) (curr_tok == (x))\n"
    "static void error(void)\n"
    "{\n"
    "    fprintf(stderr, \"%%s: %%s:%%d: error: unexpected `%%s'\\n\", prog_name,\n"
    "    string_file, lex_lineno(), lex_num2print(curr_tok));\n"
    "    exit(EXIT_FAILURE);\n"
    "}\n"
    "static int getlab(int *lab)\n"
    "{\n"
    "    if (*lab == -1)\n"
    "        *lab = label_counter++;\n"
    "    return *lab;\n"
    "}\n"
    );
    if (named_tokens_counter > 0) {
        fprintf(rec_file,
        "static char named_tokens[%d][MAX_TOKSTR_LEN];\n"
        "static void match(int expected, int slot)\n"
        "{\n"
        "    if (curr_tok == expected) {\n"
        "        strcpy(last_tokstr, lex_token_string());\n"
        "        if (slot >= 0)\n"
        "            strcpy(named_tokens[slot], last_tokstr);\n"
        "        curr_tok = lex_get_token();\n"
        "    } else {\n"
        "        error();\n"
        "    }\n"
        "}\n", named_tokens_counter);
    } else {
        fprintf(rec_file,
        "static void match(int expected, int slot)\n"
        "{\n"
        "    if (curr_tok == expected) {\n"
        "        strcpy(last_tokstr, lex_token_string());\n"
        "        curr_tok = lex_get_token();\n"
        "    } else {\n"
        "        error();\n"
        "    }\n"
        "}\n"
        );
    }

    for (i = 0; i < rule_counter; i++)
        EMITLN(0, "static void %s(void);", rule_names[i]);

    for (i = 0; i < rule_counter; i++) {
        EMITLN(0, "void %s(void) {", rule_names[i]);
        if (label_usage[i] & USES_LAB1) {
            if (label_usage[i] & USES_LAB2)
                EMITLN(1, "int lab1 = -1, lab2 = -1;");
            else
                EMITLN(1, "int lab1 = -1;");
        } else if (label_usage[i] & USES_LAB2) {
            EMITLN(1, "int lab2 = -1;");
        }
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
            if (grammar_file_path == NULL)
                grammar_file_path = argv[i];
            else
                string_file_path = argv[i];
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
    if (grammar_file_path==NULL
    || (string_file_path==NULL && !print_first && !print_follow && !validate && !generate))
        usage(TRUE);

    if ((grammar_buf=read_file(grammar_file_path)) == NULL) {
        fprintf(stderr, "%s: cannot read file `%s'\n", prog_name, grammar_file_path);
        exit(EXIT_FAILURE);
    }
    curr_ch = grammar_buf;
    LA = get_token();
    grammar();
    free(grammar_buf);
    if (start_symbol == -1)
        err(1, GRA_ERR, "start symbol not defined");
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
        err(1, GRA_ERR, "the grammar contains the following undefined symbols: %s", buf);
    }
    if (validate) {
        if (rule_counter <= 64)
            conflicts();
        else
            err(1, GRA_ERR, "cannot validate the grammar (more than 64 rules)");
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
    if (string_file_path != NULL) {
        int lab1, lab2;

        lab1 = lab2 = -1;
        if (lex_init(string_file_path) == -1) {
            fprintf(stderr, "%s: lex_init() failed!\n", prog_name);
            exit(EXIT_FAILURE);
        }
        outbuf = strbuf_new(256);
        curr_tok = lex_get_token();
        if (verbose) {
            printf(">> replacing `%s' (%s:%d)\n", rule_names[start_symbol], string_file_path, lex_lineno());
            ++ver_indent;
        }
        recognize(rules[start_symbol], &lab1, &lab2, FALSE);
        strbuf_flush(outbuf);
        strbuf_destroy(outbuf);
        if (lex_finish() == -1)
            ;
    }
    return 0;
}
