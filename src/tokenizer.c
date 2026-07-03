#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tokenizer.h"
#include "common.h"
#include "error.h"

const TokenInfo token_info_table[] = {
    [TOKEN_PLUS]           = { .name = "+",           .flags = TOK_OPERATOR, .precedence = 2},
    [TOKEN_MINUS]          = { .name = "-",           .flags = TOK_OPERATOR, .precedence = 2},
    [TOKEN_MULTIPLY]       = { .name = "*",           .flags = TOK_OPERATOR, .precedence = 3},
    [TOKEN_DIVIDE]         = { .name = "/",           .flags = TOK_OPERATOR, .precedence = 3},
    [TOKEN_CARET]          = { .name = "^",           .flags = TOK_OPERATOR, .precedence = 7},
    [TOKEN_AMPERSAND]      = { .name = "&",           .flags = TOK_OPERATOR, .precedence = 7},
    [TOKEN_OPEN_CURLY]     = { .name = "{",           .flags = 0},
    [TOKEN_CLOSE_CURLY]    = { .name = "}",           .flags = 0},
    [TOKEN_OPEN_PAREN]     = { .name = "(",           .flags = TOK_HAS_PREC, .precedence = 4},
    [TOKEN_CLOSE_PAREN]    = { .name = ")",           .flags = 0},
    [TOKEN_OPEN_SQUARE]    = { .name = "[",           .flags = TOK_HAS_PREC, .precedence = 4},
    [TOKEN_CLOSE_SQUARE]   = { .name = "]",           .flags = 0},
    [TOKEN_SEMICOLON]      = { .name = ";",           .flags = 0},
    [TOKEN_COLON]          = { .name = ":",           .flags = 0},
    [TOKEN_PERIOD]         = { .name = ".",           .flags = TOK_OPERATOR, .precedence = 5},
    [TOKEN_COMMA]          = { .name = ",",           .flags = 0},
    [TOKEN_EQUALS]         = { .name = "=",           .flags = 0},
    [TOKEN_EQUALS_EQUALS]  = { .name = "==",          .flags = TOK_OPERATOR, .precedence = 1},
    [TOKEN_ARROW]          = { .name = "=>",          .flags = 0},
    [TOKEN_LESS_EQUALS]    = { .name = "<=",          .flags = TOK_OPERATOR, .precedence = 1},
    [TOKEN_GREATER_EQUALS] = { .name = ">=",          .flags = TOK_OPERATOR, .precedence = 1},
    [TOKEN_LESS_THAN]      = { .name = "<",           .flags = TOK_OPERATOR, .precedence = 1},
    [TOKEN_GREATER_THAN]   = { .name = ">",           .flags = TOK_OPERATOR, .precedence = 1},
    [TOKEN_INT_LIT]        = { .name = "int lit",     .flags = TOK_HAS_PREC, .precedence = 6},
    [TOKEN_STR_LIT]        = { .name = "str lit",     .flags = TOK_HAS_PREC, .precedence = 6},
    [TOKEN_FLOAT_LIT]      = { .name = "float lit",   .flags = TOK_HAS_PREC, .precedence = 6},
    [TOKEN_IDENTIFIER]     = { .name = "identifier",  .flags = TOK_HAS_PREC, .precedence = 6},
    [TOKEN_RETURN]         = { .name = "return",      .flags = TOK_KEYWORD},
    [TOKEN_IF]             = { .name = "if",          .flags = TOK_KEYWORD},
    [TOKEN_ELIF]           = { .name = "elif",        .flags = TOK_KEYWORD},
    [TOKEN_ELSE]           = { .name = "else",        .flags = TOK_KEYWORD},
    [TOKEN_WHILE]          = { .name = "while",       .flags = TOK_KEYWORD},
    [TOKEN_PROC]           = { .name = "proc",        .flags = TOK_KEYWORD},
    [TOKEN_EXTERN]         = { .name = "extern",      .flags = TOK_KEYWORD},
    [TOKEN_U32]            = { .name = "u32",         .flags = TOK_PRIMITIVE},
    [TOKEN_I32]            = { .name = "i32",         .flags = TOK_PRIMITIVE},
    [TOKEN_I8]             = { .name = "i8",          .flags = TOK_PRIMITIVE},
    [TOKEN_U8]             = { .name = "u8",          .flags = TOK_PRIMITIVE},
    [TOKEN_EOF]            = { .name = "eof",         .flags = 0}
};

const TokenType token_from_char[] = {
    [':'] = TOKEN_COLON,
    [';'] = TOKEN_SEMICOLON,
    ['+'] = TOKEN_PLUS,
    ['-'] = TOKEN_MINUS,
    ['/'] = TOKEN_DIVIDE,
    ['*'] = TOKEN_MULTIPLY,
    ['='] = TOKEN_EQUALS,
    ['>'] = TOKEN_GREATER_THAN,
    ['<'] = TOKEN_LESS_THAN,
    ['('] = TOKEN_OPEN_PAREN,
    [')'] = TOKEN_CLOSE_PAREN,
    ['['] = TOKEN_OPEN_SQUARE,
    [']'] = TOKEN_CLOSE_SQUARE,
    ['{'] = TOKEN_OPEN_CURLY,
    ['}'] = TOKEN_CLOSE_CURLY,
    ['.'] = TOKEN_PERIOD,
    [','] = TOKEN_COMMA,
    ['^'] = TOKEN_CARET,
    ['&'] = TOKEN_AMPERSAND,
};

static_assert(sizeof(token_info_table) / sizeof(TokenInfo) == TOKEN_COUNT,
              "Every token must have a corresponding entry in the token to string table, so add an entry probably");

int int_from_str(const char *a, size_t len) {
    int tens = 1;
    int accum = 0;
    for (size_t i = len; i-- > 0;) {
        accum += (a[i] - '0') * tens;
        tens *= 10;
    }
    return accum;
}

float float_from_str(const char *a, size_t len) {
    float accum = 0;
    size_t decimal = len/*, exponent = -1*/;
    float tens = 1;

    for (size_t i = 0; i < len; i++) {
        if (a[i] == '.') {
            decimal = i;
        }
        if (a[i] == 'e' || a[i] == 'E') {
            // exponent = i; // TODO : ADD EXPONENT support
        }
    }

    for (size_t i = decimal; i-- > 0;) {
        accum += (a[i] - '0') * tens;
        tens *= 10;
    }

    tens = 0.1;
    for (size_t i = decimal + 1; i < len; i++) {
        accum += (a[i] - '0') * tens;
        tens /= 10;
    }

    return accum;
}

bool is_number(uint8_t c) {
    return '0' <= c && c <= '9';
}

bool is_alpha(uint8_t c) {
    return ('a' <= c  && c <= 'z') || ('A' <= c && c <= 'Z');
}


TokenArray tokenize(Arenas *arena, const char *buf, size_t size) {
    TokenArray tokens = { 0 };

    size_t start = 0;
    size_t next = 0;
    size_t col = 0;
    size_t line = 1;
    
    while (start < size) {
        uint8_t chr = buf[next++];
        Token tok = { .col = col, .line = line };

        switch (chr) {
            case '\n':
                col = 1;
                line++;
                start = next;
                continue;
            case ' ':
            case '\t':
                col++;
                start = next;
                continue;
            case '\r':
                col = 1;
                line++;
                start = buf[next] == '\n' ? ++next : next;
                continue;
            case '/':
                if (buf[next] == '/') {
                    while (next < size && buf[next] != '\n')
                        next++;
                    continue;
                }
            case '+': case '*': case '-': case '^': case '&': case '{': case '}': case '(': case ')': case '[': case ']':  case ':': case ';': case '.': case ',':
                tok.type = token_from_char[chr];
                break;
            case '=':
                tok.type = token_from_char[chr];
                if (buf[next] == '>' || buf[next] == '=') next++;
                break;
            case '<': case '>':
                tok.type = token_from_char[chr];
                if (buf[next] == '=') next++;
                break;
            default: {
                if (is_alpha(chr) || chr == '_') {
                    tok.type = TOKEN_IDENTIFIER;
                    while (next < size && (is_alpha(buf[next]) || is_number(buf[next]) || buf[next] == '_')) {
                        next++;
                    }
                } else if (is_number(chr)) {
                    tok.type = TOKEN_INT_LIT;
                    int num_decimal_points = 0;
                    while (next < size && (is_number(buf[next]) || buf[next] == '.')) {
                        if (buf[next] == '.') {
                            tok.type = TOKEN_FLOAT_LIT;
                            num_decimal_points++;
                        }
                        next++;
                    }

                    if (num_decimal_points > 1)
                        report_error(tok.line, tok.col, "too many decimal points in float literal");
                }
            }
        }

        char token[next - start + 1]; // VLA
        memcpy(token, &buf[start], next - start);
        token[next - start] = '\0';

        if (next - start > 1) {
            struct keyword *result = lookup_keyword(token, next - start);
            if (result != NULL)
                tok.type = result->token;
        }

        switch (tok.type) {
            case TOKEN_IDENTIFIER:
                tok.identifier = arena_alloc(arena->persistent, sizeof(char) * (next - start + 1));
                strncpy(tok.identifier, token, next - start);
                tok.identifier[next - start] = '\0';
                break;
            case TOKEN_INT_LIT:
                tok.int_lit = int_from_str(&buf[start], next - start);
                break;
            case TOKEN_FLOAT_LIT:
                tok.float_lit = float_from_str(&buf[start], next - start);
                break;
            case TOKEN_NONE:
                report_error(line, col, "unrecognized token");
                break;
            default:
                break;
        }

        tok.lexeme.len = next - start;
        tok.lexeme.str = &buf[start];
        arena_array_append(arena->persistent, tokens, tok);
        col += next - start;
        start = next;
    }

    Token tok_eof = {0};
    tok_eof.type = TOKEN_EOF;
    arena_array_append(arena->persistent, tokens, tok_eof);

    return tokens;
}


void print_token(Token tok) {
    switch (tok.type) {
        case TOKEN_INT_LIT: printf("%u", tok.int_lit); break;
        case TOKEN_FLOAT_LIT: printf("%.2f", tok.float_lit); break;
        case TOKEN_IDENTIFIER: printf("%s", tok.identifier); break;
        default: printf("%s", token_info_table[tok.type].name); break;
    }
    printf("\n");
}


bool token_is_operator(TokenType type) {
    return (token_info_table[type].flags & TOK_OPERATOR) == TOK_OPERATOR;
}


bool token_has_precedence(TokenType type) {
    return token_is_operator(type) ||
           (token_info_table[type].flags & TOK_HAS_PREC) == TOK_HAS_PREC;
}


bool token_is_primitive(TokenType type) {
    return (token_info_table[type].flags & TOK_PRIMITIVE) == TOK_PRIMITIVE;
}


int token_precedence(TokenType type) {
    return token_has_precedence(type) ? token_info_table[type].precedence : -1;
}
