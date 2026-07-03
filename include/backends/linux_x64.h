#include "../codegen.h"

#ifndef LINUX_X64_H
#define LINUX_X64_H

typedef enum {
    X64_LABEL = LABEL_OPCODE, // for now every label should have 0 as the opcode for all backends
    X64_ADD,
    X64_SUB,
    X64_IMUL,
    X64_DIV,
    X64_CMP,
    X64_MOV,
    X64_MOVZ,
    X64_RET,
    X64_JMP,
    X64_JNE,
    X64_JE,
    X64_JL,
    X64_JG,
    X64_JLE,
    X64_JGE,
    X64_CALL,
    X64_SETE,
    X64_SETL,
    X64_SETG,
    X64_SETLE,
    X64_SETGE,
    X64_LEA,
    X64_PUSH,
    X64_POP,
} X64_Opcode;

typedef enum {
    x64_AL,
    x64_BL,
    x64_CL,
    x64_DL,
    x64_SIL,
    x64_DIL,
    x64_BPL,
    x64_SPL,
    x64_R8B,
    x64_R9B,
    x64_R10B,
    x64_R11B,
    x64_R12B,
    x64_R13B,
    x64_R14B,
    x64_R15B,

    x64_AX,
    x64_BX,
    x64_CX,
    x64_DX,
    x64_SI,
    x64_DI,
    x64_BP,
    x64_SP,
    x64_R8W,
    x64_R9W,
    x64_R10W,
    x64_R11W,
    x64_R12W,
    x64_R13W,
    x64_R14W,
    x64_R15W,

    x64_EAX,
    x64_EBX,
    x64_ECX,
    x64_EDX,
    x64_ESI,
    x64_EDI,
    x64_EBP,
    x64_ESP,
    x64_R8D,
    x64_R9D,
    x64_R10D,
    x64_R11D,
    x64_R12D,
    x64_R13D,
    x64_R14D,
    x64_R15D,

    x64_RAX,
    x64_RBX,
    x64_RCX,
    x64_RDX,
    x64_RSI,
    x64_RDI,
    x64_RBP,
    x64_RSP,
    x64_R8,
    x64_R9,
    x64_R10,
    x64_R11,
    x64_R12,
    x64_R13,
    x64_R14,
    x64_R15,
} x64_Register;

int regsize_from_bytes(int bits);
x64_Register select_register(VregInfo info);
x64_Register addr_register(VregInfo info);
MachineOperand select_stack(CodegenInterface *iface, int slot);

extern CodegenBackend LINUX_X86_64_BACKEND;

#endif
