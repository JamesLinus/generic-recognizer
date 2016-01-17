## Description

Program that, given as input a grammar and a string, will tell if the string belongs to the language defined by the grammar. If the string does belong to the language, it will exit silently. Otherwise, it will point out in which line of the input string the error resides.

## Usage

    genrec [ -f -c -v -h ] <grammar_file> <string_file>

The grammar must be provided in a form similar to [EBNF](https://en.wikipedia.org/wiki/Extended_Backus–Naur_Form) (they differ on how terminal symbols are specified). The syntax of an input grammar file is as follows:

    grammar = rule { rule } "."
    rule = ID [ "*" ] "=" ( nonterm | term ) ";"
    nonterm = option { "|" option }
    option =  concat { "," concat }
    concat = ID | "(" nonterm ")" | "{" nonterm "}" | "[" nonterm "]"
    term = "#" NUM

The operators and their meanings are:

Operator | Meaning
:---:|:---
= | definition
, | concatenation
\| | aternation
[...] | option
{...} | repetition
(...) | grouping
; | rule termination
. | grammar termination

There must be always one (and only one) start symbol, marked with a `*` in the left-hand side of the rule.

Comment start with a `!` and extend until the end of the line.

The grammar **must not contain left recursion**. If the grammar does contain left recursion, the recognizer will likely enter into an infinite recursive loop and crash (as it is known, left factoring might be used to remove left recursion).

The user is responsible of providing a lexer with the following interface:

```c
extern int lex_lineno; /* current line number (used for messages) */
int lex_init(char *file_path); /* open and read the input string (return -1 on failure) */
int lex(void); /* get the next token from the string */
int lex_finish(void); /* do any required cleanup (return -1 on failure) */
```

Tokens are identified by numbers. A token's identifying number will follow the `#` in the `term` rule, and `lex()` must return that same number when the token is encountered. I include an example lexer with the above interface.

The following command line options are available:
* **-f**: will print the FIRST sets of the grammar rules.
* **-c**: after parsing the grammar, will check if it contains left recursion and report an error if it does. This options works only with grammars that have less than 65 rules.
* **-v**: activate the verbose mode. In this mode, the recognizer will print everything it does (replace a rule or match a token).

## Examples

There are several example grammars and correspongding valid input strings in the `examples` folder.

```
$ ls -1 examples
grammar1.ebnf
grammar2.ebnf
grammar3.ebnf
grammar4.ebnf
string1
string2
string3
string4
$ cat examples/grammar1
!
! Grammar for simple arithmetic expressions.
!

program* = { expr , dot } , eof ;
expr = term , { ( plus | minus ) , term } ;
term = factor , { ( mul | div ) , factor } ;
factor = id | num | lparen , expr , rparen ;

id = #29 ;
num = #30 ;
plus = #1 ;
minus = #2 ;
div = #3 ;
mul = #4 ;
lparen = #26 ;
rparen = #27 ;
dot = #28 ;
eof = #25 ;

.
$ cat examples/string1
10 .
10 + 20 .
10 + 20*30 .
(10+20) * (30+40) .
((10+20) - (30+40)) - (50+70)*80 .
$ ./genrec examples/grammar1.ebnf examples/string1
$ echo $?
0
$ ./genrec examples/grammar1.ebnf examples/string1 -c
$ echo $?
0
$ ./genrec examples/grammar1.ebnf examples/string1 -f
FIRST(expr) = { #26, #29, #30 }
FIRST(dot) = { #28 }
FIRST(eof) = { #25 }
FIRST(program) = { #26, #29, #30 }
FIRST(term) = { #26, #29, #30 }
FIRST(plus) = { #1 }
FIRST(minus) = { #2 }
FIRST(factor) = { #26, #29, #30 }
FIRST(mul) = { #4 }
FIRST(div) = { #3 }
FIRST(id) = { #29 }
FIRST(num) = { #30 }
FIRST(lparen) = { #26 }
FIRST(rparen) = { #27 }
$ ./genrec examples/grammar1.ebnf examples/string1 -v
>> replacing `program' << (examples/string1:1)
-->> replacing `expr' (examples/string1:1)
---->> replacing `term' (examples/string1:1)
------>> replacing `factor' (examples/string1:1)
-------->> replacing `num' (examples/string1:1)
----------<< matched #30 (examples/string1:1)
-->> replacing `dot' (examples/string1:1)
----<< matched #28 (examples/string1:1)
-->> replacing `expr' (examples/string1:2)
---->> replacing `term' (examples/string1:2)
------>> replacing `factor' (examples/string1:2)
-------->> replacing `num' (examples/string1:2)
----------<< matched #30 (examples/string1:2)
---->> replacing `plus' (examples/string1:2)
------<< matched #1 (examples/string1:2)
---->> replacing `term' (examples/string1:2)
------>> replacing `factor' (examples/string1:2)
-------->> replacing `num' (examples/string1:2)
----------<< matched #30 (examples/string1:2)
-->> replacing `dot' (examples/string1:2)
----<< matched #28 (examples/string1:2)
[...]
```

## Limitations

* It only recognizes LL(1) grammars.
* First sets are represented as `uint64_t` bit vectors. Because of this, terminals (tokens) must have values in the interval [0, 64].
* As mentioned earlier, the `-c` options only works with grammars that have less than 65 rules (for similar reasons as the previous limitation).
* Being only a recognizer, its usefulness is limited (unlike a parser, it is not going to return any AST or similar representation). Nevertheless, it may be of interest to those learning recursive-descent parsing.
