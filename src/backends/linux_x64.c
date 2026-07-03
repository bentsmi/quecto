#include "backends/linux_x64.h"
#include "codegen.h"
#include "ir.h"
#include "symbol_table.h"
  
#ifdef __MACH__
#define EXIT_STATUS "0x2000001"
#define ENTRY_SYMBOL "_main"
#else
#define EXIT_STATUS "60"
#define ENTRY_SYMBOL "_start"
#endif


#define CONCAT_HELPER(a, b) a##b
#define CONCAT(a, b) CONCAT_HELPER(a, b)

#define STR_HELPER(X) #X
#define STR(X) STR_HELPER(X)

#define REG_NAME(SIZE) CONCAT(registers_, CONCAT(SIZE, bit))
#define VIRT_TO_PHYS_REG(SIZE, ARG) registers[regsize_from_bytes(SIZE)][iface->location.items[(ARG)].register_index]
#define X64_INSTRUCTION(name) void emit_x64_from_ir_##name(CodegenInterface *iface, Instr instr)

#define MREG(x) ((MachineOperand){.type = MOPERAND_REG, .reg = (x) })
#define MMEM(m) ((MachineOperand){.type = MOPERAND_MEM, .mem = (m) })
#define ASTK(o) ((MemAccess) { .base = x64_RBP, .offset = (o) })
#define AALL(b, s, i, o) ((MemAccess) { .base = (b), .stride = (s), .index = (i), .offset = (o) })
#define MIMM(i) ((MachineOperand){.type = MOPERAND_IMM, .imm = (i) })
#define MLBL(l) ((MachineOperand){.type = MOPERAND_LABEL, .label = (l) })
#define MINV  ((MachineOperand){.type = MOPERAND_INVALID})

#define EMIT(out, op, d, a1, a2) \
    array_append( (out), ((MachineInstr) {.instruction = (op), .dest = (d), .arg1 = (a1), .arg2 = (a2)}) )

#ifdef __MACH__
const char *mangled_format = "_%.*s";
#else
const char *mangled_format = "%.*s";
#endif


const char *x86_op_to_str[] = {
    [X64_ADD] = "add",
    [X64_SUB] = "sub",
    [X64_IMUL] = "imul",
    [X64_DIV] = "div",
    [X64_CMP] = "cmp",
    [X64_MOV] = "mov",
    [X64_MOVZ] = "movzx",
    [X64_RET] = "ret",
    [X64_JMP] = "jmp",
    [X64_JNE] = "jne",
    [X64_CALL] = "call",
    [X64_SETE] = "sete",
    [X64_SETL] = "setl",
    [X64_SETG] = "setg",
    [X64_SETLE] = "setle",
    [X64_SETGE] = "setge",
    [X64_JE] = "je",
    [X64_JL] = "jl",
    [X64_JG] = "jg",
    [X64_JLE] = "jle",
    [X64_JGE] = "jge",
    [X64_LEA] = "lea",
    [X64_PUSH] = "push",
    [X64_POP] = "pop",
    [X64_LABEL] = "",
};

const char *x86_reg_to_str[] = {
    [x64_AL] = "al",
    [x64_BL] = "bl",
    [x64_CL] = "cl",
    [x64_DL] = "dl",
    [x64_SIL] = "sil",
    [x64_DIL] = "dil",
    [x64_BPL] = "bpl",
    [x64_SPL] = "spl",
    [x64_R8B] = "r8b",
    [x64_R9B] = "r9b",
    [x64_R10B] = "r10b",
    [x64_R11B] = "r11b",
    [x64_R12B] = "r12b",
    [x64_R13B] = "r13b",
    [x64_R14B] = "r14b",
    [x64_R15B] = "r15b",
  
    [x64_AX] = "ax",
    [x64_BX] = "bx",
    [x64_CX] = "cx",
    [x64_DX] = "dx",
    [x64_SI] = "si",
    [x64_DI] = "di",
    [x64_BP] = "bp",
    [x64_SP] = "sp",
    [x64_R8W] = "r8d",
    [x64_R9W] = "r9d",
    [x64_R10W] = "r10w",
    [x64_R11W] = "r11w",
    [x64_R12W] = "r12w",
    [x64_R13W] = "r13w",
    [x64_R14W] = "r14w",
    [x64_R15W] = "r15w",

    [x64_EAX] = "eax",
    [x64_EBX] = "ebx",
    [x64_ECX] = "ecx",
    [x64_EDX] = "edx",
    [x64_ESI] = "esi",
    [x64_EDI] = "edi",
    [x64_EBP] = "ebp",
    [x64_ESP] = "esp",
    [x64_R8D] = "r8d",
    [x64_R9D] = "r9d",
    [x64_R10D] = "r10d",
    [x64_R11D] = "r11d",
    [x64_R12D] = "r12d",
    [x64_R13D] = "r13d",
    [x64_R14D] = "r14d",
    [x64_R15D] = "r15d",    
    
    [x64_RAX] = "rax",
    [x64_RBX] = "rbx",
    [x64_RCX] = "rcx",
    [x64_RDX] = "rdx",
    [x64_RSI] = "rsi",
    [x64_RDI] = "rdi",
    [x64_RBP] = "rbp",
    [x64_RSP] = "rsp",
    [x64_R8] = "r8",
    [x64_R9] = "r9",
    [x64_R10] = "r10",
    [x64_R11] = "r11",
    [x64_R12] = "r12",
    [x64_R13] = "r13",
    [x64_R14] = "r14",
    [x64_R15] = "r15",
};

x64_Register registers[4][16] = {
    [0] = { x64_AL, x64_BL, x64_CL, x64_DL, x64_SIL, x64_DIL, x64_BPL, x64_SPL, x64_R8B, x64_R9B, x64_R10B, x64_R11B, x64_R12B, x64_R13B, x64_R14B, x64_R15B },
    [1] = { x64_AX, x64_BX, x64_CX, x64_DX, x64_SI, x64_DI, x64_BP, x64_SP, x64_R8W, x64_R9W, x64_R10W, x64_R11W, x64_R12W, x64_R13W, x64_R14W, x64_R15W },
    [2] = { x64_EAX, x64_EBX, x64_ECX, x64_EDX, x64_ESI, x64_EDI, x64_EBP, x64_ESP, x64_R8D, x64_R9D, x64_R10D, x64_R11D, x64_R12D, x64_R13D, x64_R14D, x64_R15D },
    [3] = { x64_RAX, x64_RBX, x64_RCX, x64_RDX, x64_RSI, x64_RDI, x64_RBP, x64_RSP, x64_R8, x64_R9, x64_R10, x64_R11, x64_R12, x64_R13, x64_R14, x64_R15 }
};

x64_Register arg_registers[4][4] = {
    [0] = { x64_DIL, x64_SIL, x64_DL,  x64_CL },
    [1] = { x64_DI,  x64_SI,  x64_DX,  x64_CX},
    [2] = { x64_EDI, x64_ESI, x64_EDX, x64_ECX },
    [3] = { x64_RDI, x64_RSI, x64_RDX, x64_RCX }
};

x64_Register ret_registers[4][1] = {
    [0] = { x64_AL },
    [1] = { x64_AX },
    [2] = { x64_EAX },
    [3] = { x64_RAX },
};


const X64_Opcode jmpCC_from_ir[OPCODE_COUNT] = {
    [OPCODE_BNE] = X64_JNE,
    [OPCODE_BEQ] = X64_JE,
    [OPCODE_BLT] = X64_JL,
    [OPCODE_BGT] = X64_JG,
    [OPCODE_BLE] = X64_JLE,
    [OPCODE_BGE] = X64_JGE,
};

const X64_Opcode setCC_from_ir[OPCODE_COUNT] = {
    [OPCODE_CMP_EQ] = X64_SETE,
    [OPCODE_CMP_LT] = X64_SETL,
    [OPCODE_CMP_GT] = X64_SETG,
    [OPCODE_CMP_LEQ] = X64_SETLE,
    [OPCODE_CMP_GEQ] = X64_SETGE,
};


inline int regsize_from_bytes(int bytes) {
  if (bytes <= 1) return 0;
  else if (bytes <= 2) return 1;
  else if (bytes <= 4) return 2;
  else if (bytes <= 8) return 3;
  else {
    printf("bytes too big %d\n", bytes);
    return -1;
  }
}


bool fprint_machine_operand(FILE *out, MachineOperand operand, bool leading) {
    switch (operand.type) {
    case MOPERAND_IMM:
        fprintf(out, "%d", operand.imm);
        break;
    case MOPERAND_REG:
        fprintf(out, "%s", x86_reg_to_str[operand.reg]);
        break;
    case MOPERAND_MEM:
        fprintf(out, "[");
        fprintf(out, "%s", x86_reg_to_str[operand.mem.base]);
        if (operand.mem.stride != 0)
            fprintf(out, " + %d * %s", operand.mem.stride, x86_reg_to_str[operand.mem.index]);
        if (operand.mem.offset < 0) fprintf(out, " + ");
        else if (operand.mem.offset > 0) fprintf(out, " - ");
        if (operand.mem.offset != 0)
            fprintf(out, "%d", abs(operand.mem.offset));
        fprintf(out, "]");
        break;
    case MOPERAND_LABEL:
          fprintf(out, "%s", operand.label);
        break;
    case MOPERAND_INVALID:
        return false;
  }
  
  if (leading) fprintf(out, ", ");
  
  return true;
}


void fprint_x64_machine_code(FILE *out, MachineCode *code) {
    for (int i = 0; i < code->count; i++) {
        MachineInstr instr = code->items[i];
        if (instr.instruction == X64_LABEL) {
            fprintf(out, "%s:\n", instr.dest.label);
            continue;
        }
        fprintf(out, "\t%s\t", x86_op_to_str[instr.instruction]);
        fprint_machine_operand(out, instr.dest, instr.arg1.type != MOPERAND_INVALID);
        fprint_machine_operand(out, instr.arg1, instr.arg2.type != MOPERAND_INVALID);
        fprint_machine_operand(out, instr.arg2, false);
        fprintf(out, "\n");
    }
}

inline x64_Register select_register(VregInfo info) { // from 0-7
    assert(info.color.index < 16);
    return registers[regsize_from_bytes(info.size)][info.color.index];
}

inline x64_Register addr_register(VregInfo info) {
    return registers[3][info.color.index];
}

inline MachineOperand select_stack(CodegenInterface *iface, int slot) {
    return MMEM(ASTK(iface->stack_from_slot[slot]));
}

void emit_x64_prologue(CodegenInterface *iface, Procedure *procedure) {
  EMIT(iface->output, X64_PUSH, MINV, MREG(x64_RBP), MINV);


  EMIT(iface->output, X64_MOV, MREG(x64_RBP), MREG(x64_RSP), MINV);
  
  for (size_t i = 0; i < procedure->saved_colors.bit_count; i++) {
    if (set_has(&procedure->saved_colors, i)) EMIT(iface->output, X64_PUSH, MINV, MREG(registers[3][i]), MINV);
  }
  
  if (iface->stackframe > 0) {
    EMIT(iface->output, X64_SUB, MREG(x64_RSP), MIMM(((iface->stackframe / 16 + 1) * 16)), MINV);
  }
}


void emit_x64_epilogue(CodegenInterface *iface, Procedure *procedure) {
    char name_end[256];
    snprintf(name_end, 256, ".end");

    EMIT(iface->output, X64_LABEL, MLBL(strdup(name_end)), MINV, MINV);

    for (size_t i = 0; i < procedure->saved_colors.bit_count; i++) {
        if (set_has(&procedure->saved_colors, i))
            EMIT(iface->output, X64_POP, MINV, MREG(registers[3][i]), MINV);
    }


    if (iface->stackframe > 0) {
        EMIT(iface->output, X64_ADD, MREG(x64_RSP), MIMM(((iface->stackframe / 16 + 1) * 16)), MINV);
    }


    EMIT(iface->output, X64_POP, MINV, MREG(x64_RBP), MINV);
    EMIT(iface->output, X64_RET, MINV, MINV, MINV);
}


X64_INSTRUCTION(add) {
    x64_Register dest = select_register(iface->vregs->items[instr.dest.vreg]);
    x64_Register arg1 = select_register(iface->vregs->items[instr.arg1.vreg]);
    x64_Register arg2 = select_register(iface->vregs->items[instr.arg2.vreg]);

    if (dest == arg1) {
        EMIT(iface->output, X64_ADD,
            MREG(arg1),
            MREG(arg2),
            MINV
        );
    } else if (dest == arg2) {
        EMIT(iface->output, X64_ADD,
            MREG(arg2),
            MREG(arg1),
            MINV
        );
    } else {
        EMIT(iface->output, X64_MOV,
            MREG(dest),
            MREG(arg1),
            MINV
        );
        EMIT(iface->output, X64_ADD,
            MREG(dest),
            MREG(arg2),
            MINV
        );
    }
}


X64_INSTRUCTION(sub) {
    x64_Register dest = select_register(iface->vregs->items[instr.dest.vreg]);
    x64_Register arg1 = select_register(iface->vregs->items[instr.arg1.vreg]);
    x64_Register arg2 = select_register(iface->vregs->items[instr.arg2.vreg]);

    if (dest == arg1) {
        EMIT(iface->output, X64_SUB,
            MREG(arg1),
            MREG(arg2),
            MINV
        );
    } else if (dest == arg2) {
        EMIT(iface->output, X64_SUB,
            MREG(arg2),
            MREG(arg1),
            MINV
        );
    } else {
        EMIT(iface->output, X64_MOV,
            MREG(dest),
            MREG(arg1),
            MINV
        );
        EMIT(iface->output, X64_SUB,
            MREG(dest),
            MREG(arg2),
            MINV
        );
    }

}


X64_INSTRUCTION(mul) {
    x64_Register dest = select_register(iface->vregs->items[instr.dest.vreg]);
    x64_Register arg1 = select_register(iface->vregs->items[instr.arg1.vreg]);
    x64_Register arg2 = select_register(iface->vregs->items[instr.arg2.vreg]);

        if (dest == arg1) {
        EMIT(iface->output, X64_IMUL,
            MREG(arg1),
            MREG(arg2),
            MINV
        );
    } else if (dest == arg2) {
        EMIT(iface->output, X64_IMUL,
            MREG(arg2),
            MREG(arg1),
            MINV
        );
    } else {
        EMIT(iface->output, X64_MOV,
            MREG(dest),
            MREG(arg1),
            MINV
        );
        EMIT(iface->output, X64_IMUL,
            MREG(dest),
            MREG(arg2),
            MINV
        );
    }

}


X64_INSTRUCTION(load) {
  EMIT(iface->output, X64_MOV,
        MREG(select_register(iface->vregs->items[instr.dest.vreg])),
        select_stack(iface, instr.arg1.slot),
        MINV);
}

X64_INSTRUCTION(load_index) {
  // EMIT(iface->output, X64_MOV,
        // MREG(VIRT_TO_PHYS_REG(iface->vreg_info.items[instr.dest.vreg].size, instr.dest.vreg)),
        // MSTK_IND(instr.arg1.stack_offset, VIRT_TO_PHYS_REG(8, instr.arg1.index), instr.arg1.size), MINV);
    EMIT(iface->output, X64_MOV,
        MREG(select_register(iface->vregs->items[instr.dest.vreg])),
        MMEM(AALL(addr_register(iface->vregs->items[instr.arg1.mem.base]),
                  instr.arg1.mem.stride,
                  addr_register(iface->vregs->items[instr.arg1.mem.index]),
                  instr.arg1.mem.offset)),
        MINV
    );
}

X64_INSTRUCTION(load_addr) {
    
    EMIT(iface->output, X64_LEA,
        MREG(select_register(iface->vregs->items[instr.dest.vreg])),
        instr.arg1.type == OPERAND_SLOT ? select_stack(iface, instr.arg1.slot) :
                MMEM(AALL(addr_register(iface->vregs->items[instr.arg1.mem.base]),
                    instr.arg1.mem.stride,
                    addr_register(iface->vregs->items[instr.arg1.mem.index]),
                    instr.arg1.mem.offset)),
        MINV);
}

X64_INSTRUCTION(store) {
    if (instr.dest.type == OPERAND_SLOT) {
        EMIT(iface->output, X64_MOV,
                select_stack(iface, instr.dest.slot),//MMEM(AALL(select_register(iface->vregs->items[instr.dest.mem.base]), 0, 0, instr.dest.mem.offset)),
                MREG(select_register(iface->vregs->items[instr.arg1.vreg])),
                MINV
            );
    }
}

X64_INSTRUCTION(store_index) {
    if (instr.dest.type == OPERAND_MEM) {
        EMIT(iface->output, X64_MOV,
                MMEM(AALL(addr_register(iface->vregs->items[instr.dest.mem.base]),
                        instr.dest.mem.stride,
                        addr_register(iface->vregs->items[instr.dest.mem.index]),
                        instr.dest.mem.offset)),
                MREG(select_register(iface->vregs->items[instr.arg1.vreg])),
                MINV
             );
    }
}

X64_INSTRUCTION(loadi) {
    EMIT(iface->output, X64_MOV,
        MREG(select_register(iface->vregs->items[instr.dest.vreg])),
        MIMM(instr.arg1.imm),
        MINV);
}


void emit_copy_if_not_same(CodegenInterface *iface, x64_Register dest, x64_Register arg) {
    if (dest == arg)
        return;
    
    EMIT(iface->output, X64_MOV,
         MREG(dest),
         MREG(arg),
         MINV
    );
}

X64_INSTRUCTION(ext_zero) {
    EMIT(iface->output, X64_MOVZ,
        MREG(select_register(iface->vregs->items[instr.dest.vreg])),
        MREG(select_register(iface->vregs->items[instr.arg1.vreg])),
        MINV
    );
}

X64_INSTRUCTION(copy) {
    if (select_register(iface->vregs->items[instr.dest.vreg]) == select_register(iface->vregs->items[instr.arg1.vreg]))
        return;
    
    EMIT(iface->output, X64_MOV,
         MREG(select_register(iface->vregs->items[instr.dest.vreg])),
         MREG(select_register(iface->vregs->items[instr.arg1.vreg])),
         MINV);
}


X64_INSTRUCTION(ret) {
    emit_copy_if_not_same(iface,
        ret_registers[regsize_from_bytes(iface->vregs->items[instr.arg1.vreg].size)][0],
        select_register(iface->vregs->items[instr.arg1.vreg])
    );
    EMIT(iface->output, X64_JMP, MLBL(".end"), MINV, MINV); // goes to epilogue (works for nasm)
}


X64_INSTRUCTION(jmp) {
    char *buf = malloc(16);
    snprintf(buf, 16, ".L_BLK%zu", instr.dest.block);
    EMIT(iface->output, X64_JMP,
        MLBL(buf),
        MINV,
        MINV);
}


X64_INSTRUCTION(jmpCC) {
    EMIT(iface->output, X64_CMP,
        MINV,
        MREG(select_register(iface->vregs->items[instr.arg1.vreg])),
        MREG(select_register(iface->vregs->items[instr.arg2.vreg])));
    char *buf = malloc(16);
    snprintf(buf, 16, ".L_BLK%zu", instr.dest.block);
    EMIT(iface->output, jmpCC_from_ir[instr.opcode],
        MLBL(buf),
        MINV,
        MINV);
}


X64_INSTRUCTION(cmpCC) {
  EMIT(iface->output, X64_CMP,
        MINV,
        MREG(select_register(iface->vregs->items[instr.arg1.vreg])),
        MREG(select_register(iface->vregs->items[instr.arg2.vreg])));

  EMIT(iface->output, setCC_from_ir[instr.opcode],
        MREG(select_register(iface->vregs->items[instr.dest.vreg])),
        MINV,
        MINV);
}


X64_INSTRUCTION(call) {
    for (int j = 0; j < iface->arg_count; j++) {
        int size = iface->vregs->items[iface->args[j]].size;
        emit_copy_if_not_same(iface,
            arg_registers[regsize_from_bytes(size)][j],
            select_register(iface->vregs->items[iface->args[j]])
        );
    }

    Key global_name = iface->globals->table.keys[instr.arg1.glbl];
    SymbolData *global_data = iface->globals->table.items[instr.arg1.glbl];
    char *name = arena_alloc(iface->scratch, (global_name.size + 8) * sizeof(char));
    snprintf(name, global_name.size + 8, global_data->externed ? mangled_format : "%.*s", global_name.size, global_name.data);
    
    iface->arg_count = 0;
    EMIT(iface->output, X64_CALL, MLBL(name), MINV, MINV);

    emit_copy_if_not_same(iface,
        select_register(iface->vregs->items[instr.dest.vreg]),
        ret_registers[regsize_from_bytes(iface->vregs->items[instr.dest.vreg].size)][0]
    );
}


X64_INSTRUCTION(param) {
    emit_copy_if_not_same(iface,
        select_register(iface->vregs->items[instr.dest.vreg]),
        arg_registers[regsize_from_bytes(iface->vregs->items[instr.dest.vreg].size)][instr.arg1.imm]
    );
}


X64_INSTRUCTION(arg) {
  iface->args[iface->arg_count++] = instr.arg1.vreg;
}


void emit_entry(FILE *out, Program *program) {
    (void)program;
    fprintf(out, "section\t.text\n");
    fprintf(out, ENTRY_SYMBOL ":\n");
    fprintf(out, "\tcall\tmain\n");
    fprintf(out, "\tmov\tedi, eax\n");
    fprintf(out, "\tmov\teax, " EXIT_STATUS "\n");
    fprintf(out, "\tsyscall\n\n");
}


void emit_mangled_symbol(FILE *out, const char *name, size_t len) {
    fprintf(out, mangled_format, len, name);
}


void emit_symbols(FILE *out, Program *program) {
    fprintf(out, "global\t" ENTRY_SYMBOL "\n");
    for (size_t i = 0; i < program->symbols->table.capacity; i++) {
        if (program->symbols->table.keys[i].size != 0) {
            SymbolData *symbol = program->symbols->table.items[i];
            if (symbol->externed) {
                fprintf(out, "extern ");
                emit_mangled_symbol(out, program->symbols->table.keys[i].data, program->symbols->table.keys[i].size);
                fprintf(out, "\n");
            }
        }
    }
    fprintf(out, "\n");
}


void calculate_offsets(CodegenInterface *iface) {
    iface->stack_from_slot = calloc(iface->slots->count, sizeof(*iface->stack_from_slot));
    for (size_t i = 0; i < iface->slots->count; i++) {
        SlotInfo slot = iface->slots->items[i];
        if (!slot.killed && !slot.param) {
            iface->stackframe += slot.size;
            iface->stack_from_slot[i] = iface->stackframe;
        }
    } 
}


CodegenBackend LINUX_X86_64_BACKEND = (CodegenBackend) {
  // .adhere_bytecode_to_spec = &adhere_bytecode_to_machine_spec,
  .print_machine_code = &fprint_x64_machine_code,
  .calculate_offsets = &calculate_offsets,
  .emit_entry = &emit_entry,
  .emit_symbols = &emit_symbols,
  .emit_epilogue = &emit_x64_epilogue,
  .emit_prologue = &emit_x64_prologue,
  .emit_machine_code_from =  {
    [OPCODE_ADD] = &emit_x64_from_ir_add,
    [OPCODE_SUB] = &emit_x64_from_ir_sub,
    [OPCODE_MUL] = &emit_x64_from_ir_mul,
    [OPCODE_DIV] = NULL,//&emit_x64_from_ir_div,
    [OPCODE_CMP_EQ] = &emit_x64_from_ir_cmpCC,
    [OPCODE_CMP_LT] = &emit_x64_from_ir_cmpCC,
    [OPCODE_CMP_GT] =  &emit_x64_from_ir_cmpCC,
    [OPCODE_CMP_LEQ] = &emit_x64_from_ir_cmpCC,
    [OPCODE_CMP_GEQ] = &emit_x64_from_ir_cmpCC,
    [OPCODE_LOAD] = &emit_x64_from_ir_load,
    [OPCODE_EXT_Z] = &emit_x64_from_ir_ext_zero,
    [OPCODE_INDEX] = &emit_x64_from_ir_load_index,//&emit_x64_from_ir_,
    [OPCODE_ADDR] = &emit_x64_from_ir_load_addr,
    [OPCODE_STORE] = &emit_x64_from_ir_store,
    [OPCODE_STORE_INDEX] = &emit_x64_from_ir_store_index,
    [OPCODE_COPY] = &emit_x64_from_ir_copy,
    [OPCODE_IMM] = &emit_x64_from_ir_loadi,
    [OPCODE_RET] = &emit_x64_from_ir_ret,
    [OPCODE_JMP] = &emit_x64_from_ir_jmp,
    [OPCODE_BEQ] = &emit_x64_from_ir_jmpCC,
    [OPCODE_BNE] = &emit_x64_from_ir_jmpCC,
    [OPCODE_BLT]  = &emit_x64_from_ir_jmpCC,
    [OPCODE_BGT]  = &emit_x64_from_ir_jmpCC,
    [OPCODE_BLE]  = &emit_x64_from_ir_jmpCC,
    [OPCODE_BGE]  = &emit_x64_from_ir_jmpCC,
    [OPCODE_CALL] = &emit_x64_from_ir_call,
    [OPCODE_PARAM] = &emit_x64_from_ir_param,
    [OPCODE_ARG] = &emit_x64_from_ir_arg,
  },
};
