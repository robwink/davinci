%{
#include <stdio.h>
#include <setjmp.h>
#include "parser.h"

Var *p_mknod(int , Var *, Var *);
Var *p_mkval(int , char *);

extern Var *curnode;
extern char *yytext;
extern FILE *ftos;
jmp_buf env;

extern int indent;
extern int pp_count;

int log_it = 0;
char *pp_str;

void out()
{
    printf("%s", pp_str);
}

%}

%token WHILE CONTINUE BREAK RETURN FOR WHERE
%token IF ELSE
%token IVAL RVAL STRING
%token ID
%token DEC_OP INC_OP INCSET_OP DECSET_OP MULSET_OP DIVSET_OP
%token LT_OP GT_OP GE_OP LE_OP EQ_OP NE_OP
%token AND_OP OR_OP CAT_OP LSHIFT_OP RSHIFT_OP
%token QUIT HELP LIST
%token FUNC_DEF
%token SHELL

%%

start
    : statement             { curnode = $$ = $1;  YYACCEPT; }
    | error separator       
            { 
                indent = 0; 
                curnode = NULL; 
                YYACCEPT;
            }
    |
    ;


statement
    : expr_stmt                  { $$ = $1; }
    | selection_statement                   { $$ = $1; }
    | jump_statement                        { $$ = $1; }
    | compound_statement                    { $$ = $1; }
    | iteration_statement                   { $$ = $1; }
    | command_statement                     { $$ = $1; }
    | SHELL                                 { $$ = pp_shell(yytext); }
    | QUIT                                  { YYABORT; }
    | FUNC_DEF separator                    { $$ = NULL; }
    | FUNC_DEF id '(' args ')' compound_statement           
                                            { $$ = NULL; }
    /* function body tree is captured somewhere else */
    ;

command_statement
    : help_statement separator      { $$ = $1; }
    ;

help_statement
    : HELP                          { $$ = pp_help(NULL); }
    | HELP id                       { $$ = pp_help($2); }
    | HELP string                   { $$ = pp_help($2); }
    | '?'                           { $$ = pp_help(NULL); }
    | '?' id                        { $$ = pp_help($2); }
    ;

expr_stmt
    : separator                     { $$ = NULL; }
    | expr separator          { $$ = p_mknod(ID_LINE, $1, $2); }
    ;

compound_statement
    :   '{' statement_list '}'  { $$ = $2; }
    ;

statement_list
    : statement                             { $$ = $1; } 
    | statement_list statement              { $$ = p_rlist(ID_LIST, $1, $2); }
    ;

selection_statement
    : IF '(' expr ')' statement ELSE statement 
                            { $$ = p_mknod(ID_IF, $3, p_mknod(ID_ELSE, $5, $7)); }
    | IF '(' expr ')' statement 
                            { $$ = p_mknod(ID_IF, $3, $5); }
    ;


separator
    : ';'                           {  
										if (pp_str && strlen(pp_str) > 1) {
											$$ = p_mkval(ID_STRING, pp_str); 
										} else {
											$$ = NULL;
										}
                                    }
    | '\n'                          { 
                                        pp_count = 0; 
										if (pp_str && strlen(pp_str) > 1) {
											$$ = p_mkval(ID_STRING, pp_str); 
										} else {
											$$ = NULL;
										}
                                    }
    ;


iteration_statement
    : WHILE '(' expr ')' statement    { $$ = p_mknod(ID_WHILE, $3, $5); }
    | FOR '(' expr_stmt expr_stmt expr ')' statement
                                        { $$ = p_mknod(ID_LIST, $3,
                                               p_mknod(ID_WHILE, $4,
                                               p_mknod(ID_FOR, $7, $5))); }
    | FOR '(' expr_stmt expr_stmt ')' statement
                                        { $$ = p_mknod(ID_LIST, $3,
                                               p_mknod(ID_WHILE, $4,
                                               p_mknod(ID_FOR, $6, NULL))); }
;

jump_statement
    : CONTINUE separator          { $$ = p_mknod(ID_LINE, 
                                        p_mknod(ID_CONT,NULL,NULL),
                                        $2); 
                                  }
    | BREAK separator             { $$ = p_mknod(ID_LINE,
                                         p_mknod(ID_BREAK,NULL,NULL),
                                         $2); 
                                    }
    | RETURN expr separator  { $$ = p_mknod(ID_LINE, 
                                    p_mknod(ID_RETURN,$2,NULL),
                                    $3); }
    ;

args
    :                                   { $$ = NULL; }
    | arg                               { $$ = p_mknod(ID_ARGS, NULL, $1); }
    | args ',' arg                      { $$ = p_mknod(ID_ARGS, $1, $3); }
    ;

arg
    : concat                            { $$ = p_mknod(ID_ARG, NULL, $1); }
    | id '=' expr                       { $$ = p_mknod(ID_ARG, $1, $3); }
    ;

ranges
    : range2                            { $$ = p_mknod(ID_RANGES, NULL, $1); }
    | ranges ',' range2                 { $$ = p_mknod(ID_RANGES, $1, $3) ; }
    ;

range2
    :                                 { $$ = p_mknod(ID_RANGE, NULL, NULL); }
    | expr                      { $$ = p_mknod(ID_RANGE, $1, $1); }
    | ':' ':' expr              { $$ = p_mknod(ID_RSTEP, NULL, $3); }
    | range                           { $$ = $1; }
    | range ':'                       { $$ = p_mknod(ID_RSTEP, $1, NULL); }
    | range ':' expr              { $$ = p_mknod(ID_RSTEP, $1, $3); }
    ;

range
    : ':' expr                  { $$ = p_mknod(ID_RANGE, NULL, $2); }
    | expr ':'                  { $$ = p_mknod(ID_RANGE, $1, NULL); }
    | expr ':' expr       { $$ = p_mknod(ID_RANGE, $1, $3); }
    ;

expr
    : concat                            { $$ = $1; }
    | assignment_expr                   { $$ = $1; }
    ;

assignment_expr
    : postfix_expr '=' expr       { $$ = p_mknod(ID_SET,$1,$3); }
    | postfix_expr INC_OP         { $$ = p_mknod(ID_INC,$1,p_mkval(ID_IVAL, (char *)strdup("1"))); }
    | postfix_expr DEC_OP         { $$ = p_mknod(ID_DEC,$1,p_mkval(ID_IVAL, (char *)strdup("1"))); }
    | postfix_expr INCSET_OP expr { $$ = p_mknod(ID_INC,$1,$3); }
    | postfix_expr DECSET_OP expr { $$ = p_mknod(ID_DEC,$1,$3); }
    | postfix_expr MULSET_OP expr { $$ = p_mknod(ID_MULSET,$1,$3); }
    | postfix_expr DIVSET_OP expr { $$ = p_mknod(ID_DIVSET,$1,$3); }
    | postfix_expr '[' WHERE expr ']' '=' expr  
                                        { $$ = p_mknod(ID_SET,
                                               p_mknod(ID_WHERE, $1, $4), $7); }
    ;


concat
    : logical_OR                        { $$ = $1; }
    | concat CAT_OP logical_OR          { $$ = p_mknod(ID_CAT, $1, $3); }
    ;

logical_OR
    : logical_AND                       { $$ = $1; }
    | logical_OR OR_OP logical_AND      { $$ = p_mknod(ID_OR ,$1,$3); }
    ;

logical_AND
    : equality_expr                     { $$ = $1; }
    | logical_AND AND_OP equality_expr  { $$ = p_mknod(ID_AND ,$1,$3); }
    ;

equality_expr
    : relation_expr                     { $$ = $1; }
    | equality_expr EQ_OP relation_expr { $$ = p_mknod(ID_EQ ,$1,$3); }
    | equality_expr NE_OP relation_expr { $$ = p_mknod(ID_NE ,$1,$3); }
    ;

relation_expr
    : shift_expr                     { $$ = $1; }
    | relation_expr LT_OP additive_expr { $$ = p_mknod(ID_LT ,$1,$3); }
    | relation_expr GT_OP additive_expr { $$ = p_mknod(ID_GT ,$1,$3); }
    | relation_expr LE_OP additive_expr { $$ = p_mknod(ID_LE ,$1,$3); }
    | relation_expr GE_OP additive_expr { $$ = p_mknod(ID_GE ,$1,$3); }
    ;

shift_expr
    : additive_expr                     { $$ = $1; }
    | additive_expr LSHIFT_OP mult_expr       { $$ = p_mknod(ID_LSHIFT,$1,$3); }
    | additive_expr RSHIFT_OP mult_expr       { $$ = p_mknod(ID_RSHIFT,$1,$3); }
	;

additive_expr
    : mult_expr                         { $$ = $1; }
    | additive_expr '+' mult_expr       { $$ = p_mknod(ID_ADD,$1,$3); }
    | additive_expr '-' mult_expr       { $$ = p_mknod(ID_SUB,$1,$3); }
    ;

mult_expr
    : power_expr                        { $$ = $1; }
    | mult_expr '*' power_expr          { $$ = p_mknod(ID_MULT, $1, $3); }
    | mult_expr '/' power_expr          { $$ = p_mknod(ID_DIV, $1, $3); }
    | mult_expr '%' power_expr          { $$ = p_mknod(ID_MOD, $1, $3); }
    ;

power_expr
    : unary_expr                        { $$ = $1; }
    | power_expr '^' unary_expr         { $$ = p_mknod(ID_POW,$1,$3); }
    ;

unary_expr
    : rhs_postfix_expr                      { $$ = $1; }
    | '-' unary_expr                    { $$ = p_mknod(ID_UMINUS,NULL,$2); }
    ;

id: ID                    { $$ = p_mkval(ID_ID, (char *)$1); free($1); }
ival : IVAL               { $$ = p_mkval(ID_IVAL, (char *)$1); free($1);  }
rval: RVAL                { $$ = p_mkval(ID_RVAL, (char *)$1); free($1); }
string: STRING            { $$ = p_mkval(ID_STRING, (char *)$1); free($1); }
;

primary_expr
    : id                            { $$ = $1; }
    | '(' expr ')'                  { $$ = $2; }
    ;


postfix_expr
    : primary_expr                  { $$ = $1; }
    | postfix_expr '[' ranges ']'   { $$ = p_mknod(ID_ARRAY,$1,$3); }
    | postfix_expr '.' id           { $$ = p_mknod(ID_DEREF,$1,$3); }
    | postfix_expr '(' args ')'     { $$ = p_mknod(ID_FUNCT,$1,$3); }
    | '@' id '(' arg ')'           { $$ = p_mknod(ID_PARALLEL,$2,$4); }
    | '@' '(' arg ')'              { $$ = p_mknod(ID_PARALLEL,NULL,$3); }
    | postfix_expr '(' '?' ')'      { $$ = pp_exact_help($1); }
    | postfix_expr '.' HELP         { $$ = pp_exact_help($1); }
    ;

rhs_postfix_expr
    : ival                                          { $$ = $1; }
    | rval                                          { $$ = $1; }
    | string                                        { $$ = $1; }
    | postfix_expr                                  { $$ = $1; }
    | '$' id                        { $$ = p_mknod(ID_ARGV, $2, NULL); }
    | '$' id '[' expr ']'           { $$ = p_mknod(ID_ARGV, $2, $4); }
    | '$' ival                      { $$ = p_mknod(ID_ARGV, $2, NULL); }
    | '$' ival '[' expr ']'         { $$ = p_mknod(ID_ARGV, $2, $4); }
    | '{' args '}'                  { $$ = p_mknod(ID_CONSTRUCT, $2, NULL); }
    ;

%%


