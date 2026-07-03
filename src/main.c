#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "ir.h"
#include "symbol_table.h"
#include "tokenizer.h"
#include "parser.h"
#include "ast.h"
#include "analysis.h"
#include "emission.h"
#include "codegen.h"
#include "passes.h"
#include "error.h"
#include "backends/linux_x64.h"


void help_prompt(void);
void unrecognized_prompt(void);
int compile(const char *filename);

int main(int argc, char **argv) { // quecto <command> <input> {flags}
    if (argc == 1) {
        help_prompt();
    } else if (argc >= 2) {
        int current_arg = 1;
        if (strncmp(argv[current_arg++], "build", sizeof("build")) == 0) {
            if (current_arg >= argc) {
                printf("no file supplied.\n");
                return -1;
            }
            char *filename = argv[current_arg];
            if (compile(filename) == 0) {
                printf("compiled successfully.\n");
            } else {
                printf("error\n");
            }
        } else if (strncmp(argv[1], "help", sizeof("help")) == 0) {
            help_prompt();
        } else {
            unrecognized_prompt();
        }
    }   
}

void help_prompt(void) {
    printf("info: Usage: quecto [command] [options]\n");
    printf("List of Commands:\n");
    printf("\tbuild {file}\t\tBuilds the file.\n");
    printf("\thelp\t\tShows this prompt.\n");
}

void unrecognized_prompt(void) {
    printf("unrecognized command\n");
}

int compile(const char *filename) {
    FILE *f = fopen(filename, "r");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    src = malloc(fsize + 1);
    fread(src, 1, fsize, f);
    fclose(f);

    src[fsize] = '\0';
    src_size = fsize;

    Arena backing = {0};
    Arena scratch = {0};
    Arenas arena = {
        .scratch = &scratch,
        .persistent = &backing
    };

    arena_create(&backing, 1024 * 2048);
    arena_create(&scratch, 1024 * 1024);
    
    TokenArray tokens = tokenize(&arena, src, src_size);
    if (error) return -1;

    HashTable types = {0}; // intern table


    ParserState parser = {
        .tokens = tokens,
        .arena = &backing,
        .type_intern_table = &types,
    };

    AST *ast = parse_program(&parser);

    if (error) return -1;

    SymbolTable symbols = { 0 };

    AnalysisContext analysis_ctx = (AnalysisContext) {
        .arena = &backing,
        .type_intern_table = &types,
        .symbol_table = &symbols,
    };

    analyze_ast(&analysis_ctx, ast);

    EmitContext emit_ctx = (EmitContext) {
        .scope = &symbols,
        .arena = &backing,
    };

    Passes passes = (PassFn[]) {
            pass_insert_phis,
            pass_rename,
            pass_kill_slots,
            pass_remove_copies,
            pass_liveness,
            pass_compute_liveness,
            pass_crosses_call,
            pass_precoloring,
            pass_color_cfg,
            pass_phis_into_copies,
            pass_sweep_nops,
            NULL,
    };

    Program program = { .symbols = &symbols };
    emit_program(&emit_ctx, &program, ast);

    passes_run(&program, passes, (Arenas) { .scratch = &scratch, .persistent = &backing});

    FILE *out = fopen("out.S", "w");
    compile_program_with(out, &scratch, &LINUX_X86_64_BACKEND, &program);

    fclose(out);
    
    arena_free(&backing);
    if (error) return -1;
    return 0;
}
