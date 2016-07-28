## Input Grammar

The program reads grammars in [EBNF](https://en.wikipedia.org/wiki/Extended_Backus–Naur_Form).
Here is an example of a grammar describing the [JSON](https://en.wikipedia.org/wiki/JSON) format:

    ! I'm a comment.
    object* = "{" [ pairs ] "}" ;
    pairs = pair { "," pair } ;
    pair = #STR ":" value ;
    value = #STR | #NUM | "true" | "false" | "null" | object | array ;
    array = "[" [ elements ] "]" ;
    elements = value { "," value } ;
    .

From this example we can see several things:

 - `=` is used to define nonterminals.
 - Each production rule is terminated by a `;`.
 - The whole grammar is terminated by a single `.`.
 - The start symbol (there must be only one) is denoted with a `*`.
 - `|` is used to denote alternatives.
 - `{}` are used to enclose parts that can repeat zero or more times.
 - `[]` are used to enclose parts that are optional.
 - `!` starts a comment that extends until the end of the line.

`()` can be used for grouping. For example:

    expr = term { ( "+" | "-" ) term } ;

Terminals symbols (tokens) can be specified in two ways:
 - With a `#` followed by the name of a token (see `tokens.def`).
 - Enclosed in quotes. If what is between the quotes is an identifier, then
   it is treated as a keyword token. Otherwise, what is between the quotes
   must be something that can be lexed by `lex_get_token()` (see `lex.c`).

There is no need for an explicit symbol to denote the empty string (epsilon, ε).

## Usage

You can use the input grammar to recognize an input string right away or to
generate a recognizer program.

### Recognizing an input string

The input string is specified as a second argument:

    $ ./genrec examples/grammar1.ebnf examples/string1

The `-v` option can be used to trace out the leftmost derivation that is performed.
The program will exit silently if the string does not contain any syntax error.

### Generating a recognizer

The `-g` option can be used to generate a recognizer program in the C programming
language. The recognizer is emitted to stdout by default (this can be changed with
the `-o` option).

For example, the following generates a recognizer for the above JSON grammar:

    $ ./genrec examples/grammar8.ebnf -g -o json_rec.c
    $ cc json_rec.c lex.c util.c -o json_rec
    $ ./json_rec examples/string8

The generated program takes as single argument the input string. It also uses
`lex.c` for the lexing part (and `lex.c` uses functions defined in `util.c`,
that is why it appears there in the linking).

## Debugging the grammar

The program can also be used to get more information about the input grammar and
to detect possible conflicts.

The `-f` options prints the _First_ sets of all nonterminals, and the `-l` option
prints the _Follow_ sets.

The `-c` option checks the input grammar for [LL(1) Conflicts](https://en.wikipedia.org/wiki/LL_parser#Conflicts).
These conflicts can be left recursion (immediate or indirect), First/First conflicts,
and First/Follow conflicts.

## Limitations

 - Sets are represented internally with `uint64_t` bit vectors. This limits the
   number of rules to 64, and the number of terminals (tokens) to 63 (the most
   significant bit is reserved to represent ε).
