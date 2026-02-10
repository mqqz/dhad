GRAMMAR

Program
program ::= { top_level } EOF ;

Top Level
top_level ::= import
            | function
            | type_decl
            | statement ;

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
