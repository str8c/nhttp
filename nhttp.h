#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define export __attribute__ ((visibility ("default")))

enum {
    TEXT_HTML, IMAGE_PNG,
};

typedef struct {
    void *data;
    int type;
    char buf[32768 - 12];
} PAGEINFO;
