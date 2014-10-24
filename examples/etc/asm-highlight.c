#include "../codebrowser.h"

static const char header[] =
    "<html>"
    "<head>"
    "<style>"
    "body{font-size:14px}"
    "i{color:#080}"
    "b{color:#400}"
    "u,s{color:#C40;text-decoration:none}"
    "s{color:#040}"
    "em{color:#400}"
    "a{color:#00F}"
    "tt{color:#F08}"
    "</style>"
    "</head>"
    "<body bgcolor=\"white\">"
    "<a href=\"./\">go back</a>"
    "<pre>";

static const char footer[] =
    "</pre>"
    "</body>"
    "</html>";

static const char* keywords[] = {
    "add", "and", "bts", "cli", "cmp", "dec", "in", "inc", "int", "je", "jmp", "jne",
    "jnz", "jz", "lea", "lgdt", "mov", "or", "out", "pop", "push", "rdmsr", "shl", "shr",
    "sti", "sub", "test", "wrmsr", "xor"
    };

/* TODO: use dest_max to prevent overflow, escape HTML from comments and strings */
char* asm_highlight(char *dest, char *dest_max, char *src)
{
    char ch, *w;
    bool firstch, firstword, num, escape;

    st(header);

    firstch = 1;
    firstword = 1;

redo:
    w = NULL;
    do {
        ch = *src;
        if(!ch) {
            break;
        }

        if(w) {
            if(isalnum(ch) || ch == '_') {
                continue;
            }

            if(num) {
                wordtag("u");
            } else if(firstch) {
                if(ch == ':') {
                    src++; wordtag("b");
                    goto redo;
                } else {
                    wordtag("em");
                }
            } else {
                *src = 0;
                if(firstword && find(w, keywords) >= 0) {
                    wordtag("a");
                } else {
                    word();
                }
            }
            firstword = 0;
            w = NULL;
        } else {
            if(isalnum(ch) || ch == '_') {
                w = src;
                num = isdigit(ch);
                continue;
            }
        }

        if(ch == ';') {
            st("<i>");
            while(*dest++ = ch, ch = *(++src), ch && ch != '\n');
            st("</i>");

            if(!ch) {
                break;
            }
        } else if(ch == '\'') {
            st("<s>");
            escape = 0;
            do {
                if(escape) {
                    st("<tt>\\");
                }
                *dest++ = ch;
                ch = *(++src);
                if(escape) {
                    escape = 0;
                    st("</tt>");
                } else if(ch == '\\') {
                    escape = 1;
                    ch = *(++src);
                }
            } while(ch && ch != '\'');
            st("</s>");

            if(!ch) {
                break;
            }
        }

        firstch = 0;
        if(ch == '\n') {
            firstch = 1;
            firstword = 1;
        }

        escapehtml(ch);
        *dest++ = ch;
    } while(src++, 1);

    st(footer);
    return dest;
}
