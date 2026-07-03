// include "ast.h"
// 
#include "codegen.h"
#include "common.h"
#include "ir.h"
#include "symbol_table.h"

#include <stdio.h>

#define MLBL(l) ((MachineOperand){.type = MOPERAND_LABEL, .label = (l) })

MachineCode emit_procedure_with(CodegenBackend *backend, Arena *scratch, Procedure procedure, SymbolTable *globals) {
    CodegenInterface iface = {
        .output = {0},
        .scratch = scratch,
        .vregs = &procedure.vregs,
        .slots = &procedure.slots,
        .globals = globals,
        .arg_count = 0,
    };

    backend->calculate_offsets(&iface);
    backend->emit_prologue(&iface, &procedure);

    for (size_t i = 0; i < procedure.cfg.count; i++) {
        int b = procedure.cfg.rpo_list[i];
        BasicBlock *block = &procedure.cfg.items[b];
        
        char *buf = arena_alloc(scratch, 16);
        snprintf(buf, 16, "\t.L_BLK%d", b);
        MachineInstr instr = (MachineInstr) { .instruction = LABEL_OPCODE, .dest = MLBL(buf)};
        array_append(iface.output, instr);

        for (size_t j = 0; j < block->bytecode.count; j++) {
            Instr instr = block->bytecode.items[j];

            switch (instr.opcode) {
                case OPCODE_JMP:
                    if (procedure.cfg.idom[block->successors[0]] == b)
                        continue;
                    instr.dest = (Operand) { .type = OPERAND_BLOCK, .block = block->successors[0] };
                    break;
                case OPCODE_RET:
                    break;
                case OPCODE_BEQ:
                case OPCODE_BNE:
                case OPCODE_BLT:
                case OPCODE_BGT:
                case OPCODE_BLE:
                case OPCODE_BGE:
                    instr.dest = (Operand) { .type = OPERAND_BLOCK, .block = block->successors[1] };
                    instr.opcode = opposite_opcode_table[instr.opcode];
                    break;
                default:
                    break;
            }
            
            EmitFn fn = backend->emit_machine_code_from[instr.opcode];
            assert(fn != NULL && "no emit handler for opcode");
            fn(&iface, instr);
        }
    }

    backend->emit_epilogue(&iface, &procedure);
    return iface.output;
}


void compile_program_with(FILE *out, Arena *scratch, CodegenBackend *backend, Program *program) {
    backend->emit_symbols(out, program);
    backend->emit_entry(out, program);

    for (size_t i = 0; i < program->count; i++) {
        fprintf(out, "%s:\n", program->items[i].name);
        MachineCode code = emit_procedure_with(backend, scratch, program->items[i], program->symbols);
        backend->print_machine_code(out, &code);
    }
}
