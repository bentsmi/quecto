#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "tokenizer.h"

int token_tests(void) {

    Arena persistent;
    arena_create(&persistent, 16*1024);

    Arenas arenas = (Arenas){
        .persistent = &persistent,
        .scratch = NULL,
    };
    
    struct {
        const char *input;
        int expected_count;
        TokenType *expected_types;
    } tok_tests[] = {
        {
            .input = "(((3 + 4)));",
            .expected_count = 11, // (, (, (, 3, +, 4, ), ), ), ;, EOF
            .expected_types = (TokenType[]){TOKEN_OPEN_PAREN, TOKEN_OPEN_PAREN, TOKEN_OPEN_PAREN, TOKEN_INT_LIT, TOKEN_PLUS, TOKEN_INT_LIT, TOKEN_CLOSE_PAREN, TOKEN_CLOSE_PAREN, TOKEN_CLOSE_PAREN, TOKEN_SEMICOLON, TOKEN_EOF}
        },
        {
            .input = "3.14 * ((2 + 1));",
            .expected_count = 11, // 3.14, *, (, 2, +, 1, ), ;,  EOF
            .expected_types = (TokenType[]){TOKEN_FLOAT_LIT, TOKEN_MULTIPLY, TOKEN_OPEN_PAREN, TOKEN_OPEN_PAREN, TOKEN_INT_LIT, TOKEN_PLUS, TOKEN_INT_LIT, TOKEN_CLOSE_PAREN, TOKEN_CLOSE_PAREN, TOKEN_SEMICOLON, TOKEN_EOF}
        },
        {
            .input = "42 / 7 - 1.2;",
            .expected_count = 7, // 42, /, 7, -, 1.2, ;, EOF
            .expected_types = (TokenType[]){TOKEN_INT_LIT, TOKEN_DIVIDE, TOKEN_INT_LIT, TOKEN_MINUS, TOKEN_FLOAT_LIT, TOKEN_SEMICOLON, TOKEN_EOF}
        }
    };

    int num_tok_tests = sizeof(tok_tests) / sizeof(tok_tests[0]);
    int tok_tests_passed = 0;

    for (int i = 0; i < num_tok_tests; i++) {
        printf("Running Token Test %d: \"%s\"\n", i + 1, tok_tests[i].input);
        TokenArray result = tokenize(&arenas, tok_tests[i].input, strlen(tok_tests[i].input));

        if (result.count != (size_t)tok_tests[i].expected_count) {
            printf("FAILED: Expected %d tokens, got %zu\n", tok_tests[i].expected_count, result.count);
        } else {
            int match = 1;
            for (int j = 0; j < tok_tests[i].expected_count; j++) {
                if (result.items[j].type != tok_tests[i].expected_types[j]) {
                    printf("FAILED: Token %d mismatch. Expected %s, got %s\n", 
                           j, token_info_table[tok_tests[i].expected_types[j]].name, 
                           token_info_table[result.items[j].type].name);
                    match = 0;
                    break;
                }
            }
            if (match) {
                printf("PASSED\n\n");
                tok_tests_passed++;
            }
        }
    }

    printf("\nTest Summary: %d/%d tok_tests_passed\n", tok_tests_passed, num_tok_tests);
    arena_free(&persistent);
    return (tok_tests_passed == num_tok_tests) ? 0 : 1;
}

int main(void) {
    return token_tests();
}
