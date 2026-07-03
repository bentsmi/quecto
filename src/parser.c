#include <stdio.h>

#include "parser.h"
#include "ast.h"
#include "common.h"
#include "symbol_table.h"
#include "tokenizer.h"
#include "error.h"


TokenType peek_token_type(ParserState *parser, size_t dist) { // we never peek backwards
    return (parser->current + dist < parser->tokens.count) ? parser->tokens.items[parser->current + dist].type :
                 parser->tokens.items[parser->tokens.count - 1].type;
}


Token consume_token(ParserState *parser) {
    return (parser->current < parser->tokens.count) ?
            parser->tokens.items[parser->current++] :
            parser->tokens.items[parser->tokens.count - 1];
}


Token expect_token(ParserState *parser, TokenType type) {
    if (peek_token_type(parser, 0) != type) {
        Token tok = consume_token(parser);
        report_error(tok.line, tok.col, "expected \'%s\' but got \'"sv_fmt"\' instead\n",
                token_info_table[type].name,
                sv_arg(tok.lexeme)
        );
        return tok;
    }

    return consume_token(parser);
}

// PROGRAM and TOP-LEVEL DECLS

AST *parse_program(ParserState *parser) {
    ASTProgram *program = arena_alloc_type(parser->arena, ASTProgram);
    program->base.type = AST_PROGRAM;

    while (peek_token_type(parser, 0) != TOKEN_EOF) {
        AST *statement = NULL;
        switch(peek_token_type(parser, 0)) {
            case TOKEN_EXTERN: statement = parse_extern(parser); break;
            case TOKEN_PROC:   statement = parse_procedure(parser); break;
            default: {
                Token tok = consume_token(parser);
                report_error(tok.line, tok.col, "Expected top-level declaration got %s", token_info_table[tok.type].name);
                return NULL;
            }
        }
        arena_array_append(parser->arena, program->decls, statement);
    }

    return &program->base;
}


AST *parse_extern(ParserState *parser) {
    ASTExtern *exter = arena_alloc_type(parser->arena, ASTExtern);
    exter->base.type = AST_EXTERN;
    exter->base.token = expect_token(parser, TOKEN_EXTERN);
    exter->decl = parse_procedure(parser);
    return &exter->base;    
}


AST *parse_procedure(ParserState *parser) {
    ASTProc *procedure = arena_alloc_type(parser->arena, ASTProc);
    procedure->base.type = AST_PROCEDURE;
    procedure->base.token = expect_token(parser, TOKEN_PROC);
    procedure->name = parse_identifier(parser);

    expect_token(parser, TOKEN_OPEN_PAREN);
    procedure->params = parse_delimited(parser, parse_decl, TOKEN_COMMA, TOKEN_CLOSE_PAREN);
    expect_token(parser, TOKEN_ARROW);
    expect_token(parser, TOKEN_OPEN_PAREN);
    procedure->rets = parse_delimited(parser, parse_decl, TOKEN_COMMA, TOKEN_CLOSE_PAREN);

    assert(procedure->rets.count <= 1 && "no support for multiple return values yet");

    if (peek_token_type(parser, 0) == TOKEN_OPEN_CURLY)
        procedure->body = parse_block(parser);
    else
        procedure->body = NULL;

    return &procedure->base;
}

// STATEMENTS

AST *parse_statement(ParserState *parser) {
    AST *statement = NULL;

    switch(peek_token_type(parser, 0)) {
        case TOKEN_OPEN_CURLY: return parse_block(parser);
        case TOKEN_IF:         return parse_if(parser);
        case TOKEN_WHILE:      return parse_while(parser);
        case TOKEN_RETURN:     statement = parse_return(parser); break;
        case TOKEN_IDENTIFIER: {
            if (peek_token_type(parser, 1) == TOKEN_COLON) {
                statement = parse_decl(parser);
                break;
            }
        }
        default: {
            AST *expr = parse_expr(parser);
            statement = expr;
            if (peek_token_type(parser, 0) == TOKEN_EQUALS) {
                ASTAssign *assign = arena_alloc_type(parser->arena, ASTAssign);
                assign->base.type = AST_ASSIGNMENT;
                assign->base.token = expect_token(parser, TOKEN_EQUALS);
                assign->lhs = expr;
                assign->rhs = parse_rhs(parser);
                statement = &assign->base;
            }
            break;
        }
    }

    expect_token(parser, TOKEN_SEMICOLON);

    return statement;
}


AST *parse_block(ParserState *parser) {
    ASTBlock *block = arena_alloc_type(parser->arena, ASTBlock);
    block->base.type = AST_BLOCK;
    block->base.token = expect_token(parser, TOKEN_OPEN_CURLY);
    block->stmts = (ASTList){ 0 };

    while (peek_token_type(parser, 0) != TOKEN_CLOSE_CURLY) {
        AST *stmt = parse_statement(parser);
        arena_array_append(parser->arena, block->stmts, stmt);
    }
    expect_token(parser, TOKEN_CLOSE_CURLY);

    return &block->base;
}


AST *parse_decl(ParserState *parser) {
    ASTDecl *decl = arena_alloc_type(parser->arena, ASTDecl);
    decl->base.type = AST_DECL;

    decl->lhs = parse_identifier(parser);
    
    expect_token(parser, TOKEN_COLON);
    if (peek_token_type(parser, 0) != TOKEN_EQUALS) {
        decl->base.resolved_qtype = parse_type(parser);
        if (peek_token_type(parser, 0) != TOKEN_EQUALS)
            return &decl->base;
    }
    expect_token(parser, TOKEN_EQUALS);
    
    decl->rhs = parse_rhs(parser);

    return &decl->base;
}


QuectoType *parse_type(ParserState *parser) {
    QuectoType qtype = { 0 };

    Token tok = consume_token(parser);
    switch(tok.type) {
        case TOKEN_I32:     qtype.type = QUECTO_I32; break;
        case TOKEN_U32:     qtype.type = QUECTO_U32; break;
        case TOKEN_I8:      qtype.type = QUECTO_I8; break;
        case TOKEN_U8:      qtype.type = QUECTO_U8; break;
        case TOKEN_OPEN_SQUARE:
            qtype.type = QUECTO_ARRAY;
            
            qtype.inner = parse_type(parser);
            expect_token(parser, TOKEN_SEMICOLON);
            tok = expect_token(parser, TOKEN_INT_LIT);
            
            qtype.array_size = tok.int_lit;

            expect_token(parser, TOKEN_CLOSE_SQUARE);
            break;
        case TOKEN_CARET:
            qtype.type = QUECTO_POINTER;
            qtype.inner = parse_type(parser);
            break;                
        default: {
            parser->current--;
            qtype.type = QUECTO_UNKNOWN;
        }
    }
    return arena_intern(parser->arena, parser->type_intern_table, &qtype, sizeof(QuectoType));
}


AST *parse_if_chain(ParserState *parser) {
    if (peek_token_type(parser, 0) == TOKEN_ELIF) {
        ASTIf *statement = arena_alloc_type(parser->arena, ASTIf);
        statement->base.type = AST_ELIF;
        statement->base.token = expect_token(parser, TOKEN_ELIF);
        
        statement->cond = parse_expr(parser);
        statement->then = parse_statement(parser);
        statement->otherwise = parse_if_chain(parser);

        return &statement->base;
    } else if (peek_token_type(parser, 0) == TOKEN_ELSE) {
        ASTIf *statement = arena_alloc_type(parser->arena, ASTIf);
        statement->base.type = AST_ELSE;
        statement->base.token = expect_token(parser, TOKEN_ELSE);
        
        statement->cond = NULL;
        statement->then = parse_statement(parser);
        statement->otherwise = NULL;

        return &statement->base;
    }

    return NULL;
}


AST *parse_if(ParserState *parser) {
    ASTIf *_if = arena_alloc_type(parser->arena, ASTIf);
    _if->base.type = AST_IF;
    _if->base.token = expect_token(parser, TOKEN_IF);

    _if->cond = parse_expr(parser);
    _if->then = parse_statement(parser);
    _if->otherwise = parse_if_chain(parser);

    return &_if->base;
}


AST *parse_while(ParserState *parser) {
    ASTWhile *_while = arena_alloc_type(parser->arena, ASTWhile);
    _while->base.type = AST_WHILE;
    _while->base.token = expect_token(parser, TOKEN_WHILE);

    _while->cond = parse_expr(parser);
    _while->then = parse_statement(parser);

    return &_while->base;
}


AST *parse_return(ParserState *parser) {
    ASTReturn *_return = arena_alloc_type(parser->arena, ASTReturn);
    _return->base.type = AST_RETURN;
    _return->base.token = expect_token(parser, TOKEN_RETURN);

    if (peek_token_type(parser, 0) != TOKEN_SEMICOLON)
        _return->expr = parse_expr(parser);
    else
        _return->expr = NULL;

    return &_return->base;
}




ASTList parse_delimited(ParserState *parser, ParseFn callback, TokenType delimiter, TokenType end) {
    ASTList list = { 0 };
    
    while (peek_token_type(parser, 0) != end) {
        AST *ast = callback(parser);
        if (ast != NULL) arena_array_append(parser->arena, list, ast);
        if (peek_token_type(parser, 0) != end)
            expect_token(parser, delimiter);
    }

    expect_token(parser, end);
    return list;
}

// EXPRESSIONS

AST *parse_expr_helper(ParserState *parser, int min_prec) {  
    AST *left = NULL;

    // NOTE: utilizes the fact that get precedence returns -1 when the next token is
    // not an operator or apart of an expression
    while (token_precedence(peek_token_type(parser, 0)) > min_prec) {
        switch (peek_token_type(parser, 0)) {
            case TOKEN_OPEN_SQUARE: left = parse_index(parser, left); break;
            case TOKEN_OPEN_PAREN:
                if (left == NULL) {
                    expect_token(parser, TOKEN_OPEN_PAREN);
                    left = parse_expr_helper(parser, 0);
                    expect_token(parser, TOKEN_CLOSE_PAREN);
                } else left = parse_call(parser, left);
                break;
            case TOKEN_PERIOD:      left = parse_access(parser, left); break;
            case TOKEN_AMPERSAND:   left = parse_reference(parser); break;
            case TOKEN_INT_LIT:     left = parse_int_lit(parser); break;
            case TOKEN_FLOAT_LIT:   left = parse_float_lit(parser); break;
            case TOKEN_STR_LIT:     left = parse_str_lit(parser); break;
            case TOKEN_IDENTIFIER:  left = parse_identifier(parser); break;
            default:                left = parse_binary_op(parser, left); break;
        }
    }

    // if (left == NULL)
    //     report_error(parser->tokens.items[parser->current - 1].line, parser->tokens.items[parser->current - 1].col,
    //                  "%s %s",
    //                 token_info_table[parser->tokens.items[parser->current - 2].type].name, token_info_table[parser->tokens.items[parser->current].type].name               );

    return left;
}

AST *parse_list_init(ParserState *parser) {
    ASTListConstruct *list = arena_alloc_type(parser->arena, ASTListConstruct);
    list->base.type = AST_LIST;
    list->base.token = expect_token(parser, TOKEN_OPEN_CURLY);

    list->items = parse_delimited(parser, parse_expr, TOKEN_COMMA, TOKEN_CLOSE_CURLY);

    return &list->base;
}

AST *parse_expr(ParserState *parser) {
    return parse_expr_helper(parser, 0);
}

AST *parse_rhs(ParserState *parser) {
    switch(peek_token_type(parser, 0)) {
        case TOKEN_OPEN_CURLY:
            return parse_list_init(parser);
        default:
            return parse_expr(parser);
    }
}


AST *parse_call(ParserState *parser, AST *ident) {
    ASTCall *call = arena_alloc_type(parser->arena, ASTCall);
    call->base.type = AST_CALL;
    call->base.token = expect_token(parser, TOKEN_OPEN_PAREN);
    
    call->ident = ident;
    call->args = parse_delimited(parser, parse_expr, TOKEN_COMMA, TOKEN_CLOSE_PAREN);
    
    return &call->base;
}


AST *parse_index(ParserState *parser, AST *head) {
    ASTIndex *index = arena_alloc_type(parser->arena, ASTIndex);
    index->base.type = AST_INDEX;
    index->base.token = expect_token(parser, TOKEN_OPEN_SQUARE);

    index->head = head;
    index->index = parse_expr(parser);

    expect_token(parser, TOKEN_CLOSE_SQUARE);
    return &index->base;
}


AST *parse_access(ParserState *parser, AST *on) {
    ASTAccess *access = arena_alloc_type(parser->arena, ASTAccess);
    access->base.type = AST_ACCESS;
    access->base.token = expect_token(parser, TOKEN_PERIOD);
    
    access->head = on;
    access->spec = parse_identifier(parser);
    
    return &access->base;
}


AST *parse_reference(ParserState *parser) {
    ASTRef *ref = arena_alloc_type(parser->arena, ASTRef);
    ref->base.type = AST_REF;
    ref->base.token = expect_token(parser, TOKEN_AMPERSAND);
    ref->head = parse_expr(parser); 
    return &ref->base;
}


AST *parse_binary_op(ParserState *parser, AST *left) {
    ASTBinaryOp *op = arena_alloc_type(parser->arena, ASTBinaryOp);
    op->base.type = AST_BINARY_OP;
    op->left = left;
    
    op->base.token = consume_token(parser);
    switch (op->base.token.type) {
        case TOKEN_EQUALS_EQUALS:   op->op = OP_EQUALS; break;
        case TOKEN_LESS_THAN:       op->op = OP_LESS_THAN; break;
        case TOKEN_GREATER_THAN:    op->op = OP_GREATER_THAN; break;
        case TOKEN_LESS_EQUALS:     op->op = OP_LESS_EQUALS; break;
        case TOKEN_GREATER_EQUALS:  op->op = OP_GREATER_EQUALS; break;
        case TOKEN_PLUS:            op->op = OP_PLUS;     break;
        case TOKEN_MINUS:           op->op = OP_MINUS;    break;
        case TOKEN_MULTIPLY:        op->op = OP_MULTIPLY; break;
        case TOKEN_DIVIDE:          op->op = OP_DIVIDE;   break;
        default:                    UNREACHABLE("TokenType");
    }

    op->right = parse_expr_helper(parser, token_precedence(op->base.token.type));
    return &op->base;
}

// EXPRESSION ELEMENTS

AST *parse_identifier(ParserState *parser) {
    ASTIdentifier *identifier = arena_alloc_type(parser->arena, ASTIdentifier);
    Token tok = expect_token(parser, TOKEN_IDENTIFIER);

    identifier->base.type = AST_IDENTIFIER;
    identifier->base.token = tok;
    identifier->name = tok.identifier;

    return &identifier->base;
}

AST *parse_int_lit(ParserState *parser) {
    ASTIntLit *literal = arena_alloc_type(parser->arena, ASTIntLit);
    literal->base.type = AST_INT_LIT;
    literal->base.token = expect_token(parser, TOKEN_INT_LIT);
    literal->literal = literal->base.token.int_lit;
    return &literal->base;
}


AST *parse_float_lit(ParserState *parser) {
    ASTFloatLit *literal = arena_alloc_type(parser->arena, ASTFloatLit);
    literal->base.type = AST_FLOAT_LIT;
    literal->base.token = expect_token(parser, TOKEN_FLOAT_LIT);
    literal->literal = literal->base.token.float_lit;
    return &literal->base;
}


AST *parse_str_lit(ParserState *parser) {    
    ASTStringLit *literal = arena_alloc_type(parser->arena, ASTStringLit);
    literal->base.type = AST_STR_LIT;
    literal->base.token = expect_token(parser, TOKEN_STR_LIT);
    literal->literal = literal->base.token.string_lit;
    return &literal->base;
}
