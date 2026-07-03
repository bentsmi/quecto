#ifndef CFG_H
#define CFG_H

#include "ir.h"

void allocate_cfg(Arena *arena, CFGraph *cfg);
Operand emit_instr_into_block(Arena *arena, BasicBlock *block, Instr instr);
Operand add_instr_x_before_end_block(Arena *arena, BasicBlock *block, Instr instr);

// TODO: add a typedef and explicit sentinels for the block IDs

void fill_rpo(CFGraph *cfg, int *index, int block);
void fill_idom(CFGraph *cfg);
void fill_predecessors(CFGraph *cfg);
void fill_df(CFGraph *cfg);
void add_predecessor(CFGraph *cfg, int, int);

bool dominates(CFGraph *cfg, int a, int b);
int exit_for(CFGraph *cfg, int a);

#endif
