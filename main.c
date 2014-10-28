#include "nhttp.h"
#define __USE_GNU /* required for accept4() */
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>

/* ISSUES
    -cant be used to host large files (whole files are loaded into memory when requested),
        also because it kills connections after CONN_TIMEOUT seconds to prevent some attacks
    TODO
    -dont parse entire HTTP request again everytime new POST data arrives
    -partial HTTP requests (usually receive complete request, but slow proxies break this rule)
*/

char* getconfig(GETPAGE **getpage, char *path, char *host);

#ifdef DEBUG
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

#ifndef PORT
#define PORT 80
#endif

#define CONN_TIMEOUT 20 /* max time allowed between accepting connection and sending reponse */
#define POST_MAX 0x10000 /* maximum size of POST requests allowed */

#define _htons(x) __bswap_constant_16(x)
#define _htonl(x) __bswap_constant_32(x)

typedef struct {
    int sock, dlen, sent, post;
    char *data;
    int padding[2];
} CLIENT;

static struct {
    uint16_t family, port;
    uint32_t ip;
    uint8_t padding[8];
} addr = {
    .family = AF_INET,
    .port = _htons(PORT),
};

static const struct itimerspec itimer = {
    .it_interval = {.tv_sec = CONN_TIMEOUT}, .it_value = {.tv_sec = CONN_TIMEOUT},
};

#define error_html(a, b) "HTTP/1.0 " a "\r\nContent-type: text/html\r\n\r\n" \
    "<html><head><title>" a " " b "</title></head>" \
    "<body bgcolor=\"white\"><center><h1>" a " " b "</h1></center><hr></body></html>" \

static const char
    error404[] = error_html("404", "Not Found"),
    error502[] = error_html("502", "Bad Gateway");

#define HEADER(x) "HTTP/1.0 200\r\nContent-type: " x "\r\n\r\n"
#define HLEN(x) (sizeof(HEADER(x)) - 1)

static const char* header[] = {
    HEADER("text/html; charset=utf-8"), HEADER("image/png"),
};

static int header_length[] = {
    HLEN("text/html; charset=utf-8"), HLEN("image/png"),
};

static void client_free(CLIENT *cl)
{
    close(cl->sock);
    free(cl->data);
    cl->sock = -1;
}

/* handle http request
    HTTP header must always be complete (single read), only POST data can be incomplete
*/
static void do_request(CLIENT *cl)
{
    int len, k, content_length;
    char *path, *a, *host, *post;
    bool _post;
    GETPAGE *getpage;

    PAGEINFO info;

    debug("%.*s\n", cl->dlen, cl->data);

    cl->data[cl->dlen] = 0; /* work in null-terminated space */
    switch(*(uint32_t*)cl->data) { /* cl->data is always allocated >= 4 bytes */
    case _htonl('GET '):
        path = cl->data + 4;
        _post = 0;
        break;
    case _htonl('POST'):
        if(cl->data[4] == ' ') {
            path = cl->data + 5;
            _post = 1;
            content_length = -1;
            break;
        }
    default:
        goto INVALID_REQUEST;
    }

    if(*path++ != '/') { /* first character of request path always '/' */
        goto INVALID_REQUEST;
    }

    /* find the end of requested path */
    len = 0;
    for(a = path; *a != ' '; a++) {
        if(++len >= 256 || !*a || (*a == '.' && *(a + 1) == '/')) {
            /* dont allow long paths or paths with aliases */
            goto INVALID_REQUEST;
        }
    }
    *a++ = 0;

    /* find the Host: and Content-Length (if POST) */
    host = NULL;
    do {
    REDO:
        do {
            if(!*a) {
                goto INVALID_REQUEST;
            }
            if(*a == '\r' && *(a + 1) == '\n') {
                *a = 0;
                a += 2;
                break;
            }
            a++;
        } while(1);

        if(!memcmp(a, "Host: ", 6)) {
            a += 6;
            host = a;
            goto REDO;
        }

        if(_post && !memcmp(a, "Content-Length: ", 16)) {
            content_length = strtol(a + 16, &a, 0);
            goto REDO;
        }
    } while(*a != '\r' || *(a + 1) != '\n');

    if(!host) {
        goto INVALID_REQUEST;
    }

    if(_post) {
        if(content_length < 0) {
            goto INVALID_REQUEST;
        }

        post = a + 2;
        k = cl->data + cl->dlen - post;
        //printf("%u %i\n", k, content_length);
        if(k < content_length) {
            return; /* wait for more data */
        }
    } else {
        post = NULL;
    }

    //printf("%i\n%s\n%s\n", content_length, path, host);
    path = getconfig(&getpage, path, host);
    if(!getpage) {
        send(cl->sock, error502, sizeof(error502) - 1, 0);
        goto INVALID_REQUEST;
    }

    info.data = NULL;
    info.type = 0;
    if((len = getpage(&info, path, post, content_length)) < 0) {
        send(cl->sock, error404, sizeof(error404) - 1, 0);
    } else {
        send(cl->sock, header[info.type], header_length[info.type], 0);

        k = send(cl->sock, (info.data ? info.data : info.buf), len, 0);
        if(k < len) {
            if(k >= 0) {
                len -= k;
                cl->dlen = -len;
                cl->sent = 0;
                cl->data = realloc(cl->data, len); //handle realloc error
                memcpy(cl->data, (info.data ? info.data : info.buf) + k, len);
                free(info.data);
                return;
            }
        }
        free(info.data);
    }

INVALID_REQUEST:
    client_free(cl);
}

int main(int argc, char *argv[])
{
    int efd, tfd, sock, client; /* file descriptors and sockets */
    int n, len;
    socklen_t addrlen;
    uint64_t exp;
    struct epoll_event events[16], *ev, *ev_last;
    CLIENT *cl, *cend, *cl_list[2];
    int cl_count[2];
    bool list;

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return 1;
    }

    n = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)&n, sizeof(int));

    if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("bind() failed\n");
        goto EXIT_CLOSE_SOCK;
    }

    if(listen(sock, SOMAXCONN) < 0) {
        goto EXIT_CLOSE_SOCK;
    }

    if((tfd = timerfd_create(CLOCK_MONOTONIC, 0)) < 0) {
        goto EXIT_CLOSE_SOCK;
    }

    timerfd_settime(tfd, 0, &itimer, NULL);

    if((efd = epoll_create(1)) < 0) {
        goto EXIT_CLOSE_TFD;
    }

    ev = &events[0];
    ev->events = EPOLLIN;
    ev->data.ptr = NULL;
    epoll_ctl(efd, EPOLL_CTL_ADD, sock, ev); //check epoll_ctl error
    ev->events = EPOLLIN;
    ev->data.ptr = (void*)1;
    epoll_ctl(efd, EPOLL_CTL_ADD, tfd, ev); //check epoll_ctl error

    addrlen = 0;
    list = 0;
    cl_list[0] = NULL;
    cl_list[1] = NULL;
    cl_count[0] = 0;
    cl_count[1] = 0;

    do {
        if((n = epoll_wait(efd, events, 1, -1)) < 0) {
            printf("epoll error %i\n", errno);
            continue;
        }

        ev = events;
        ev_last = ev + n;
        do {
            if(!ev->data.ptr) { /* listening socket event */
                if((client = accept4(sock, (struct sockaddr*)&addr, &addrlen, SOCK_NONBLOCK)) < 0) {
                    debug("accept failed\n");
                    continue;
                }

                if(!(cl = realloc(cl_list[list], (cl_count[list] + 1) * sizeof(CLIENT)))) {
                    continue;
                }

                cl_list[list] = cl;
                cl += cl_count[list]++;
                cl->sock = client;
                cl->dlen = 0;
                cl->data = NULL;

                ev->events = (EPOLLIN | EPOLLOUT) | EPOLLET;
                ev->data.ptr = cl;
                epoll_ctl(efd, EPOLL_CTL_ADD, client, ev); //TODO handle epoll_ctl error
            } else if(ev->data.ptr == (void*)1) { /* timerfd event */
                read(tfd, &exp, 8);
                list = !list;
                if(cl_count[list]) {
                    cl = cl_list[list];
                    cend = cl + cl_count[list];
                    cl_count[list] = 0;
                    do {
                        if(cl->sock >= 0) {
                            client_free(cl); //does not need to set sock=-1
                        }
                    } while(++cl != cend);
                } /* write above as a for() loop? */
            } else { /* client socket event */
                cl = ev->data.ptr;
                if(cl->dlen < 0) { /* waiting for EPOLLOUT */
                    if(!(ev->events & EPOLLOUT)) {
                        continue;
                    }

                    n = -cl->dlen;
                    len = send(cl->sock, cl->data + cl->sent, n, 0);
                    if(len < 0) {
                        client_free(cl);
                        continue;
                    }
                    cl->sent += len;
                    cl->dlen += len;
                    if(cl->dlen == 0) {
                        client_free(cl);
                    }
                    continue;
                }

                if(!(ev->events & EPOLLIN)) { /* no data available */
                    continue;
                }

                /* get bytes available */
                if(ioctl(cl->sock, FIONREAD, &len) < 0) {
                    debug("ioctl error %u\n", errno);
                    client_free(cl); continue;
                }

                if(cl->dlen + len > POST_MAX) {
                    client_free(cl); continue;
                }

                cl->data = realloc(cl->data, cl->dlen + len + 3); /* +1 for null terminator, minimum 4 bytes allocated */
                //TODO handle realloc error

                if(recv(cl->sock, cl->data + cl->dlen, len, 0) != len) {
                    client_free(cl); continue;
                }
                cl->dlen += len;
                do_request(cl);
            }
        } while(ev++, ev != ev_last);
    } while(1);

    close(efd);
EXIT_CLOSE_TFD:
    close(tfd);
EXIT_CLOSE_SOCK:
    close(sock);
    return 1;
}
