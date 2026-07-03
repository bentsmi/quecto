#include "emission.h"
#include "ast.h"
#include "cfg.h"
#include "common.h"
#include "ir.h"
#include "symbol_table.h"

#define NONE (Operand) { 0 }
#define VREG(v) (Operand) { .type = OPERAND_VREG, .vreg = (v) }
#define IMM(i) (Operand) { .type = OPERAND_IMM, .imm = (i) }
#define MEM(m) (Operand) { .type = OPERAND_MEM, .mem = (m) }
#define INDEX(b, i, o, s) (MemRef) { .base = (b), .index = (i), .offset = (o), .stride = (s), .size = 0 }
#define GLOBAL(g) (Operand) { .type = OPERAND_GLOBAL, .glbl = (g) }
#define SLOT(s) (Operand) { .type = OPERAND_SLOT, .slot = (s) }


const Opcode jump_condition_opcode_table[OP_COUNT] = {
    [OP_EQUALS] = OPCODE_BEQ,
    [OP_NEQUALS] = OPCODE_BNE,
    [OP_LESS_THAN] = OPCODE_BLT,
    [OP_GREATER_THAN] = OPCODE_BGT,
    [OP_LESS_EQUALS] = OPCODE_BLE,
    [OP_GREATER_EQUALS] = OPCODE_BGE,
};


void emit_program(EmitContext *context, Program *into, AST *base) {
    ASTProgram *program = CAST_AST(base, ASTProgram, AST_PROGRAM);

    for (size_t i = 0; i < program->decls.count; i++) {
        Procedure proc =  { 0 };
        switch (program->decls.items[i]->type) {
            case AST_PROCEDURE: emit_procedure(context, &proc, program->decls.items[i]); break;
            case AST_EXTERN: continue;
            default:
            UNREACHABLE("not valid program level decl");
            break;
        }

        array_append(*into, proc);
    }
}


void emit_procedure(EmitContext *context, Procedure *into, AST *base) {
    ASTProc *procedure = CAST_AST(base, ASTProc, AST_PROCEDURE);
    ASTBlock *body = CAST_AST(procedure->body, ASTBlock, AST_BLOCK);
    ASTIdentifier *name = CAST_AST(procedure->name, ASTIdentifier, AST_IDENTIFIER);

    context->procedure = into;
    context->procedure->name = name->name;
    context->procedure->vregs = (VregInfoTable) { 0 };
    context->procedure->slots = (SlotTable) { 0 };
    context->procedure->cfg = (CFGraph) { 0 };
    context->slot_from_symbol = (HashTable) { 0 }; // reset per-proc
    
    context->procedure->cfg.entry_block = allocate_block(context);
    start_block(context, context->procedure->cfg.entry_block);

    for (size_t i = 0; i < procedure->params.count; i++) {
        ASTDecl *decl = CAST_AST(procedure->params.items[i], ASTDecl, AST_DECL);
        ASTIdentifier *ident = CAST_AST(decl->lhs, ASTIdentifier, AST_IDENTIFIER);
        Operand slot = slot_for(context, get_symbol(body->symbols, ident->name), true);
        emit_instr(context, OPCODE_PARAM, slot, IMM(i), NONE);
    }

    emit_block(context, procedure->body);
    if (context->current_block != -1) emit_ret(context, (Operand) { 0 });
}


void emit_block(EmitContext *context, AST *base) {
    ASTBlock *block = CAST_AST(base, ASTBlock, AST_BLOCK);
    context->scope = block->symbols;
    for (size_t i = 0; i < block->stmts.count; i++) {
        emit_statement(context, block->stmts.items[i]);
    }
    context->scope = block->symbols->prev;
}


void emit_statement(EmitContext *context, AST *statement) {
    assert(statement != NULL);
    switch (statement->type) {
        case AST_CALL:       (void)emit_call(context, statement, false);   break;
        case AST_BINARY_OP:  (void)emit_expr(context, statement);   break;
        case AST_BLOCK:      emit_block(context, statement);  break;
        case AST_DECL:       emit_decl(context, statement);   break;
        case AST_ASSIGNMENT: emit_assign(context, statement); break;
        case AST_RETURN:     emit_return(context, statement); break;
        case AST_IF:         emit_if(context, statement);     break;
        case AST_WHILE:      emit_while(context, statement);  break;
        default:  UNREACHABLE("ASTType");
    }
}




void emit_list(EmitContext *context, AST *base, Operand into) {
    ASTListConstruct *list = CAST_AST(base, ASTListConstruct, AST_LIST);

    for (size_t i = 0; i < list->items.count; i++) {
        Operand arg = emit_expr(context, list->items.items[i]);
        
        emit_instr(context, OPCODE_STORE_INDEX,
                   MEM(INDEX(into.vreg, 0, -i * quecto_type_size(list->items.items[i]->resolved_qtype), 0)),
                   arg,
                   NONE);
    }
}


Operand decay(EmitContext *context, AST *base) {
    switch (base->resolved_qtype->type) {
        case QUECTO_ARRAY:
            return addr_of_slot(context, slot_from_identifier(context, base));
        case QUECTO_POINTER:
            return emit_instr(context, OPCODE_LOAD, allocate_vreg(context, (VregInfo){.size = 8}), slot_from_identifier(context, base), NONE);
        default:
            assert(0 && "type cannot decay");
            return NONE;
    }
}


Operand slot_from_identifier(EmitContext *context, AST *base) {
    ASTIdentifier *ident = CAST_AST(base, ASTIdentifier, AST_IDENTIFIER);
    SymbolData *var = get_symbol(context->scope, ident->name);
    return slot_for(context, var, false);
}


Operand mem_from_index(EmitContext *context, AST *base) {
    ASTIndex *index = CAST_AST(base, ASTIndex, AST_INDEX);
    Operand head = decay(context, index->head);
    Operand ind = emit_expr(context, index->index);
    ind = emit_instr(context, OPCODE_EXT_Z, allocate_vreg(context, (VregInfo){.size=8,.sign=false}), ind, NONE);
    int size = quecto_type_size(index->base.resolved_qtype);
    
    return MEM(INDEX(head.vreg, ind.vreg, 0, size));
}


Operand addr_of_slot(EmitContext *context, Operand slot) {
    assert(slot.type == OPERAND_SLOT);

    Operand out = allocate_vreg(context, (VregInfo) {.size = 8, .sign = 0});
    
    context->procedure->slots.items[slot.slot].address_taken = true;
    emit_instr(context, OPCODE_ADDR, out, slot, NONE);

    return out;
}


void emit_decl(EmitContext *context, AST *base) {
    ASTDecl *decl = CAST_AST(base, ASTDecl, AST_DECL);
    if (decl->rhs == NULL) return;

    Operand slot = slot_from_identifier(context, decl->lhs);

    switch (decl->rhs->type) {
        case AST_LIST:
            emit_list(context, decl->rhs, addr_of_slot(context, slot));
            break;
        default: {
            Operand rhs = emit_expr(context, decl->rhs);
            emit_instr(context, OPCODE_STORE, slot, rhs, NONE);
        }
    }
}


Operand emit_assign_lhs(EmitContext *context, AST *base) {
    switch (base->type) {
        case AST_IDENTIFIER: return slot_from_identifier(context, base);
        case AST_INDEX: return mem_from_index(context, base);
        case AST_ACCESS: UNREACHABLE("unimplemented"); break;
        default: UNREACHABLE("invalid lhs");
    }
}


void emit_assign(EmitContext *context, AST *base) {
    ASTAssign *assign = CAST_AST(base, ASTAssign, AST_ASSIGNMENT);
    Operand lhs = emit_assign_lhs(context, assign->lhs);
    Operand rhs = emit_expr(context, assign->rhs);
    emit_instr(context, lhs.type == OPERAND_MEM ? OPCODE_STORE_INDEX : OPCODE_STORE, lhs, rhs, NONE);    
}

void emit_if(EmitContext *context, AST *base) {
    ASTIf *_if = CAST_AST(base, ASTIf, AST_IF);

    int end = allocate_block(context);
    int btrue = allocate_block(context);
    int bfalse = _if->otherwise ? allocate_block(context) : end;
    emit_branch(context, btrue, bfalse, _if->cond);
    
    start_block(context, btrue);
    emit_statement(context, _if->then);
    fallthrough_to(context, end);

    if (_if->otherwise) {
        start_block(context, bfalse);
        emit_if_chain(context, _if->otherwise, end);
    }
    start_block(context, end);
}


void emit_if_chain(EmitContext *context, AST *base, int end) {
    if (base->type == AST_ELIF) {
        ASTIf *_if = CAST_AST(base, ASTIf, AST_ELIF);
        int btrue = allocate_block(context);
        int bfalse = allocate_block(context);

        emit_branch(context, btrue, bfalse, _if->cond);

        start_block(context, btrue);
        emit_statement(context, _if->then);
        fallthrough_to(context, end);

        if (_if->otherwise) {
            start_block(context, bfalse);
            emit_if_chain(context, _if->otherwise, end);
        }
    } else if (base->type == AST_ELSE) {
        ASTIf *_if = CAST_AST(base, ASTIf, AST_ELSE);
        emit_statement(context, _if->then);
        emit_jmp(context, end);
    } else {
        assert(0 && "only elif and else should be chained in 'statement->otherwise' ");
    }
}


void emit_while(EmitContext *context, AST *base) {
    ASTWhile *_while = CAST_AST(base, ASTWhile, AST_WHILE);

    int condition = allocate_block(context);
    int loop = allocate_block(context);
    int end = allocate_block(context);

    start_block(context, condition);
    emit_branch(context, loop, end, _while->cond);
    start_block(context, loop);
    emit_statement(context, _while->then);
    emit_jmp(context, condition);
    start_block(context, end);
}


void emit_return(EmitContext *context, AST *base) {
    ASTReturn *ret = CAST_AST(base, ASTReturn, AST_RETURN);

    if (ret->expr != NULL)
        emit_ret(context, emit_expr(context, ret->expr));
    else
        emit_ret(context, NONE);
}


Operand emit_call(EmitContext *context, AST *base, bool has_dest) {
    ASTCall *call = CAST_AST(base, ASTCall, AST_CALL);
    ASTIdentifier *ident = CAST_AST(call->ident, ASTIdentifier, AST_IDENTIFIER);

    Operand ops[call->args.count];
    for (size_t i = 0; i < call->args.count; i++) {
        ops[i] = emit_expr(context, call->args.items[i]);
    }
    
    for (size_t i = 0; i < call->args.count; i++) {
        emit_instr(context, OPCODE_ARG, NONE, ops[i], NONE);
    }
    
    return emit_instr(context, OPCODE_CALL,
        has_dest ? allocate_vreg_for_type(context, call->base.resolved_qtype) : NONE,
        global_for(context, ident->name),
        NONE
    );
}


Operand emit_ref(EmitContext *context, AST* base) {
    ASTRef *ref = CAST_AST(base, ASTRef, AST_REF);
    return emit_instr(context, OPCODE_ADDR, allocate_vreg(context, (VregInfo){ .size = 8 }), emit_expr(context, ref->head), NONE);
}


Operand emit_binary_op(EmitContext *context, AST *base) {
    ASTBinaryOp *op = CAST_AST(base, ASTBinaryOp, AST_BINARY_OP);
    
    Opcode opcode;
    switch (op->op) {
        case OP_PLUS:           opcode = OPCODE_ADD; break;
        case OP_MINUS:          opcode = OPCODE_SUB; break;
        case OP_MULTIPLY:       opcode = OPCODE_MUL; break;
        case OP_DIVIDE:         opcode = OPCODE_DIV; break;
        case OP_EQUALS:         opcode = OPCODE_CMP_EQ; break;
        case OP_LESS_THAN:      opcode = OPCODE_CMP_LT; break;
        case OP_GREATER_THAN:   opcode = OPCODE_CMP_GT; break;
        case OP_LESS_EQUALS:    opcode = OPCODE_CMP_LEQ; break;
        case OP_GREATER_EQUALS: opcode = OPCODE_CMP_GEQ; break;
        default: UNREACHABLE("invalid operation");
    }

    return emit_instr(context, opcode,
          allocate_vreg_for_type(context, op->base.resolved_qtype),
          emit_expr(context, op->left),
          emit_expr(context, op->right)
    );
}


Operand emit_expr(EmitContext *context, AST *expr) {
    switch (expr->type) {
        case AST_INT_LIT:    return emit_instr(context, OPCODE_IMM, allocate_vreg_for_type(context, expr->resolved_qtype), IMM(((ASTIntLit*)expr)->literal), NONE);
        case AST_IDENTIFIER:
            if (quecto_is_array(expr->resolved_qtype))
                return decay(context, expr);
            return emit_instr(context, OPCODE_LOAD, allocate_vreg_for_type(context, expr->resolved_qtype), slot_from_identifier(context, expr), NONE);
        case AST_INDEX:      return emit_instr(context, OPCODE_INDEX, allocate_vreg_for_type(context, expr->resolved_qtype), mem_from_index(context, expr), NONE);
        case AST_REF:        return emit_ref(context, expr); 
        case AST_CALL:       return emit_call(context, expr, true);
        case AST_BINARY_OP:  return emit_binary_op(context, expr);
        default: UNREACHABLE("invalid expr ast type");
    }
}


Operand emit_instr(EmitContext *context, Opcode opcode, Operand dest, Operand arg1, Operand arg2) {
    Instr instr = { 0 };
    instr.opcode = opcode;
    instr.dest = dest;
    instr.arg1 = arg1;
    instr.arg2 = arg2;
    return emit_instr_into_block(context->arena, &context->procedure->cfg.items[context->current_block], instr);
}


void emit_branch(EmitContext *context, int true_target, int false_target, AST *base) {
    assert(context->current_block != -1);
    ASTBinaryOp *expr = CAST_AST(base, ASTBinaryOp, AST_BINARY_OP);
    
    Instr terminator = { 0 };
    terminator.opcode = jump_condition_opcode_table[expr->op];
    terminator.arg1 = emit_expr(context, expr->left);
    terminator.arg2 = emit_expr(context, expr->right);

    int size = quecto_type_size(base->resolved_qtype);

    if (context->procedure->vregs.items[terminator.arg1.vreg].size < size) terminator.arg1 = emit_instr(context, OPCODE_EXT_Z, allocate_vreg(context, (VregInfo){.size=size,.sign=false}), terminator.arg1, NONE);
    if (context->procedure->vregs.items[terminator.arg2.vreg].size < size) terminator.arg2 = emit_instr(context, OPCODE_EXT_Z, allocate_vreg(context, (VregInfo){.size=size,.sign=false}), terminator.arg2, NONE);

    context->procedure->cfg.items[context->current_block].successors[0] = true_target;
    context->procedure->cfg.items[context->current_block].successors[1] = false_target;

    emit_instr_into_block(context->arena, &context->procedure->cfg.items[context->current_block], terminator);
    context->current_block = -1;
}


void emit_jmp(EmitContext *context, int target) {
    assert(context->current_block != -1);
    
    Instr terminator = { .opcode = OPCODE_JMP };
    context->procedure->cfg.items[context->current_block].successors[0] = target;
    emit_instr_into_block(context->arena, &context->procedure->cfg.items[context->current_block], terminator);

    context->current_block = -1;
}


void emit_ret(EmitContext *context, Operand with) {
    assert(context->current_block != -1);
    
    Instr terminator = { .opcode = OPCODE_RET, .arg1 = with };
    emit_instr_into_block(context->arena, &context->procedure->cfg.items[context->current_block], terminator);

    context->current_block = -1;
}


int allocate_block(EmitContext *context) {
    BasicBlock block = { 0 };
    block.successors[0] = -1;
    block.successors[1] = -1;
    arena_array_append(context->arena, context->procedure->cfg, block);
    return context->procedure->cfg.count - 1;
}


void start_block(EmitContext *context, int block_id) {
    if (context->current_block != -1 && block_id != context->current_block) {
        emit_jmp(context, block_id);
    }
    context->current_block = block_id;
}


void fallthrough_to(EmitContext *context, int block) {
    if (context->current_block != -1) emit_jmp(context, block);
}


Operand allocate_vreg_explicit(Arena *arena, Procedure *procedure, VregInfo info) {
    info.interval.iend = -1;
    info.interval.bend = -1;
    info.interval.istart = -1;
    info.interval.bstart = -1;
    info.color.index = -1;
    info.hint = 0;
    arena_array_append(arena, procedure->vregs, info);
    return VREG(procedure->vregs.count - 1);
}


Operand allocate_vreg(EmitContext *context, VregInfo info) {
    return allocate_vreg_explicit(context->arena, context->procedure, info);
}


Operand allocate_vreg_for_type(EmitContext *context, QuectoType *type) {
  assert(type != NULL && "quecto type not initialized or nulled");

  VregInfo info;
  info.size = quecto_type_size(type);
  info.sign = quecto_is_signed(type);
  info.interval.bend = -1;
  info.interval.iend = -1;
  info.color.index = -1;

  return allocate_vreg(context, info);
}


Operand global_for(EmitContext *context, const char *ident) {
    SymbolTable *globals = context->scope;
    for (;globals->prev != NULL; globals = globals->prev);

    return GLOBAL(ht_nindex(&globals->table, ident, strlen(ident)));
}


Operand slot_for(EmitContext *context, SymbolData *sym, bool param) {
    int *slot = ht_nsearch(&context->slot_from_symbol, &sym, sizeof(SymbolData*)); //pass by pointer cause pointers shud be unique alr
    if (slot != NULL) {
        return SLOT(*slot);
    }

    SlotInfo info = (SlotInfo) {
        .size = quecto_type_size(sym->qtype),
        .sign = quecto_is_signed(sym->qtype),
        .param = param,
        .address_taken = false,
    };

    slot = arena_alloc(context->arena, sizeof(int));
    *slot = context->procedure->slots.count;
    arena_array_append(context->arena, context->procedure->slots, info);

    ht_ninsert(&context->slot_from_symbol, &sym, sizeof(SymbolData*), slot);

    return SLOT(*slot);
}

