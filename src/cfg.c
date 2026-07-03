
#include "ast.h"
#include "common.h"
#include "cfg.h"
#include "passes.h"

Operand emit_instr_into_block(Arena *arena, BasicBlock *block, Instr instr) {
    arena_array_append(arena, block->bytecode, instr);
    return instr.dest;
}


Operand add_instr_x_before_end_block(Arena *arena, BasicBlock *block, Instr instr) {
    Instr terminator = block->bytecode.items[block->bytecode.count - 1];
    arena_array_append(arena, block->bytecode, terminator);
    block->bytecode.items[block->bytecode.count - 2] = instr;
    return instr.dest;
}


void allocate_cfg(Arena *arena, CFGraph *cfg) {
    cfg->idom = arena_alloc(arena, sizeof(int) * cfg->count);
    cfg->rpo = arena_alloc(arena, sizeof(int) * cfg->count);
    cfg->rpo_list = arena_alloc(arena, sizeof(int) * cfg->count);
    cfg->live_in = arena_alloc(arena, sizeof(Set) * cfg->count),
    cfg->live_out = arena_alloc(arena, sizeof(Set) * cfg->count),
    cfg->uses = arena_alloc(arena, sizeof(Set) * cfg->count),
    cfg->defines = arena_alloc(arena, sizeof(Set) * cfg->count);
    cfg->df = arena_alloc(arena, sizeof(Set) * cfg->count);
    
    for (size_t i = 0; i < cfg->count; i++) {
        set_create(&cfg->df[i], arena, cfg->count);
    }
    
    memset(cfg->idom, -1, sizeof(int) * cfg->count);
    memset(cfg->rpo, -1, sizeof(int) * cfg->count);
    memset(cfg->rpo_list, -1, sizeof(int) * cfg->count);
}


bool dominates(CFGraph *cfg, int a, int b) { // does A dominate B
    while (b != (int)cfg->entry_block) {
        if (a == cfg->idom[b]) return true;
        b = cfg->idom[b];
    }
    return a == (int)cfg->entry_block;
}


int exit_for(CFGraph *cfg, int a) {
    int succ1 = cfg->items[a].successors[0];
    int succ2 = cfg->items[a].successors[1];
    if (succ1 == -1) return -1;
    return cfg->rpo[succ1] > cfg->rpo[succ2] ? succ1 : succ2;
}


void fill_rpo(CFGraph *cfg, int *index, int block) {
    if (block == -1 || cfg->rpo[block] != -1) return;

    cfg->rpo[block] = 0;
    
    fill_rpo(cfg, index, cfg->items[block].successors[1]);
    fill_rpo(cfg, index, cfg->items[block].successors[0]);

    (*index)++;
    cfg->rpo[block] = cfg->count - *index;
    cfg->rpo_list[cfg->count - *index] = block;
}


int intersection(int *dominates, int *rpo, int left, int right) {
     while (left != right) {
         while (rpo[left] < rpo[right]) right = dominates[right];
         while (rpo[left] > rpo[right]) left = dominates[left];
     }
     return right;
}


void fill_idom(CFGraph *cfg) {
    cfg->idom[cfg->entry_block] = cfg->entry_block;

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < cfg->count; i++) {
            int curr = cfg->rpo_list[i];
            int mdom = -1;
            for (size_t k = 0; k < cfg->items[curr].predecessors.count; k++) {
                int p = cfg->items[curr].predecessors.items[k];
                if (cfg->idom[p] == -1) continue;
                mdom = (mdom == -1) ? p :  intersection(cfg->idom, cfg->rpo, mdom, p);
            }
            if (mdom != -1 && cfg->idom[curr] != mdom) {
                cfg->idom[curr] = mdom;
                changed = true;
            }
        }
    }
}


void add_predecessor(CFGraph *cfg, int block, int with) {
    if (block == -1) return;

    bool add = (with != -1);
    for (size_t i = 0; i < cfg->items[block].predecessors.count; i++) {
        if (cfg->items[block].predecessors.items[i] == with) {
            return;
        }
    }
    if (add) array_append(cfg->items[block].predecessors, with);

    add_predecessor(cfg, cfg->items[block].successors[0], block);
    add_predecessor(cfg, cfg->items[block].successors[1], block);
}


void fill_predecessors(CFGraph *cfg) {
    add_predecessor(cfg, cfg->entry_block, -1);
}


void fill_df(CFGraph *cfg) {
    for (size_t i = 0; i < cfg->count; i++) {
        if (cfg->items[i].predecessors.count < 2) continue;
        
        for (size_t j = 0; j < cfg->items[i].predecessors.count; j++) {
            int runner = cfg->items[i].predecessors.items[j];
            
            while (runner != cfg->idom[i]) {
                set_insert(&cfg->df[runner], i);
                runner = cfg->idom[runner];
            }
            
        }
    }
}

