#include <stdio.h>

#include "symbol_table.h"
#include "common.h"

const uint32_t quecto_primitive_width[] = {
    [QUECTO_COMP_INT] = -1, // depends on container or other known types in an expression
    [QUECTO_I8] = 1,
    [QUECTO_U8] = 1,
    [QUECTO_I32] = 4,
    [QUECTO_U32] = 4,
    [QUECTO_ARRAY] = -1,
    [QUECTO_POINTER] = -1,
    [QUECTO_UNKNOWN] = -1,
};

QuectoType default_integer_type = { .type = QUECTO_U32 };
QuectoType default_char_type = { .type = QUECTO_I8 };

SymbolData *insert_symbol(Arena *arena, SymbolTable *symbol_table, const char *symbol) {
    HashTable *table = &symbol_table->table;

    SymbolData *data = arena_alloc_type(arena, SymbolData);

    ht_insert(table, symbol, data);
    return data;
}

void *get_symbol(SymbolTable *symbol_table, const char *str) {
    SymbolTable *table = symbol_table;
    while (table != NULL) {
        void *result = ht_search(&table->table, str);
        if (result != NULL) return result;
        table = table->prev;
    }
    return NULL;
}


bool quecto_types_equal(QuectoType *a, QuectoType *b) {
    if (a == b) return true; // for interned values
    
    if ((a && b) && ((a->type == b->type) || (quecto_is_integer(a) && b->type == QUECTO_COMP_INT))) {
        if (a->inner != NULL && b->inner != NULL) {
            return quecto_types_equal(a->inner, b->inner);
        }
        return true;
    }
    return false;
}


bool quecto_is_integer(QuectoType *a) {
    switch (a->type) {
        case QUECTO_I32:
        case QUECTO_U32:
        case QUECTO_I8:
        case QUECTO_U8:
            return true;
        default:
            return false;
    }
}


bool quecto_is_procedure(QuectoType *a) {
    return a->type == QUECTO_PROCEDURE;
}


bool quecto_is_array(QuectoType *a) {
    return a->type == QUECTO_ARRAY;
}


bool quecto_is_primitive(QuectoType *a) {
    switch (a->type) {
        case QUECTO_I32:
        case QUECTO_U32:
        case QUECTO_I8:
        case QUECTO_U8:
            return true;
        default:
            return false;
    }
}


int quecto_type_size(QuectoType *a) {
    if (quecto_is_primitive(a)) {
        return quecto_primitive_width[a->type];
    } else {
         switch (a->type) {
            case QUECTO_ARRAY:
                 return quecto_type_size(a->inner) * a->array_size;
            case QUECTO_POINTER:
                return 8; // should depend on system
            default:
                UNREACHABLE("invalid");
         }   
    }
    return -1;
}


bool quecto_is_signed(QuectoType *a) {
    if (quecto_is_primitive(a)) {
        switch (a->type) {
            case QUECTO_I32:
            case QUECTO_I8:
                return true;
            default:
                return false;
        }
    } else {
        switch (a->type) {
            case QUECTO_ARRAY:
                return quecto_is_signed(a->inner);
            case QUECTO_POINTER:
                return false;
            default:
                UNREACHABLE("invalid");
                return false;
        }
    }
}


void print_symbol_table(SymbolTable *symbol_table, int indent) {
    for (size_t i = 0; i < symbol_table->table.capacity; i++) {
        if (symbol_table->table.keys[i].data == NULL) continue;

        SymbolData *data = (SymbolData*) symbol_table->table.items[i];

        print_indent(indent, "\"%.*s\":\t",
                     (int)symbol_table->table.keys[i].size,
                     (char*)symbol_table->table.keys[i].data
                 );
        print_type(data->qtype);
        printf("\n");
    }
}


void print_function_signature(ProcSignature *signature) {
    printf("(");
    for (size_t i = 0; i < signature->param_count; i++) {
        print_type(signature->param_types[i]);
        if (i == signature->param_count) printf(", ");
    }
    printf(") -> (");
    for (size_t i = 0; i < signature->return_count; i++) {
        print_type(signature->return_types[i]);
        if (i == signature->return_count) printf(", ");
    }
    printf(")");
}

void print_type(QuectoType *qtype) {
    if (qtype == NULL) {
        printf("(null)");
        return;
    }
    switch(qtype->type) {
        case QUECTO_UNKNOWN:     printf("unknown"); break;
        case QUECTO_I32:         printf("i32"); break;
        case QUECTO_U32:         printf("u32"); break;
        case QUECTO_I8:          printf("i8"); break;
        case QUECTO_U8:          printf("u8"); break;
        case QUECTO_COMP_INT:    printf("comptime_int"); break;
        case QUECTO_ARRAY:
            printf("[");
            print_type(qtype->inner);
            printf("; %lu]", qtype->array_size);
            break;
        case QUECTO_POINTER:
            printf("^");
            print_type(qtype->inner);
            break;
        case QUECTO_PROCEDURE: print_function_signature(qtype->signature); break;
        default:
            UNREACHABLE("Non-existent Quecto Type");
            break;
    }
}
