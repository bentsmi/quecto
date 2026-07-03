#include "analysis.h"
#include "ast.h"
#include "common.h"
#include "error.h"
#include "symbol_table.h"

QuectoType *resolve_binary_type(QuectoType *left, QuectoType *right) {
    if (quecto_types_equal(left, right)) return left;
    if (quecto_is_integer(left) && quecto_is_integer(right)) {
        return quecto_primitive_width[left->type] > quecto_primitive_width[right->type] ? left : right;
    }
    return right;
}

QuectoType *pointer_of_type(AnalysisContext *context, QuectoType *type) {
    QuectoType pointer = { 0 };
    pointer.type = QUECTO_POINTER;
    pointer.inner = type;
    return arena_intern(context->arena, context->type_intern_table, &pointer, sizeof(QuectoType));
}


SymbolData *lookup_or_error(AnalysisContext *context, AST *base, const char *message) {
    ASTIdentifier *identifier = CAST_AST(base, ASTIdentifier, AST_IDENTIFIER);
    SymbolData *var = get_symbol(context->symbol_table, identifier->name);
    return var ? var : (report_error(identifier->base.token.line, identifier->base.token.col, message), NULL);
}


void analyze_ast(AnalysisContext *context, AST *base) { // assumes ast is a AST_PROGRAM
    ASTProgram *program = CAST_AST(base, ASTProgram, AST_PROGRAM);
    
    for (size_t i = 0; i < program->decls.count; i++) {
        switch(program->decls.items[i]->type) {
            case AST_PROCEDURE: analyze_procedure(context, program->decls.items[i], false); break;
            case AST_EXTERN:    analyze_extern(context, program->decls.items[i]); break;
            default: UNREACHABLE("not top-level decl"); break;
        }
    }
}


 void analyze_procedure(AnalysisContext *context, AST *base, bool externed) {
    ASTProc *procedure = CAST_AST(base, ASTProc, AST_PROCEDURE);
    ASTIdentifier *identifier = CAST_AST(procedure->name, ASTIdentifier, AST_IDENTIFIER);

    SymbolData *symbol = insert_symbol(context->arena, context->symbol_table, identifier->name);

    ProcSignature signature = { 0 };
    QuectoType qtype = { 0 };
    qtype.type = QUECTO_PROCEDURE;

    if (procedure->body != NULL) {
        ASTBlock *block = CAST_AST(procedure->body, ASTBlock, AST_BLOCK);
        block->symbols = arena_alloc_type(context->arena, SymbolTable);
    }

    signature.param_count = procedure->params.count;
    for (size_t i = 0; i < procedure->params.count; i++) {
        ASTDecl *decl = CAST_AST(procedure->params.items[i], ASTDecl, AST_DECL);
        ASTIdentifier *ident = CAST_AST(decl->lhs, ASTIdentifier, AST_IDENTIFIER);
        
        signature.param_types[i] = decl->base.resolved_qtype;
        if (procedure->body != NULL) {
            SymbolData *sym = insert_symbol(context->arena, context->symbol_table, ident->name);
            sym->qtype = decl->base.resolved_qtype;
        }        
    }

    signature.return_count = procedure->rets.count;
    for (size_t i = 0; i < procedure->rets.count; i++) {
        ASTDecl *decl = CAST_AST(procedure->rets.items[i], ASTDecl, AST_DECL);
        ASTIdentifier *ident = CAST_AST(decl->lhs, ASTIdentifier, AST_IDENTIFIER);
        
        signature.return_types[i] = decl->base.resolved_qtype;
        if (procedure->body != NULL) {
            SymbolData *sym = insert_symbol(context->arena, context->symbol_table, ident->name);
            sym->qtype = decl->base.resolved_qtype;
        }        
    }

    qtype.signature = arena_intern(context->arena, context->type_intern_table, &signature, sizeof(signature));
    symbol->qtype = arena_intern(context->arena, context->type_intern_table, &qtype, sizeof(QuectoType));
    symbol->externed = externed;
    
    context->current_procedure = symbol->qtype->signature;

    if (procedure->body != NULL)
        analyze_block(context, procedure->body);
}

void analyze_extern(AnalysisContext *context, AST *base) {
    ASTExtern *exter = CAST_AST(base, ASTExtern, AST_EXTERN);
    analyze_procedure(context, exter->decl, true);
}


void analyze_block(AnalysisContext *context, AST *base) {
    ASTBlock *block = CAST_AST(base, ASTBlock, AST_BLOCK);

    if (block->symbols == NULL)
        block->symbols = arena_alloc_type(context->arena, SymbolTable);
    block->symbols->prev = context->symbol_table;
    context->symbol_table = block->symbols;

    for (size_t i = 0; i < block->stmts.count; i++) {
        analyze_statement(context, block->stmts.items[i]);
    }

    context->symbol_table = block->symbols->prev;
}


void analyze_statement(AnalysisContext *context, AST *statement) {
    if (statement == NULL) return;

    switch (statement->type) {
        case AST_ASSIGNMENT: analyze_assignment(context, statement); break;
        case AST_DECL: analyze_declaration(context, statement); break;
        case AST_BLOCK: analyze_block(context, statement); break;
        case AST_CALL: analyze_expression(context, statement, NULL); break;
        case AST_WHILE: analyze_while(context, statement); break;
        case AST_IF: analyze_if(context, statement); break;
        case AST_ELIF: analyze_elif(context, statement); break;
        case AST_ELSE: analyze_else(context, statement); break;
        case AST_RETURN: analyze_return(context, statement); break;
        default:
            UNREACHABLE("AST NOT STATEMENT");
            break;
    }
}


void analyze_declaration(AnalysisContext *context, AST *base) {
    ASTDecl *decl = CAST_AST(base, ASTDecl, AST_DECL);
    ASTIdentifier *ident = CAST_AST(decl->lhs, ASTIdentifier, AST_IDENTIFIER);

    SymbolData *symbol = insert_symbol(context->arena, context->symbol_table, ident->name);
    
    if (decl->rhs != NULL) {
        QuectoType *is = analyze_expression(context, decl->rhs, decl->base.resolved_qtype);
        if (decl->base.resolved_qtype == NULL)
            decl->base.resolved_qtype = (is->type == QUECTO_COMP_INT) ? &default_integer_type : is;
        if (!quecto_types_equal(decl->base.resolved_qtype, is)) {
        //report_error(decl->line, decl->col, "declared as "); // todo: create string/char buffer builder to ease printing for multiple destinations (i.e file output, errors, regular)
            printf("declared as "); print_type(decl->base.resolved_qtype); printf(" but assigned "); print_type(is); printf(".\n");
        }
    } else {
        if (decl->base.resolved_qtype == NULL) {
            report_error(base->token.line, base->token.col, "cannot infer type without expr");
        }
    }

    symbol->qtype = decl->base.resolved_qtype;
}


void analyze_if(AnalysisContext *context, AST *base) {
    ASTIf *_if = CAST_AST(base, ASTIf, AST_IF);

    analyze_expression(context, _if->cond, NULL);
    analyze_statement(context, _if->then);
    if (_if->otherwise) analyze_statement(context, _if->otherwise);
}


void analyze_while(AnalysisContext *context, AST *base) {
    ASTWhile *_if = CAST_AST(base, ASTWhile, AST_WHILE);

    analyze_expression(context, _if->cond, NULL);
    analyze_statement(context, _if->then);
}


void analyze_elif(AnalysisContext *context, AST *base) {
    ASTIf *_if = CAST_AST(base, ASTIf, AST_ELIF);

    analyze_expression(context, _if->cond, NULL);
    analyze_statement(context, _if->then);
    if (_if->otherwise) analyze_statement(context, _if->otherwise);
}


void analyze_else(AnalysisContext *context, AST *base) {
    ASTIf *_if = CAST_AST(base, ASTIf, AST_ELSE);
    analyze_statement(context, _if->then);
}


void analyze_return(AnalysisContext *context, AST *base) {
    ASTReturn *ret = CAST_AST(base, ASTReturn, AST_RETURN);
    if (ret->expr != NULL) analyze_expression(context, ret->expr, NULL);
}


void analyze_assignment(AnalysisContext *context, AST *base) {
    ASTAssign *assign = CAST_AST(base, ASTAssign, AST_ASSIGNMENT);

    QuectoType *expected = analyze_lhs(context, assign->lhs);
    analyze_expression(context, assign->rhs, expected);
}


QuectoType *analyze_lhs(AnalysisContext *context, AST *base) {
    switch (base->type) {
        case AST_IDENTIFIER: {
            SymbolData *symbol = lookup_or_error(context, base, "undefined variable");
            return symbol->qtype;
        }
        case AST_INDEX: {
            return analyze_index(context, base);
        }
        default:
            report_error(base->token.line, base->token.col, "invalid lhs of assignment");
            return NULL;
    }
}


QuectoType *analyze_expression(AnalysisContext *context, AST *expr, QuectoType *expected) {
    switch(expr->type) {
        case AST_CALL: return analyze_call(context, expr);
        case AST_INT_LIT: return expr->resolved_qtype = (expected == NULL || expected->type == QUECTO_UNKNOWN) ? &default_integer_type : expected;
        case AST_STR_LIT: return analyze_string_literal(context, expr);
        case AST_IDENTIFIER: return analyze_symbol(context, expr);
        case AST_BINARY_OP: return analyze_binary_op(context, expr, expected);
        case AST_INDEX: return analyze_index(context, expr);
        case AST_LIST: return analyze_list(context, expr, expected);
        case AST_REF: return analyze_ref(context, expr);
        default: UNREACHABLE("AST NOT EXPR");
    }
    return NULL;
}


QuectoType *analyze_call(AnalysisContext *context, AST *base) {
    ASTCall *call = CAST_AST(base, ASTCall, AST_CALL);

    SymbolData *symbol = lookup_or_error(context, call->ident, "cannot call an undefined procedure");

    if (!quecto_is_procedure(symbol->qtype)) {
        report_error(call->base.token.line, call->base.token.col, "Cannot call a non-procedure");
        return NULL;
    }

    ProcSignature *call_signature = symbol->qtype->signature;

    if (call_signature->param_count != call->args.count) {
        report_error(call->base.token.line, call->base.token.col, "Call does not match procedure signature");
        return NULL;
    }

    for (size_t i = 0; i < call->args.count; i++) {
        analyze_expression(context, call->args.items[i], call_signature->param_types[i]);
    }

    return call->base.resolved_qtype = call_signature->return_types[0];
}


QuectoType *analyze_index(AnalysisContext *context, AST *base) {
    ASTIndex *index = CAST_AST(base, ASTIndex, AST_INDEX);

    index->head->resolved_qtype= analyze_expression(context, index->head, NULL);
    index->index->resolved_qtype = analyze_expression(context, index->index, NULL);

    // if (!quecto_is_array(index->head->resolved_qtype)) {
        // report_error(index->base.token.line, index->base.token.col, "cannot index a non array");
        // return NULL;
    // }
    //

    return index->base.resolved_qtype = index->head->resolved_qtype->inner;
}


QuectoType *analyze_ref(AnalysisContext *context, AST *base) {
    ASTRef *ref = CAST_AST(base, ASTRef, AST_REF);
    // todo: verify that the reference is valid i.e not a constant integer etc
    QuectoType *inner = analyze_expression(context, ref->head, NULL);
    return base->resolved_qtype = pointer_of_type(context, inner);
}


QuectoType *analyze_binary_op(AnalysisContext *context, AST *base, QuectoType *expected) {
    ASTBinaryOp *op = CAST_AST(base, ASTBinaryOp, AST_BINARY_OP);

    QuectoType *left = analyze_expression(context, op->left, expected);
    QuectoType *right = analyze_expression(context, op->right, expected);

    bool left_valid = left != NULL && left->type != QUECTO_COMP_INT;
    bool right_valid = right != NULL && right->type != QUECTO_COMP_INT;

    if (!left_valid && right_valid) left = analyze_expression(context, op->left, right);
    if (!right_valid && left_valid) right = analyze_expression(context, op->right, left);

    return op->base.resolved_qtype = resolve_binary_type(left, right);
}


QuectoType *analyze_string_literal(AnalysisContext *context, AST *lit) {
    return lit->resolved_qtype = pointer_of_type(context, &default_char_type);
}


QuectoType *analyze_list(AnalysisContext *context, AST *base, QuectoType *expected) {
    ASTListConstruct *list = CAST_AST(base, ASTListConstruct, AST_LIST);
    
    if (expected == NULL || expected->type == QUECTO_UNKNOWN) {
        report_error(list->base.token.line, list->base.token.col, "list must be specified with type.");
        return NULL;
    }
    
    for (size_t i = 0; i < list->items.count; i++) {
        analyze_expression(context, list->items.items[i], expected->inner);
    }
        
    return list->base.resolved_qtype = expected;
}


QuectoType *analyze_symbol(AnalysisContext *context, AST *symbol) {
    SymbolData *var = lookup_or_error(context, symbol, "undefined variable");
    return var ? (symbol->resolved_qtype = var->qtype) : NULL;
}


QuectoType *analyze_access(AnalysisContext *context, AST *access) {
    (void)context; // TODO: add analysis for access on an expression
    (void)access;
    return NULL;
}


