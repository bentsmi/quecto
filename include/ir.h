#ifndef IR_H
#define IR_H

#include "ast.h"
#include "common.h"
#include "symbol_table.h"

#define VREG_NONE -1

typedef enum {
    OPCODE_NONE,
    OPCODE_ADD,
    OPCODE_SUB,
    OPCODE_MUL,
    OPCODE_DIV,

    OPCODE_CMP_EQ,
    OPCODE_CMP_LT,
    OPCODE_CMP_GT,
    OPCODE_CMP_LEQ,
    OPCODE_CMP_GEQ,

    OPCODE_EXT_Z, // zero extend for low to high
    
    OPCODE_LOAD, // TODO: currently load and store operate on the stack.
               // will need to seperate stack loads from regular loads
    OPCODE_IMM,
    OPCODE_INDEX,
    OPCODE_ADDR,

    OPCODE_STORE,
    OPCODE_STORE_INDEX,
    OPCODE_COPY,

    OPCODE_CALL,
    OPCODE_ARG, // defines args for call
    OPCODE_PARAM, // defines entry for vregs

    OPCODE_JMP,
    OPCODE_BEQ,
    OPCODE_BLT,
    OPCODE_BGT,
    OPCODE_BGE,
    OPCODE_BLE,
    OPCODE_BNE,
    OPCODE_RET,

    OPCODE_COUNT,
} Opcode;

typedef enum {
    OPC_NONCOMM = 1,
    OPC_CLOBBERS_CALL = 2,
} OpcodeFlags;

typedef struct {
	int base; // vreg
	int index; // vreg
	int offset;
	int stride;
	int size;
} MemRef;


typedef enum {
    OPERAND_ANY = -1,
    OPERAND_NONE = 0,
    OPERAND_VREG,
    OPERAND_IMM,
    OPERAND_MEM,
    OPERAND_SLOT,
    OPERAND_BLOCK, // for jumps and stuff
    OPERAND_GLOBAL
} OperandType;

typedef enum {
    OPDR_NONE = 0,
    OPDR_USE,
    OPDR_DEF,
} OperandRole;


typedef struct {
    OperandRole dest, arg1, arg2;
    OpcodeFlags flags;
} OpcodeDesc;


extern const OpcodeDesc opcode_description_table[];


typedef struct {
    OperandType type;
    union {
        size_t vreg;
        size_t slot;
        size_t block;
        size_t imm;
        size_t glbl;
    		MemRef mem;
    };
} Operand;


typedef struct {
    Opcode opcode;
    Operand dest;
    Operand arg1;
    Operand arg2;
} Instr;


typedef struct {
    Instr *items;
    size_t count;
    size_t capacity;
} Bytecode;


typedef enum {
    PR_BASE_POINTER = 1,
    PR_STACK_POINTER = 2,
    PR_GENERAL_PURPOSE = 4,
    PR_CALLEE_SAVED = 8,
    PR_RETURN = 32,
    PR_ARG = 64,
} PhysRegFlags;


typedef struct {
    int index;
    PhysRegFlags flags;
} Color;


typedef struct {
    int bstart, istart, bend, iend; // NOTE: this bend, iend is a simplication for linear scans in the future may be replaced with a set of end based on cfg
} Interval;


typedef struct {
    int size;
    bool sign;
    Interval interval;
    Color color;
    PhysRegFlags hint;
    bool crosses_call;
    bool killed; // marked for death
} VregInfo;


typedef struct {
    VregInfo *items;
    size_t count;
    size_t capacity;
} VregInfoTable;


typedef struct {
    int size;
    bool sign;
    bool param;
    bool address_taken;
    bool killed; // marked for death
} SlotInfo;


typedef struct {
    SlotInfo *items;
    size_t count;
    size_t capacity;
} SlotTable;


typedef struct {
    int slot;
    Operand dest;
    Operand *args;
} Phi;

typedef struct {
    DYN_ARR(Phi) phis; // TODO: have DYN_ARR's declared globally so each implementation
    DYN_ARR(int) predecessors; // is not its own type.
    Bytecode bytecode;
    int successors[2];
} BasicBlock;


typedef struct {
    BasicBlock *items;
    int *idom, *rpo, *rpo_list; // indexed by item id; int because -1 is the unvisited sentinel
    Set *df, *live_in, *live_out, *uses, *defines; // live_in -> defines are vreg liveness related
    size_t count, capacity, entry_block;
} CFGraph;


typedef struct {
    CFGraph cfg;
    Bytecode flattened;
    VregInfoTable vregs;
    int *labels;
    SlotTable slots;
    Set saved_colors;
    const char *name;
} Procedure;


typedef struct {
    SymbolTable *symbols;
    Procedure *items;
    size_t count;
    size_t capacity;
} Program;

extern const char *opcode_to_string[OPCODE_COUNT];
extern const Opcode opposite_opcode_table[OPCODE_COUNT];

Operand allocate_vreg_explicit(Arena *arena, Procedure *procedure, VregInfo info);
VregInfo vregi_from_sloti(SlotInfo slot);

bool operand_has_vreg(Operand operand, size_t vreg);
int operand_collect_vregs(Operand opnd, size_t *out, int offset);

bool instr_match(Instr *instr, Opcode opcode, OperandType dest, OperandType arg1, OperandType arg2); // if arg is -1, matches anything
int instr_collect_used_vregs(Instr instr, size_t *out);
bool instr_uses_vreg(Instr instr, size_t vreg);
bool instr_defines_vreg(Instr instr, size_t vreg);
void instr_replace_vreg(Instr *instr, size_t find, size_t replace);

void print_procedure(Procedure proc);
void print_procedure_colored(Procedure *proc);
bool print_operand(Operand operand, bool leading);
void print_program(Program program, bool colored);
void print_cfg(CFGraph graph);
void print_block(CFGraph graph, bool walked[], int block);
void print_instruction(Instr instr);
void print_vregs(Procedure procedure);

#endif
