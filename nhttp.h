#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

enum {
    TEXT_HTML, IMAGE_PNG,
};

typedef struct {
    void *data;
    int type;
    char buf[32768 - 12];
} PAGEINFO;

typedef int (GETPAGE)(PAGEINFO*, const char*, const char*, int);

#define export(x, y) x##_##y
#define import(x) GETPAGE x##_getpage
