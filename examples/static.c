#include "../nhttp.h"

int filetype(const char *path)
{
    do {
        if(!strcmp(path, ".png")) {
            return IMAGE_PNG;
        }
    } while(*(++path));
    return TEXT_HTML;
}

int getpage(PAGEINFO *p, const char *path, const char *post, int postlen)
{
    FILE *file;
    int len;
    const char *filepath;

    filepath = path;
    if(!*path) {
        filepath = "index.html";
    }

    if(!(file = fopen(filepath, "rb"))) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);

    if(len >= 32756) {
        p->data = malloc(len);
        if(!p->data) {
            return -1;
        }
        len = fread(p->data, 1, len, file);
    } else {
        len = fread(p->buf, 1, len, file);
    }

    fclose(file);

    p->type = filetype(filepath);
    return len;
}
