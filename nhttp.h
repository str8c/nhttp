#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum {
    TEXT_HTML, IMAGE_PNG,
};

typedef struct {
    void *data;
    int type;
    char buf[0];
} PAGEINFO;
