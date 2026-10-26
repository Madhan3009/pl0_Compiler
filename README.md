# pl0c

A compiler for [PL/0](https://en.wikipedia.org/wiki/PL/0), the small teaching
language from Niklaus Wirth's *Algorithms + Data Structures = Programs*,
written in C++17. `pl0c` translates PL/0 source into C, which any C compiler
can then turn into a native executable.

## Building

Requires a C++17 compiler (g++ by default).

```sh
make          # builds ./pl0c
make test     # builds and runs the test suite
make clean
```

## Usage

```sh
./pl0c program.pl0 > program.c
cc -o program program.c
```

The generated C is written to standard output. Errors go to standard error
with the line number, and the exit status is non-zero:

```
pl0c: error: 4: undefined symbol: x
```

## Example

`test/pass/17_factorial.pl0`:

```pascal
{ A complete program: compute 10! iteratively }
const n = 10;
var i, fact;

procedure step;
    begin
        fact := fact * i;
        i := i + 1
    end;

begin
    fact := 1;
    i := 1;
    while i <= n do call step
end.
```

compiles to:

```c
const long pl0_n = 10;
long pl0_i;long pl0_fact;
void
pl0_step(void)
{
{
pl0_fact=pl0_fact*pl0_i;
pl0_i=pl0_i+1;
}
;
}

int
main(int argc, char *argv[])
{
{
pl0_fact=1;
pl0_i=1;
while(pl0_i<=pl0_n)pl0_step();
;
}
;return 0;
}
```

Every identifier gets a `pl0_` prefix, so PL/0 names such as `printf` or
`double` can't clash with C keywords or library functions.

## Grammar

```
program    = block "." ;
block      = [ "const" ident "=" number { "," ident "=" number } ";" ]
             [ "var" ident { "," ident } ";" ]
             { "procedure" ident ";" block ";" } statement ;
statement  = [ ident ":=" expression
             | "call" ident
             | "begin" statement { ";" statement } "end"
             | "if" condition "then" statement
             | "while" condition "do" statement ] ;
condition  = "odd" expression
           | expression ( "=" | "#" | "<" | "<=" | ">" | ">=" ) expression ;
expression = [ "+" | "-" ] term { ( "+" | "-" ) term } ;
term       = factor { ( "*" | "/" ) factor } ;
factor     = [ "+" | "-" ] ( ident | number | "(" expression ")" ) ;
```

Comments are written `{ like this }` and may span lines. `#` means "not equal".

## How it works

`pl0c` is a single-pass compiler in one file, `compiler.cc`:

- **Lexer.** Turns the source into tokens. Identifiers and numbers are
  `std::string_view`s into the source buffer, so nothing is copied. Numbers
  are parsed with `std::from_chars`, and out-of-range values are rejected.
- **Parser.** Recursive descent, with one function per grammar rule.
- **Semantic checks.** A scoped symbol table rejects undefined or duplicate
  identifiers, assignments to constants or procedures, `call`s on anything
  that isn't a procedure, and procedures nested more than one level deep.
- **Code generation.** C is emitted while parsing, with no intermediate
  representation.

## Tests

`test/test.sh` runs every program in `test/pass/` (which must compile) and
`test/fail/` (which must be rejected). A failing test can name the error it
expects on its first line:

```pascal
{ expect: undefined symbol }
```

The harness also reports crashes and catches infinite loops with a timeout.
Run it with `-v` to see the compiler's output for failed tests.

## Limitations

- No I/O statements yet, so compiled programs can't print results.
- Procedures can be nested only one level deep.
- Numbers are integers that fit in a C `long`.
