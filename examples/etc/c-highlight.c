#include "../codebrowser.h"

static const char header[] =
    "<html>"
    "<head>"
    "<style>"
    "body{font-size:14px;color:#F00}"
    "i{color:#080;font-style:normal}"
    "b{color:#008}"
    "u,s{color:#240;text-decoration:none}"
    "s{color:#000}"
    "a{color:#888}"
    "tt{color:#040}"
    "</style>"
    "</head>"
    "<body bgcolor=\"white\">"
    "<a href=\"./\" style=\"color:#00F\">go back</a>"
    "<pre>";

static const char footer[] =
    "</pre>"
    "</body>"
    "</html>";

static const char* keywords[] = {
    "bool", "break", "char", "const", "continue", "do", "else", "enum", "for", "goto", "if", "int",
    "int16_t", "int32_t", "int64_t", "int8_t", "long", "return", "short", "static", "struct", "typedef",
    "uint16_t", "uint32_t", "uint64_t", "uint8_t", "void", "while"
};

/* TODO: use dest_max to prevent overflow */
char* c_highlight(char *dest, char *dest_max, char *src)
{
    char ch, quotechar, *w;
    bool num, first, escape, preproc;

    st(header);

    num = 0;
    first = 1;
    preproc = 0;
    ch = 0;

    w = NULL;
    do {
        escape = (ch == '\\' && !escape);
        ch = *src;
        if(!ch) {
            break;
        }

        if(!preproc) {
            if(w) {
                if(isalnum(ch) || ch == '_') {
                    continue;
                }

                if(num) {
                    wordtag("u");
                }  else {
                    *src = 0;
                    if(find(w, keywords) >= 0) {
                        wordtag("b");
                    } else {
                        wordtag("s");
                    }
                }
                w = NULL;
            } else {
                if(isalnum(ch) || ch == '_') {
                    w = src;
                    num = isdigit(ch);
                    continue;
                }
            }
        }

        if(ch == '\n') {
            first = 1;
            if(preproc && !escape) {
                preproc = 0;
                st("</i>");
            }
            goto noescape;
        }

        if(ch == '/') {
            if(*(src + 1) == '*') {
                st("<a>");
                *dest++ = '/'; *dest++ = '*'; src++;
                do {
                    escape = (ch == '*');
                    ch = *(++src);
                    if(!ch) {
                        goto breakout;
                    }

                    escapehtml(ch);
                    *dest++ = ch;
                } while(!escape || ch != '/');
                st("</a>");
                continue;
            } else if(*(src + 1) == '/') {
                st("<a>");
                *dest++ = '/'; *dest++ = '/'; src++;
                do {
                    ch = *(++src);
                    if(!ch) {
                        goto breakout;
                    }

                    escapehtml(ch);
                    *dest++ = ch;
                } while(ch != '\n');
                st("</a>");
                continue;
            }
        }

        if(preproc) {
            goto escape;
        }

        if(!escape && (ch == '"' || ch == '\'' )) {
            st("<tt>");
            quotechar = ch;
            escape = 1;
            goto start2;
            do {
                escape = (ch == '\\' && !escape);
                ch = *(++src);
                if(!ch) {
                    goto breakout;
                }

                escapehtml(ch);
            start2:
                *dest++ = ch;
            } while(escape || ch != quotechar);
            st("</tt>");
            continue;
        }

        if(first && ch == '#') {
            st("<i>");
            preproc = 1;
            escape = 0;
        }

        first = 0;
    escape:
        escapehtml(ch);
    noescape:
        *dest++ = ch;
    } while(src++, 1);
breakout:

    st(footer);
    return dest;
}
