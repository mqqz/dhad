GRAMMAR

Program
program ::= { top_level } EOF ;

Top Level
top_level ::= import
            | function
            | statement ;

Import
import ::= KW_IMPORT identifier ";" ;

Function
function ::= KW_FN identifier "(" [ parameters ] ")" [ ":" type ] block ;

Parameters
parameters ::= parameter { "," parameter } ;
parameter  ::= identifier ":" type ;

Types
type ::= KW_INT
       | KW_FLOAT
       | KW_BOOL
       | KW_CHAR
       | KW_STRING
       | KW_NULL ;

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
          | "(" expression ")" ;

--------------------------------------------------
-- Lexical
--------------------------------------------------

identifier ::= LETTER { LETTER | DIGIT | "_" } ;
number ::= DIGIT { DIGIT } ;
string ::= '"' { ANY_CHAR - '"' } '"' ;

