#include <stdio.h>

#include "ast.h"
#include "common.h"
#include "error.h"
#include "symbol_table.h"


#define STR_CASE(unwrap, into, check, body) case check: { into *unwrap = CAST_AST(ast, into, check); body }


void ast_print(Arena *arena, AST *ast) {
    int mark = arena_mark(arena);
    String out = ast_str(arena, ast);

    printf("%s\n", out.data);
    arena_restore(arena, mark);
}


#define arena_sprintf_indent(fmt, ...) arena_sprintf(arena, "%.*s"fmt, indent, tabs, __VA_ARGS__)
String ast_str_helper(Arena *arena, AST *ast, int indent) {
    if (ast == NULL) return (String) { .len = 0, .data= ""};
    switch (ast->type) {
        STR_CASE(program, ASTProgram, AST_PROGRAM, {
            return astlist_str(arena, program->decls, "\n\n", indent);
        })
        STR_CASE(proc, ASTProc, AST_PROCEDURE, {
            char *name = ast_str(arena, proc->name).data;
            String params = astlist_str(arena, proc->params, ", ", 0);
            String rets = astlist_str(arena, proc->rets, ", ", 0);
            char *block = ast_str(arena, proc->body).data;
            return arena_sprintf_indent("proc %s (%.*s) => (%.*s) %s ", name, params.len, params.data, rets.len, rets.data, block);
        })
        STR_CASE(exter, ASTExtern, AST_EXTERN, {
            char *decl = ast_str(arena, exter->decl).data;
            return arena_sprintf_indent("extern %s", decl);
        })
        STR_CASE(block, ASTBlock, AST_BLOCK, {
            char *stmts = astlist_str(arena, block->stmts, "\n", indent + 1).data;
            return arena_sprintf(arena, "{\n %s \n%.*s}", stmts, indent, tabs);
        })
        STR_CASE(decl, ASTDecl, AST_DECL, {
            char *lhs = ast_str(arena, decl->lhs).data;  
            char *rhs = ast_str(arena, decl->rhs).data;
            
            return (decl->rhs == NULL) ? arena_sprintf_indent("%s: type", lhs) : arena_sprintf_indent("%s := %s", lhs, rhs);
        })
        STR_CASE(assign, ASTAssign, AST_ASSIGNMENT, {
            char *lhs = ast_str(arena, assign->lhs).data;
            char *rhs = ast_str(arena, assign->rhs).data;
            return arena_sprintf_indent("%s = %s", lhs, rhs);
        })
        STR_CASE(_if, ASTIf, AST_IF, {
            char *cond = ast_str(arena, _if->cond).data;  
            char *then = ast_str_helper(arena, _if->then, indent).data;
            char *otherwise = ast_str_helper(arena, _if->otherwise, indent).data;
            return arena_sprintf_indent("if (%s) %s %s", cond, then, otherwise);
        })
        STR_CASE(_if, ASTIf, AST_ELIF, {
            char *cond = ast_str(arena, _if->cond).data;  
            char *then = ast_str_helper(arena, _if->then, indent).data;
            char *otherwise = ast_str_helper(arena, _if->otherwise, indent).data;
            return arena_sprintf(arena, "elif (%s) %s %s", cond, then, otherwise);
        })
        STR_CASE(_if, ASTIf, AST_ELSE, {
            char *then = ast_str_helper(arena, _if->then, indent).data;
            return arena_sprintf(arena, "else %s", then);
        })
        STR_CASE(_if, ASTWhile, AST_WHILE, {
            char *cond = ast_str(arena, _if->cond).data;  
            char *then = ast_str_helper(arena, _if->then, indent).data;
            return arena_sprintf_indent("while (%s) %s", cond, then);
        })
        STR_CASE(_if, ASTReturn, AST_RETURN, {
            char *expr = ast_str(arena, _if->expr).data;
            return arena_sprintf_indent("return %s", expr);
        })
        STR_CASE(_if, ASTCall, AST_CALL, {
            char *ident = ast_str(arena, _if->ident).data;
            char *args = astlist_str(arena, _if->args, ",", 0).data;
            return arena_sprintf_indent("%s(%s)", ident, args);
        })
        STR_CASE(_if, ASTIndex, AST_INDEX, {
            char *head = ast_str(arena, _if->head).data;
            char *index = ast_str(arena, _if->index).data;
            return arena_sprintf_indent("%s[%s]", head, index);
        })
        STR_CASE(_if, ASTAccess, AST_ACCESS, {
            char *head = ast_str(arena, _if->head).data;
            char *spec = ast_str(arena, _if->spec).data;
            return arena_sprintf_indent("%s.%s", head, spec);
        })
        STR_CASE(ref, ASTRef, AST_REF, {
            char *head = ast_str(arena, ref->head).data;
            return arena_sprintf_indent("&%s", head);
        })
        STR_CASE(bin, ASTBinaryOp, AST_BINARY_OP, {
            char *left = ast_str(arena, bin->left).data;
            const char *op = "(?)";
            switch (bin->op) {
                case OP_LESS_THAN:      op = "<"; break;
                case OP_EQUALS:         op = "=="; break;
                case OP_GREATER_THAN:   op = ">"; break;
                case OP_NEQUALS:        op = "!="; break;
                case OP_LESS_EQUALS:    op = "<="; break;
                case OP_GREATER_EQUALS: op = ">="; break;
                case OP_PLUS:           op = "+"; break;
                case OP_MINUS:          op = "-"; break;
                case OP_DIVIDE:         op = "/"; break;
                case OP_MULTIPLY:       op = "*"; break;
                default: UNREACHABLE("invalid op");
            }
            char *right = ast_str(arena, bin->right).data;
            return arena_sprintf_indent("%s %s %s", left, op, right);
        })
        STR_CASE(list, ASTListConstruct, AST_LIST, {
            char *items = astlist_str(arena, list->items, ", ", 0).data;
            return arena_sprintf_indent("{%s}", items);
        })
        STR_CASE(ident, ASTIdentifier, AST_IDENTIFIER, {
            return arena_sprintf_indent("%s", ident->name);
        })
        STR_CASE(_int, ASTIntLit, AST_INT_LIT, {
            return arena_sprintf_indent("%d", _int->literal);
        })
        STR_CASE(_float, ASTFloatLit, AST_FLOAT_LIT, {
            return arena_sprintf_indent("%d", _float->literal);
        })
        STR_CASE(_str, ASTStringLit, AST_STR_LIT, {
            return arena_sprintf_indent("%s", _str->literal);
        })
        default:
            UNREACHABLE("invalid ast");
            break;
    }
}

String ast_str(Arena *arena, AST *root) {
    return ast_str_helper(arena, root, 0);
}

String astlist_str(Arena *arena, ASTList list, const char *delim, int indent) {
    if (list.count == 0) return (String) { 0 };

    int dlen = (int)(strlen(delim));

    String *parts = arena_alloc(arena, list.count * sizeof *parts);
    int total = 0;
    for (size_t i = 0; i < list.count; i++) {
        parts[i] = ast_str_helper(arena, list.items[i], indent);
        total += parts[i].len;
    }
    
    total += dlen * (list.count - 1);
    char *buf = arena_alloc(arena, total + 1);
    int offset = 0;
    for (size_t i = 0; i < list.count; i++) {
        if (i > 0) {
            memcpy(buf + offset, delim, dlen);
            offset += dlen;
        }
        memcpy(buf + offset, parts[i].data, parts[i].len);
        offset += parts[i].len;
    }
    buf[total] = '\0';
    return (String) { .len = total, .data = buf};
}


// bool op_is_arithmetic(BinaryOp op) {
//     switch (op) {
//         case OP_DIVIDE:
//         case OP_PLUS:
//         case OP_MINUS:
//         case OP_MULTIPLY:
//             return true;
//         default:
//             return false;
//     }
// }


// bool op_is_conditional(BinaryOp op) {
//     switch (op) {
//         case OP_EQUALS:
//         case OP_LESS_EQUALS:
//         case OP_GREATER_THAN:
//         case OP_GREATER_EQUALS:
//         case OP_LESS_THAN:
//             return true;
//         default:
//             return false;
//     }
// }


// BinaryOp op_opposite(BinaryOp op) {
//     switch(op) {
//         case OP_EQUALS: return OP_NEQUALS;
//         case OP_NEQUALS: return OP_EQUALS;
//         case OP_LESS_THAN: return OP_GREATER_EQUALS;
//         case OP_GREATER_THAN: return OP_LESS_EQUALS;
//         case OP_LESS_EQUALS: return OP_GREATER_THAN;
//         case OP_GREATER_EQUALS: return OP_LESS_THAN;
//         default:
//             UNREACHABLE("invalid binary op");
//     }
// }
