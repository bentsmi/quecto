#include "passes.h"
#include "cfg.h"
#include "common.h"
#include "ir.h"


void passes_run(Program *program, Passes passes, Arenas arenas) {
    for (size_t i = 0; i < program->count; i++) {
        Procedure *procedure = &program->items[i];
        passes_preparation(&arenas, procedure);

        for (size_t j = 0; passes[j] != NULL; j++) {
            passes[j](&arenas, procedure);
        }
    }
}


void passes_preparation(Arenas *arenas, Procedure *procedure) {
    CFGraph *cfg = &procedure->cfg;

    allocate_cfg(arenas->persistent, cfg);
    
    int i = 0;
    fill_rpo(cfg, &i, procedure->cfg.entry_block);
    fill_predecessors(cfg);
    fill_idom(cfg); // needs rpo, rpo_list, predecessors
    fill_df(cfg);
    pass_liveness(arenas, procedure); // needs vreg info
}


void pass_liveness(Arenas *arenas, Procedure *procedure) {
    CFGraph *cfg = &procedure->cfg;
    VregInfoTable *vregs = &procedure->vregs;

    for (size_t i = 0; i < cfg->count; i++) {
        set_create(&cfg->live_in[i], arenas->persistent, vregs->count);
        set_create(&cfg->live_out[i], arenas->persistent, vregs->count);
        set_create(&cfg->uses[i], arenas->persistent, vregs->count);
        set_create(&cfg->defines[i], arenas->persistent, vregs->count);
    }

    Set tmp;
    set_create(&tmp, arenas->scratch, vregs->count);
    
    for (size_t b = 0; b < cfg->count; b++) {
        BasicBlock *block = &cfg->items[b];
        Instr instr;

        for (size_t i = 0; i < block->bytecode.count; i++) {
            instr = block->bytecode.items[i];
            if (instr.dest.type == OPERAND_VREG) {
                set_insert(&cfg->defines[b], instr.dest.vreg);
            }

            size_t used[16];
            size_t count = instr_collect_used_vregs(instr, used);
            for (size_t v = 0; v < count; v++) {
                if (!set_has(&cfg->defines[b], used[v])) {
                    set_insert(&cfg->uses[b], used[v]);
                }
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t k = 0; k < cfg->count; k++) {
            int b = cfg->rpo_list[k];
            BasicBlock *block = &cfg->items[b];


            for (int s = 0; s < 2; s++) {
                int succ = block->successors[s];

                if (succ == -1) continue;
                set_add(&tmp, &cfg->live_in[succ]);
                
                BasicBlock *successor = &cfg->items[succ];
                int idx = -1;
                for (size_t i = 0; i < successor->predecessors.count; i++) {
                    if (b == successor->predecessors.items[i])
                        idx = i;
                }
                for (size_t p = 0; p < successor->phis.count; p++) {
                    Phi phi = successor->phis.items[p];
                    if (phi.args[idx].type == OPERAND_VREG)
                        set_insert(&tmp, phi.args[idx].vreg);
                }
                
            }

            if (!set_equals(&cfg->live_out[b], &tmp))
                changed = true;
            
            set_copy(&cfg->live_out[b], &tmp);
            set_subtract(&tmp, &cfg->defines[b]);
            set_add(&tmp, &cfg->uses[b]);

            if (!set_equals(&cfg->live_in[b], &tmp))
                changed = true;
            
            
            set_copy(&cfg->live_in[b], &tmp);
            set_clear(&tmp);
        }
    }
}


void pass_insert_phis(Arenas *arenas, Procedure *procedure) {
    CFGraph *cfg = &procedure->cfg;
    SlotTable *slots = &procedure->slots;

    Set stored_at_blocks, phi_blocks, working;

    size_t mark = arena_mark(arenas->scratch);

    set_create(&stored_at_blocks, arenas->scratch, cfg->count);
    set_create(&phi_blocks, arenas->scratch, cfg->count);
    set_create(&working, arenas->scratch, cfg->count);
    
    for (size_t slot = 0; slot < slots->count; slot++) {
        if (slots->items[slot].address_taken) continue;

        set_clear(&stored_at_blocks);
        set_clear(&phi_blocks);
        set_clear(&working);

        for (size_t block = 0; block < cfg->count; block++) {
            for (size_t j = 0; j < cfg->items[block].bytecode.count; j++) {
                Instr instr = cfg->items[block].bytecode.items[j];
                if (instr.opcode == OPCODE_STORE && instr.dest.type == OPERAND_SLOT && instr.dest.slot == slot) {
                    set_insert(&stored_at_blocks, block);
                    break;
                }
            }
        }

        set_add(&working, &stored_at_blocks);
        while (!set_empty(&working)) {
            size_t val;
            bool popped = set_pop(&working, &val);
            assert(popped);
            for (size_t j = 0; j < cfg->count; j++) {
                if (!set_has(&cfg->df[val], j)) continue; // add live in pruning, needs more computation

                if (!set_has(&phi_blocks, j)) {
                    set_insert(&phi_blocks, j);
                    set_insert(&working, j);
                }
            } 
        }

        while (!set_empty(&phi_blocks)) {
            size_t block;
            bool popped = set_pop(&phi_blocks, &block);
            assert(popped);
            Phi phi = { 0 };
            phi.slot = slot;
            phi.args = arena_alloc(arenas->persistent, sizeof(Operand) * cfg->items[block].predecessors.count);
            arena_array_append(arenas->persistent, cfg->items[block].phis, phi);
        }
    }
    arena_restore(arenas->scratch, mark);
}


void pass_kill_slots(Arenas *arenas, Procedure *procedure) {
    (void)arenas; // doesn't allocate, but needs to match pass signature
    
    CFGraph *cfg = &procedure->cfg;
    for (size_t s = 0; s < procedure->slots.count; s++) {
        procedure->slots.items[s].killed = true;
    }
    for (size_t k = 0; k < cfg->count; k++) {
        int b = cfg->rpo_list[k];

        for (size_t i = 0; i < cfg->items[b].bytecode.count; i++) {
            Instr instr = cfg->items[b].bytecode.items[i];

            if (instr.dest.type == OPERAND_SLOT) {
                procedure->slots.items[instr.dest.slot].killed = false;
            }
            if (instr.arg1.type == OPERAND_SLOT) {
                procedure->slots.items[instr.arg1.slot].killed = false;
            }
            if (instr.arg2.type == OPERAND_SLOT) {
                procedure->slots.items[instr.arg2.slot].killed = false;
            }
            
        }
    }
}

typedef DEFINE_STACK(Operand) OperandStack;
DEFINE_STACK_DEF(OperandStack, Operand)

void pass_rename_recurs(Arenas *arenas, Procedure *procedure, OperandStack stacks[], size_t bid) {
    CFGraph *cfg = &procedure->cfg;
    SlotTable *slots = &procedure->slots;
    BasicBlock *block = &cfg->items[bid];

    size_t *push_count = arena_alloc(arenas->scratch, sizeof(size_t) * slots->count); // taken care of by interface to this

    for (size_t i = 0; i < block->phis.count; i++) {
        Phi *phi = &block->phis.items[i];
        phi->dest = allocate_vreg_explicit(arenas->persistent, procedure, vregi_from_sloti(slots->items[phi->slot]));
        OperandStack_push(&stacks[phi->slot], phi->dest);
        push_count[phi->slot]++;
    }

    for (size_t i = 0; i < block->bytecode.count; i++) {
        Instr *instr = &block->bytecode.items[i];
       
        if (instr_match(instr, OPCODE_LOAD, -1, OPERAND_SLOT, OPERAND_NONE) && !slots->items[instr->arg1.slot].address_taken) {
            instr->opcode = OPCODE_COPY;
            instr->arg1 = OperandStack_peek(&stacks[instr->arg1.slot], 1);
        }

        if (instr_match(instr, OPCODE_PARAM, OPERAND_SLOT, -1, -1) && !slots->items[instr->dest.slot].address_taken) {
            instr->dest = OperandStack_peek(&stacks[instr->dest.slot], 1);
        }

        if (instr_match(instr, OPCODE_STORE, OPERAND_SLOT, -1, -1) && !slots->items[instr->dest.slot].address_taken) {
            OperandStack_push(&stacks[instr->dest.slot], instr->arg1);
            push_count[instr->dest.slot]++;
            *instr = (Instr) { 0 };
        }
    }

    for (size_t i = 0; i < 2; i++) {
        int s = block->successors[i];
        if (s == -1) continue;

        BasicBlock *succ = &cfg->items[s];

        int idx = -1;
        for (size_t j = 0; j < succ->predecessors.count; j++) {
            if (succ->predecessors.items[j] == (int)bid) {
                idx = j;
                break;
            }
        }

        for (size_t j = 0; j < succ->phis.count; j++) {
            Phi *p = &succ->phis.items[j];
            p->args[idx] = OperandStack_peek(&stacks[p->slot], 1);
        }
    }

    for (size_t i = 0; i < cfg->count; i++) {
        if (cfg->idom[i] == (int)bid && i != bid)
            pass_rename_recurs(arenas, procedure, stacks, i);
    }

    for (size_t i = 0; i < slots->count; i++) {
        for (size_t j = 0; j < push_count[i]; j++)
            OperandStack_pop(&stacks[i]);
    }
}


void pass_rename(Arenas *arenas, Procedure *procedure) {
    SlotTable *slots = &procedure->slots;

    size_t mark = arena_mark(arenas->scratch);
    OperandStack *stacks = arena_alloc(arenas->scratch, sizeof(OperandStack) * slots->count);

    for (size_t i = 0; i < slots->count; i++) {
        OperandStack_set_backing(&stacks[i], arenas->scratch);
        if (slots->items[i].param && !slots->items[i].address_taken) {
            OperandStack_push(&stacks[i], allocate_vreg_explicit(arenas->persistent, procedure, vregi_from_sloti(slots->items[i])));
        }
    }

    pass_rename_recurs(arenas, procedure, stacks, procedure->cfg.entry_block);
    arena_restore(arenas->scratch, mark);
}


void pass_remove_copies(Arenas *arenas, Procedure *procedure) {
    (void)arenas; // doesn't allocate but needs to match signature
    
    CFGraph *cfg = &procedure->cfg;
    for (size_t i = 0; i < cfg->count; i++) {
        for (size_t j = 0; j < cfg->items[i].bytecode.count; j++) {
            Instr *instr = &cfg->items[i].bytecode.items[j];

            if (instr->opcode == OPCODE_COPY && instr->dest.type == OPERAND_VREG && instr->arg1.type == OPERAND_VREG) {
                for (size_t k = j + 1; k < cfg->items[i].bytecode.count; k++) {
                    Instr *marked = &cfg->items[i].bytecode.items[k];
                    instr_replace_vreg(marked, instr->dest.vreg, instr->arg1.vreg);
                }
                *instr = (Instr) { 0 };
            }
        }
    }
}


void pass_compute_liveness(Arenas *arenas, Procedure *procedure) {
    (void)arenas;
    
    CFGraph *cfg = &procedure->cfg;
    VregInfoTable *vregs = &procedure->vregs;

    for (size_t i = 0; i < vregs->count; i++) {
        int bend = -1;
        
        for (size_t j = cfg->count; j-- > 0;) {
            int b = cfg->rpo_list[j];
            bool alive = set_has(&cfg->live_in[b], i) || set_has(&cfg->defines[b], i);
            
            if (!alive) continue;
            
            if (!set_has(&cfg->live_out[b], i)) { // dies in this block
                int iend = 0;
                for (size_t k = cfg->items[b].bytecode.count; k-- > 0;) {
                    Instr instr = cfg->items[b].bytecode.items[k];
                    if (instr_uses_vreg(instr, i)) {
                        iend = (int)k;
                        break;
                    }
                }
                bend = b;
                vregs->items[i].interval.iend = iend;
            }  else {
                bend = b;
                vregs->items[i].interval.iend = cfg->items[b].bytecode.count;
            }
            
            if (set_has(&cfg->defines[b], i)) {
                int istart = 0;
                for (size_t k = cfg->items[b].bytecode.count; k-- > 0;) {
                    Instr instr = cfg->items[b].bytecode.items[k];
                    if (instr_defines_vreg(instr, i)) {
                        istart = (int)k;
                        break;
                    }
                }
                vregs->items[i].interval.istart = istart;
                vregs->items[i].interval.bstart = b;
            }

            break;
        }
        vregs->items[i].interval.bend = bend;
    }

    for (size_t b = 0; b < cfg->count; b++) {
        for (size_t s = 0; s < 2; s++) { // find backedges
            int succ = cfg->items[b].successors[s];
            
            if (succ == -1) continue;
            if (!dominates(cfg, succ, b)) continue; // if block -> succ, and succ dominates block, must be loop/backedge
                                                                // thus we go to the succesors other successor and that must be exit
            int exit = exit_for(cfg, succ);
            if (exit == -1) continue;

            for (size_t v = 0; v < vregs->count; v++) {
                if (!set_has(&cfg->live_out[b], v) || !set_has(&cfg->live_in[succ], v)) continue;
                
                int bend = vregs->items[v].interval.bend;
                
                if (bend == -1 || cfg->rpo[exit] > cfg->rpo[bend]) {
                    vregs->items[v].interval.bend = exit;
                    vregs->items[v].interval.iend = 0;
                }
            }
        }
    }
}


void pass_crosses_call(Arenas *arenas, Procedure *procedure) {
    CFGraph *cfg = &procedure->cfg;
    Set live;
    set_create(&live, arenas->scratch, procedure->vregs.count);
    
    for (size_t v = 0; v < procedure->vregs.count; v++) {
        procedure->vregs.items[v].crosses_call = false;
    }
    
    for (size_t k = 0; k < cfg->count; k++) {
        int b = cfg->rpo_list[k];

        set_copy(&live, &cfg->live_out[b]);

        for (size_t i = cfg->items[b].bytecode.count; i-- > 0;) {
            Instr instr = cfg->items[b].bytecode.items[i];
            if (instr.opcode == OPCODE_CALL) {
                for (size_t v = 0; v < procedure->vregs.count; v++) {
                    if (set_has(&live, v) && !(instr.dest.type == OPERAND_VREG && instr.dest.vreg == v)) {
                        procedure->vregs.items[v].crosses_call = true;
                        procedure->vregs.items[v].hint |= PR_CALLEE_SAVED;
                    }
                }
            }

            
            if (instr.dest.type == OPERAND_VREG) {
                set_remove(&live, instr.dest.vreg);
            }

            size_t in_use[16];
            size_t count = instr_collect_used_vregs(instr, in_use);
            for (size_t i = 0; i < count; i++) {
                set_insert(&live, in_use[i]);   
            }
        }
    }
}


typedef struct {
    Arena *arena;
    int *position_from_index;
    Set enabled;
    Set general_purpose;
    Set callee_saved;
    Set args;
    Set rets;
    Set intersection;
    Color *backing; // index, flags
} ColorPool;


void color_pool_init(ColorPool *pool, Arena *arena, Color *backing, size_t count) {
    pool->arena = arena;
    pool->backing = backing;
    pool->position_from_index = arena_alloc(arena, sizeof(int) * count);
    set_create(&pool->enabled, arena, count);
    set_create(&pool->intersection, arena, count);
    set_create(&pool->general_purpose, arena, count);
    set_create(&pool->callee_saved, arena, count);
    set_create(&pool->args, arena, count);
    set_create(&pool->rets, arena, count);

    for (size_t i = 0; i < count; i++) {
        pool->position_from_index[backing[i].index] = i;
        if ((backing[i].flags & PR_GENERAL_PURPOSE) == PR_GENERAL_PURPOSE) set_insert(&pool->general_purpose, i);
        if ((backing[i].flags & PR_CALLEE_SAVED) == PR_CALLEE_SAVED) set_insert(&pool->callee_saved, i);
        if ((backing[i].flags & PR_ARG) == PR_ARG) set_insert(&pool->args, i);
        if ((backing[i].flags & PR_RETURN) == PR_RETURN) set_insert(&pool->rets, i);

        set_insert(&pool->enabled, i);
    }

    set_clear(&pool->intersection);
}


void color_pool_push(ColorPool *pool, Color color) {
    set_insert(&pool->enabled, pool->position_from_index[color.index]);
}


PhysRegFlags lift(PhysRegFlags filter) {
    if ((filter & PR_RETURN) == PR_RETURN) return filter & ~PR_RETURN;
    if ((filter & PR_ARG) == PR_ARG) return filter & ~PR_ARG;
    if ((filter & PR_CALLEE_SAVED) == PR_CALLEE_SAVED) return filter & ~PR_CALLEE_SAVED;
    return filter;
}


Color color_pool_pop(ColorPool *pool, PhysRegFlags filter) {
    bool changed = true;

    while (changed) {
        set_copy(&pool->intersection, &pool->enabled);
        changed = false;

        if ((filter & PR_GENERAL_PURPOSE) == PR_GENERAL_PURPOSE) set_intersect(&pool->intersection, &pool->general_purpose);
        if ((filter & PR_CALLEE_SAVED) == PR_CALLEE_SAVED) set_intersect(&pool->intersection, &pool->callee_saved);
        if ((filter & PR_ARG) == PR_ARG) set_intersect(&pool->intersection, &pool->args);
        if ((filter & PR_RETURN) == PR_RETURN) set_intersect(&pool->intersection, &pool->rets);

        if (set_empty(&pool->intersection)) {
            if (filter != lift(filter)) changed = true;
            filter = lift(filter);
            continue;
        }

        size_t position;
        bool popped = set_pop(&pool->intersection, &position);
        assert(popped);
        set_remove(&pool->enabled, position);
        return pool->backing[position];
    }
    assert(0 && "pop failed");
    return (Color) { .index = -1 };
}


bool color_pool_empty(ColorPool *pool) {
    return set_empty(&pool->enabled);
}


void pass_precoloring(Arenas *arenas, Procedure *procedure) {
    (void)arenas;
    
    CFGraph *cfg = &procedure->cfg;
    VregInfoTable *vregs = &procedure->vregs;

    for (size_t k = 0; k < cfg->count; k++) {
        int b = cfg->rpo_list[k];

        for (size_t i = 0; i < cfg->items[b].bytecode.count; i++) {
            Instr instr = cfg->items[b].bytecode.items[i];

            if (instr.opcode == OPCODE_RET && instr.arg1.type == OPERAND_VREG) {
                vregs->items[instr.arg1.vreg].hint |= PR_RETURN;
            }

            if (instr.opcode == OPCODE_ARG && instr.arg1.type == OPERAND_VREG) {
                vregs->items[instr.arg1.vreg].hint |= PR_ARG;
            }

            if (instr.opcode == OPCODE_PARAM && instr.dest.type == OPERAND_VREG) {
                vregs->items[instr.dest.vreg].hint |= PR_ARG;
            }

            if (instr.opcode == OPCODE_CALL && instr.dest.type == OPERAND_VREG && !vregs->items[instr.dest.vreg].crosses_call) {
                vregs->items[instr.dest.vreg].hint |= PR_RETURN;
            }
        }
    }
}


void pass_color_cfg_recurs(Arenas *arenas, Procedure *procedure, size_t block, Set *live, ColorPool *pool) {
    CFGraph *cfg = &procedure->cfg;
    VregInfoTable *vregs = &procedure->vregs;
    BasicBlock *blk = &cfg->items[block];

    size_t mark = arena_mark(arenas->scratch);
    Set tmp, last_pool;
    set_create(&last_pool, arenas->scratch, pool->enabled.bit_count);
    set_copy(&last_pool, &pool->enabled);
    set_create(&tmp, arenas->scratch, vregs->count);
    set_copy(&tmp, live);

    for (size_t p = 0; p < blk->phis.count; p++) {
        Phi phi = blk->phis.items[p];
        set_insert(live, phi.dest.vreg);

        PhysRegFlags filter = PR_GENERAL_PURPOSE | vregs->items[phi.dest.vreg].hint;;
        if (vregs->items[phi.dest.vreg].crosses_call) {
            filter |= PR_CALLEE_SAVED;
        }
            
        if (!color_pool_empty(pool)) vregs->items[phi.dest.vreg].color = color_pool_pop(pool, filter);
        else assert(0 && "no spills yet...");
    }

    for (size_t i = 0; i < blk->bytecode.count; i++) {
        Instr instr = blk->bytecode.items[i];
        
        bool non_comm = (instr.opcode == OPCODE_SUB || instr.opcode == OPCODE_DIV);
        int protected = (non_comm && instr.arg2.type == OPERAND_VREG) ? (int)instr.arg2.vreg : -1;

        for (size_t v = 0; v < vregs->count; v++) {
            if (protected >= 0 && v == (size_t)protected) continue;
            
            if (set_has(live, v)
                && !set_has(&cfg->live_out[block], v)
                && vregs->items[v].interval.iend <= (int)i
                && vregs->items[v].interval.bend == (int)block
            ) {
                set_remove(live, v);
                color_pool_push(pool, vregs->items[v].color);
            }
        }

        if (instr.dest.type == OPERAND_VREG) {
            set_insert(live, instr.dest.vreg);

            PhysRegFlags filter = PR_GENERAL_PURPOSE | vregs->items[instr.dest.vreg].hint;

            if (vregs->items[instr.dest.vreg].crosses_call) {
                filter |= PR_CALLEE_SAVED;
            }

            if (!color_pool_empty(pool)) vregs->items[instr.dest.vreg].color = color_pool_pop(pool, filter);
            else assert(0 && "no spills yet...");            


            if ((vregs->items[instr.dest.vreg].color.flags & PR_CALLEE_SAVED) == PR_CALLEE_SAVED) {
                set_insert(&procedure->saved_colors, vregs->items[instr.dest.vreg].color.index);
            }
        }

        if (protected != -1 && set_has(live, protected)
                && !set_has(&cfg->live_out[block], protected)
                && vregs->items[protected].interval.iend <= (int)i
                && vregs->items[protected].interval.bend == (int)block
        ) {
            set_remove(live, protected);
            color_pool_push(pool, vregs->items[protected].color);
        }
    }

    for (size_t b = 0; b < cfg->count; b++) {
        if (cfg->idom[b] == (int)block && b != block) pass_color_cfg_recurs(arenas, procedure, b, live, pool);
    }

    set_copy(live, &tmp);
    set_copy(&pool->enabled, &last_pool);
    arena_restore(arenas->scratch, mark);
}


void pass_color_cfg(Arenas *arenas, Procedure *procedure) {
    size_t mark = arena_mark(arenas->scratch);
    
    Set live;
    set_create(&live, arenas->scratch, procedure->vregs.count);
    set_create(&procedure->saved_colors, arenas->persistent, 16);

    Color colors [16] = {
        (Color) { .index = 15, .flags = PR_GENERAL_PURPOSE | PR_CALLEE_SAVED}, // r15
        (Color) { .index = 14, .flags = PR_GENERAL_PURPOSE | PR_CALLEE_SAVED}, // r14
        (Color) { .index = 13, .flags = PR_GENERAL_PURPOSE | PR_CALLEE_SAVED}, // r13
        (Color) { .index = 12, .flags = PR_GENERAL_PURPOSE | PR_CALLEE_SAVED }, // r12
        (Color) { .index = 11, .flags = PR_GENERAL_PURPOSE }, // r11
        (Color) { .index = 10, .flags = PR_GENERAL_PURPOSE }, // r10
        (Color) { .index = 9, .flags = PR_GENERAL_PURPOSE }, // r9
        (Color) { .index = 8, .flags = PR_GENERAL_PURPOSE }, // r8
        (Color) { .index = 7, .flags = PR_STACK_POINTER }, // rsp
        (Color) { .index = 6, .flags = PR_BASE_POINTER }, // rbp
        (Color) { .index = 3, .flags = PR_GENERAL_PURPOSE | PR_ARG }, // rdx
        (Color) { .index = 2, .flags = PR_GENERAL_PURPOSE | PR_ARG }, // rcx
        
        (Color) { .index = 4, .flags = PR_GENERAL_PURPOSE | PR_ARG }, // rsi
        (Color) { .index = 5, .flags = PR_GENERAL_PURPOSE | PR_ARG }, // rdi
        
        (Color) { .index = 1, .flags = PR_GENERAL_PURPOSE }, // rbx
        (Color) { .index = 0, .flags = PR_GENERAL_PURPOSE | PR_RETURN}, // rax
    };

    ColorPool pool = { 0 };
    color_pool_init(&pool, arenas->scratch, colors, 16);

    pass_color_cfg_recurs(arenas, procedure, procedure->cfg.entry_block, &live, &pool);
    arena_restore(arenas->scratch, mark);
}


void pass_phis_into_copies(Arenas *arenas, Procedure *procedure) {
    CFGraph *cfg = &procedure->cfg;
    for (size_t b = 0; b < cfg->count; b++) {
        for (size_t p = 0; p < cfg->items[b].phis.count; p++) { // arg should be same as pred
            Phi phi = cfg->items[b].phis.items[p];
            for (size_t a = 0; a < cfg->items[b].predecessors.count; a++) {
                Instr instr = { 0 };
                instr.opcode = OPCODE_COPY;
                instr.dest = phi.dest;
                instr.arg1 = phi.args[a];
                add_instr_x_before_end_block(arenas->persistent,&cfg->items[cfg->items[b].predecessors.items[a]], instr);
            }
        }
    }
}


void pass_sweep_nops(Arenas *arenas, Procedure *procedure) {
    CFGraph *cfg = &procedure->cfg;
    for (size_t b = 0; b < cfg->count; b++) {
        Bytecode new;
        new.items = arena_alloc(arenas->persistent, sizeof(Instr) * cfg->items[b].bytecode.count);
        new.capacity = cfg->items[b].bytecode.count;
        new.count = 0;
        for (size_t i = 0; i < cfg->items[b].bytecode.count; i++) {
            if (cfg->items[b].bytecode.items[i].opcode != OPCODE_NONE) {
                new.items[new.count++] = cfg->items[b].bytecode.items[i];
            }
        }
        cfg->items[b].bytecode = new;
    }
}


void debug_pass_print(Arenas *arenas, Procedure *procedure) {
    (void)arenas;
    print_procedure(*procedure);
}


void debug_pass_print_colored(Arenas *arenas, Procedure *procedure) {
    (void)arenas;
    print_procedure_colored(procedure);
}
