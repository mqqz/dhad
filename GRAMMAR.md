GRAMMAR

Program
program ::= { top_level } EOF ;

Top Level
top_level ::= import
            | function
            | type_decl ;

Import
import ::= KW_IMPORT identifier ";" ;

Function
function ::= KW_FN identifier "(" [ parameters ] ")" [ ":" type ] block ;

Type Declaration
type_decl ::= KW_TYPE struct_decl
            | KW_TYPE enum_decl ;

struct_decl ::= KW_STRUCT identifier "{" [ struct_fields ] "}" ";" ;
struct_fields ::= struct_field { "،" struct_field } ;
struct_field ::= identifier ":" type ;

enum_decl ::= KW_ENUM identifier "{" [ enum_variants ] "}" ";" ;
enum_variants ::= enum_variant { "،" enum_variant } ;
enum_variant ::= identifier [ ":" type ] ;

Parameters
parameters ::= parameter { "," parameter } ;
parameter  ::= identifier ":" type ;

Types
type ::= type "|" type_primary
       | type_primary ;

type_primary ::= KW_INT
               | KW_FLOAT
               | KW_BOOL
               | KW_CHAR
               | KW_STRING
               | KW_NULL
               | identifier
               | "[" type "]"
               | "(" type ")"
               | "(" type "،" type { "،" type } ")" ;

Block
block ::= "{" { statement } "}" ;

--------------------------------------------------
-- Statements (split to remove dangling else)
--------------------------------------------------

statement ::= matched_stmt
            | unmatched_stmt ;

matched_stmt ::= declaration ";"
               | assignment ";"
               | while_stmt
               | for_stmt
               | return_stmt ";"
               | break_stmt ";"
               | continue_stmt ";"
               | call_stmt ";"
               | block ;

unmatched_stmt ::= if_stmt ;

--------------------------------------------------
-- Declarations / Assignment
--------------------------------------------------

declaration ::= ( KW_LET | KW_CONST ) identifier [ ":" type ] "=" expression ;

assignment ::= identifier "=" expression ;

--------------------------------------------------
-- If / Else (disambiguated)
--------------------------------------------------

if_stmt ::= KW_IF "(" expression ")" statement
            [ KW_ELIF "(" expression ")" statement ]
            [ KW_ELSE statement ] ;

--------------------------------------------------
-- Loops
--------------------------------------------------

while_stmt ::= KW_WHILE "(" expression ")" statement ;

for_stmt ::= KW_FOR "(" expression ")" statement ;

--------------------------------------------------
-- Control Flow
--------------------------------------------------

return_stmt ::= KW_RETURN [ expression ] ;
break_stmt  ::= KW_BREAK ;
continue_stmt ::= KW_CONTINUE ;

--------------------------------------------------
-- Function Call
--------------------------------------------------

call_stmt ::= identifier "(" [ arguments ] ")" ;
arguments ::= expression { "," expression } ;

--------------------------------------------------
-- Expressions
--------------------------------------------------

expression ::= logical_or ;

logical_or ::= logical_and { KW_OR logical_and } ;
logical_and ::= equality { KW_AND equality } ;

equality ::= comparison { ( "==" | "!=" ) comparison } ;
comparison ::= term { ( "<" | ">" | "<=" | ">=" ) term } ;

term ::= factor { ( "+" | "-" ) factor } ;
factor ::= unary { ( "*" | "/" ) unary } ;

unary ::= ( "!" | "-" ) unary
        | primary ;

primary ::= number
          | string
          | KW_TRUE
          | KW_FALSE
          | KW_NULL
          | identifier
          | call_stmt
          | struct_literal
          | field_access
          | "(" expression ")" ;

struct_literal ::= identifier "{" [ field_inits ] "}" ;
field_inits ::= field_init { "،" field_init } ;
field_init ::= identifier ":" expression ;

field_access ::= primary "." identifier ;

--------------------------------------------------
-- Lexical
--------------------------------------------------

identifier ::= LETTER { LETTER | DIGIT | "_" } ;
number ::= DIGIT { DIGIT } ;
string ::= '"' { ANY_CHAR - '"' } '"' ;


Execution semantics
-------------------

`عدد` is a signed 64-bit integer and `عشري` is an IEEE-754 double.
Integer overflow (including negation and division), division by zero, null array
access, and out-of-bounds indices are runtime errors. Numeric literals outside
these ranges are rejected. Logical `و` and `أو` evaluate left to right and
short-circuit. Floating equality is exact, including the usual NaN rules.

Only imports, type declarations, and functions are allowed at the top level.
Programs execute a parameterless `بداية` returning integer, boolean, or null/void.
The integer exit result is reduced to its signed low 32 bits; the host operating
system may further reduce process exit statuses. `اطبع` returns status zero.

String literals cannot contain NUL bytes. Strings compare by contents (null equals only null), and arrays compare by
identity. Equality on structs, tuples, unions, and functions is unsupported.
`ثابت` prevents rebinding and direct indexed writes; mutable aliases to arrays
still share their contents. Array length and indices use 64-bit integers.
