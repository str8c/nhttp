#include <dirent.h>
#include <unistd.h>

#include "codebrowser.h"

int _find(const char *word, const char **list, int max)
{
    int min, mid, i;

    min = 0;
    do {
        mid = (max + min) / 2;
        i = strcmp(word, list[mid]);
        if(!i) {
            return mid;
        } else if(i < 0) {
            max = mid - 1;
        } else {
            min = mid + 1;
        }
    } while (max >= min);

    return -1;
}

char* asm_highlight(char *dest, char *dest_max, char *src);
char* c_highlight(char *dest, char *dest_max, char *src);

static char* default_highlight(char *dest, char *dest_max, char *src)
{
    char ch;

    st("<html><body bgcolor=\"white\" style=\"font-size:14px\">"
       "<a href=\"./\" style=\"color:#00F\">go back</a><pre>");
    while((ch = *src++)) {
        escapehtml(ch);
        *dest++ = ch;
    }
    st("</pre></body></html>");

    return dest;
}

static char* (*highlight[])(char*, char*, char*) = {
    default_highlight,
    asm_highlight,
    c_highlight
};


export int getpage(PAGEINFO *p, char *path, const char *post, int postlen)
{
    FILE *file;
    int len, type;
    char *str;
    bool root;

    /* prepend path with ROOT */
    len = strlen(path);
    char filepath[len + sizeof(ROOT)];
    strcpy(filepath, ROOT);
    str = filepath + sizeof(ROOT) - 1;
    memcpy(str, path, len + 1);

    if(!*str) { /* root dir */
        root = 1;
        goto directory;
    }

    type = 0;
    do {
        if(eq(str, ".asm")) {
            type = 1; break;
        } else if(eq(str, ".c") || eq(str, ".h")) {
            type = 2; break;
        } else if(eq(str, "/")) {
            root = 0;
            goto directory;
        }
    } while(*(++str));

    if(!(file = fopen(filepath, "rb"))) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);

    if(len > sizeof(p->buf) - 128) {
        fclose(file);
        return -1;
    }
{
    char buf[len + 1];
    len = fread(buf, 1, len, file);
    buf[len] = 0;
    fclose(file);

    str = highlight[type](p->buf, p->buf + sizeof(p->buf), buf);
    return str - p->buf; }

directory:; /* requested is a directory */
    DIR *d;
    struct dirent *dir;
    char *dest;

    d = opendir(filepath);
    if(!d) {
        return -1;
    }

    dest = p->buf;
    st("<html><head><style>body{font-size:14px}a{color:#00F}</style></head>"
       "<body bgcolor=\"white\"><pre>");
    if(!root) {
        st("<a href=\"../\">go up</a>");
    } else {
        st("<a href=\"/\">home</a>");
    }

    st("<ul>");
    while((dir = readdir(d))) {
        if(dir->d_name[0] == '.') {
            continue;
        }

        if(dir->d_type == DT_DIR) {
            dest += sprintf(dest, "<li><a href=\"%s/\">%s/</a></li>", dir->d_name, dir->d_name);
        } else {
            dest += sprintf(dest, "<li><a href=\"%s\">%s</a></li>", dir->d_name, dir->d_name);
        }
    }
    st("</ul></pre></body></html>");

    closedir(d);
    return dest - p->buf;
}
