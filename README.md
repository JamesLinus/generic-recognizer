## Input Grammar

The program reads grammars in [EBNF](https://en.wikipedia.org/wiki/Extended_Backus–Naur_Form).
Here is an example of a grammar describing the [JSON](https://en.wikipedia.org/wiki/JSON) format:

    ! I'm a comment.
    object* = "{" [ pairs ] "}" ;
    pairs = pair { "," pair } ;
    pair = #STR2 ":" value ;
    value = #STR2 | #NUM | "true" | "false" | "null" | object | array ;
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

The `-g` option can be used to generate a recursive descent recognizer program
in the C programming language. The recognizer is emitted to stdout by default
(this can be changed with the `-o` option).

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

Sometimes, a grammar containing conflicts can still be used and will behave
in a deterministic manner. The following points are relevant in determining
the behavior:

 - All the operators consult uniquely the First sets of their operands to decide
   where to direct the flow of control of the recognition.
 - The `|` operator tests its operands from left to right. For example, given
   the rule

     ```
     S = α | β ;
     ```

   if there is a First/First conflict between `α` and `β`, then `β` will be unreachable
   on those symbols that cause the conflict (see [grammar7.ebnf](examples/grammar7.ebnf)).
 - `[]` (and `{}`) tests the current token of lookahead against the First set of its
   operand to decide if it applies or not. For example, given the rule

   ```
   S = α [ β ] γ ;
   ```

   `[]` will apply `β` when the current token is in First(β) and will not care if
   it is also in First(γ). This is how the [dangling else problem](https://en.wikipedia.org/wiki/Dangling_else)
   is resolved (see [grammar6.ebnf](examples/grammar6.ebnf)).

Left recursion almost certainly will cause the death of the recognizer by infinite
recursion, so under the `-c` option left recursion is considered a fatal error.

## Generating output

You can cause the generation of output by embedding outputting constructs
directly into the grammar. The basic idea is borrowed from [META II](https://en.wikipedia.org/wiki/META_II).
The following example translates simple arithmetic expressions into assembly code
for a stack machine:

    $ cat expr.ebnf
    program* = { expr "." } ;
    expr = term { "+" term {{ "ADD" }}
                | "-" term {{ "SUB" }} } ;
    term = factor { "*" factor {{ "MUL" }}
                  | "/" factor {{ "DIV" }} } ;
    factor = #ID {{ "LD " * }} | #NUM {{ "LDL " * }} | "(" expr ")" ;
    .
    $ cat expr_string
    a*b+c/2.
    $ ./genrec expr.ebnf expr_string
    LD a
    LD b
    MUL
    LD c
    LDL 2
    DIV
    ADD

Output constructs are introduced with `{{}}`. These constructs need to be
attached (concatenated) to something (before or after depending on when you
want the output to be emitted). The output is line-oriented, meaning that a
new-line character is emitted after each construct. One or more of the following
things can appear between the `{{}}`:

 - A double-quoted string (e.g. `"ADD"`) to output text verbatim.
 - A `;` to output a new-line character.
 - A `*` to output the last matched token.
 - A `*1` or `*2` to output a generated label of the form `L1, L2, ...`.
   The first time `*1` appears in a production rule a label is generated
   and outputted and this same label will be outputted wherever `*1` is
   encountered again in the _same_ rule. Something similar happens for `*2`.
 - `+` to increase the indentation level.
 - `-` to decrease the indentation level.
 - An identifier to output a named-token (explained below). This is to
   complement `*` when one wants to output a token other than the last
   one matched.

The default indentation level is zero. An indentation level of zero or below
means the lines are outputted with no indentation. Each indentation level
represents 4 space characters.

One can give a name to a matched token and reference it later within `{{...}}`.
Token naming only works for tokens specified through the form `# token` (e.g.
`#ID`, `#NUM`, `#STR`). For example:

    ! To give a name to a matched token one writes
    !     < # token : name >
    assign_stmt = <#ID:dest_var> "=" expr {{ "ST "dest_var }} ;

There are some limitations. First, all names reside in a single flat namespace;
one can reference names defined in other rules and names with same spelling in
different rules will collide. Second, token strings of named-tokens are stored
in static buffers, one buffer per name, so recursive rules may produce some
unintended results due to overwriting of said buffers.

The following example uses the label generation capabilities:

    if_stmt = "if" expr "then" {{ "BF " *1 }}
              stmt "else" {{ "B " *2 ; *1 }}
              stmt {{ *2 }} ;

## Limitations

 - Sets are represented internally with `uint64_t` bit vectors. This limits the
   number of rules to 64, and the number of terminals (tokens) to 63 (the most
   significant bit is reserved to represent ε).
