#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "common.h"


typedef enum {
    QUECTO_UNKNOWN, // used if type is inferred / program is invalid if something is this after analysis
    QUECTO_COMP_INT,
    QUECTO_U8,
    QUECTO_I8,
    QUECTO_U32,
    QUECTO_I32,
    QUECTO_ARRAY,
    QUECTO_POINTER,
    QUECTO_PROCEDURE,
    QUECTO_COUNT
} QuectoPrimitiveType;

typedef struct {
    struct QuectoType *param_types[MAX_PARAMS], *return_types[MAX_PARAMS];
    size_t param_count, return_count;
} ProcSignature;

typedef struct QuectoType {
    QuectoPrimitiveType type;
    union {
        struct {
            struct QuectoType *inner;
            size_t array_size;
        };
        ProcSignature *signature;
    };
} QuectoType;

typedef struct {
    int id;
    QuectoType *qtype;
    bool externed;
} SymbolData;

typedef struct SymbolTable {
    struct SymbolTable *prev;
    HashTable table;
} SymbolTable;

extern const char *symbol_type_to_string_table[];
extern const uint32_t quecto_primitive_width[QUECTO_COUNT];
extern QuectoType default_integer_type; // default integer type
extern QuectoType default_char_type; // default integer type

void print_symbol_table(SymbolTable *symbol_table, int indent);
SymbolData *insert_symbol(Arena *arena, SymbolTable *symbol_table, const char *symbol);
void *get_symbol(SymbolTable *symbol_table, const char *str);

bool quecto_types_equal(QuectoType *a, QuectoType *b);
bool quecto_is_primitive(QuectoType *a);
bool quecto_is_procedure(QuectoType *a);
bool quecto_is_array(QuectoType *a);
bool quecto_is_integer(QuectoType *a);
bool quecto_is_signed(QuectoType *a);
int quecto_type_size(QuectoType *a);
void print_type(QuectoType *type);
 
#endif
