#include "error.h"
#include "common.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

bool error = false;
char *src;
size_t src_size;

char *line_start(unsigned int line) {
    unsigned int current = 0;
    unsigned int current_line = 1;
    while (current_line < line) {
        char c = src[current++];
        if (c == '\n') current_line++;
    }
    return &src[current];
}

void report_error(unsigned int line, unsigned int col, const char *fmt, ...) {
    error = true;

    printf("error %d:%d: ", line, col);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");

    char *line_src = line_start(line);
    int len = strcspn(line_src, "\n");
    printf(sv_fmt"\n", len, line_src);
    for (size_t i = 1; i < col; i++) {
        printf("~");
    }
    printf("^\n");

    exit(1);
}

// TODO: do this
void report_error_without_exit(unsigned int line, unsigned int col, const char *fmt, ...) {
    error = true;

    printf("error %d:%d: ", line, col);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
}
