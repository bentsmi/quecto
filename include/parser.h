#ifndef PARSER_H
#define PARSER_H

#include "common.h"
#include "symbol_table.h"
#include "tokenizer.h"
#include "ast.h"

typedef enum {
    PCLF_DECL,
    PCLF_ASSIGN,
    PCLF_BLOCK,
    PCLF_WHILE,
    PCLF_IF,
    PCLF_RET,
    PCLF_EXPR,
} ParserClsfcn; 

typedef struct {
    TokenArray tokens;
    size_t current;
    HashTable *type_intern_table;
    Arena *arena;
} ParserState;

typedef AST* (*ParseFn)(ParserState *parser);

QuectoType *parse_type(ParserState *parser);

AST *parse_program(ParserState *parser); 
AST *parse_procedure(ParserState *parser);
AST *parse_extern(ParserState *parser);

AST *parse_statement(ParserState *parser);
AST *parse_block(ParserState *parser); 
AST *parse_decl(ParserState *parser);
AST *parse_rhs(ParserState *parser);
AST *parse_if(ParserState *parser); 
AST *parse_while(ParserState *parser); 
AST *parse_assignment(ParserState *parser);
AST *parse_return(ParserState *parser); 

ASTList parse_delimited(ParserState *parser, ParseFn callback, TokenType delimiter, TokenType end);

AST *parse_expr(ParserState *parser);
AST *parse_binary_op(ParserState *parser, AST *left);
AST *parse_call(ParserState *parser, AST *ident);
AST *parse_index(ParserState *parser, AST *head);
AST *parse_access(ParserState *parser, AST *on);
AST *parse_reference(ParserState *parser);

AST *parse_identifier(ParserState *parser);
AST *parse_int_lit(ParserState *parser);
AST *parse_float_lit(ParserState *parser);
AST *parse_str_lit(ParserState *parser);


#endif
