 #define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dlfcn.h>

/* ISSUES
    -cant be used to host large files (whole files are loaded into memory when requested),
        also because it kills connections after CONN_TIMEOUT seconds to prevent some attacks
*/

#ifdef DEBUG
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

#ifndef PORT
#define PORT 80
#endif

#define CONN_TIMEOUT 20 /* max time allowed between accepting connection and sending reponse*/
#define MAX_REQUEST_SIZE 0x10000 /* maximum size of POST requests allowed */

typedef struct {
    int sock, dlen, sent, post;
    char *data;
    int padding[2];
} CLIENT;

typedef struct {
    char name[12];
    uint32_t last_modified;
    void *lib;
    int (*getpage)(void *data, char *path, char *post);
} LIB;

static struct {
    uint16_t family, port;
    uint32_t ip;
    uint8_t padding[8];
} addr = {
    .family = AF_INET,
    .port = __bswap_constant_16(PORT),
};

static const struct itimerspec itimer = {
    .it_interval = {.tv_sec = CONN_TIMEOUT}, .it_value = {.tv_sec = CONN_TIMEOUT},
};


static const char error404[] = "HTTP/1.0 404\r\nContent-type: text/html\r\n\r\n"
    "<html>"
    "<head><title>404 Not Found</title></head>"
    "<body bgcolor=\"white\"><center><h1>404 Not Found</h1></center><hr></body>"
    "</html>";

#define HEADER(x) "HTTP/1.0 200\r\nContent-type: " x "\r\n\r\n"
#define HLEN(x) (sizeof(HEADER(x)) - 1)

enum {
    TEXT_HTML, IMAGE_PNG,
};

static const char* header[] = {
    HEADER("text/html; charset=utf-8"), HEADER("image/png"),
};

static int header_length[] = {
    HLEN("text/html; charset=utf-8"), HLEN("image/png"),
};

static void *libconfig;
static void (*getlibname)(char *dest, char *path, char *host);

static LIB* libs_get(char *name)
{
    static LIB libs[64];
    LIB *lib;

    for(lib = libs; lib->name[0]; lib++) {
        if(!strcmp(lib->name, name)) {
            return lib->getpage ? lib : NULL;
        }
    }

    strcpy(lib->name, name);
    if((lib->lib = dlopen(name, RTLD_NOW | RTLD_LOCAL)) && (lib->getpage = dlsym(lib->lib, "getpage"))) {
        return lib;
    }

    return NULL;
}

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
    uint32_t type;
    int len, k;
    char libname[64];
    char *path, *limit, *a;
    LIB *lib;

    struct {
        void *data;
        int type;
        char buf[32768 - 12];
    } info;

    limit = cl->data + cl->dlen;
    type = *(uint32_t*)cl->data;
    if(type == htonl('GET ')) {
        path = cl->data + 4;
    } else if(type == htonl('POST')) {
        path = cl->data + 5;
    } else {
        client_free(cl); return;
    }

    for(a = path; a != limit && *a != ' '; a++);
    *a = 0;

    getlibname(libname, path, NULL);
    debug("request path: %s\nrequest lib: %s\n", path, libname);
    if(!(lib = libs_get(libname))) {
        client_free(cl); return;
    }

    info.data = NULL;
    info.type = 0;
    if((len = lib->getpage(&info, path, NULL)) < 0) {
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
                return;
            }
        }
        free(info.data);
    }
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
    _Bool list;

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

    //lib system init
    if(!(libconfig = dlopen("./config.so", RTLD_NOW | RTLD_LOCAL))) {
        printf("dlopen failed: %s\n", dlerror());
        goto EXIT_CLOSE_EFD;
    }

    if(!(getlibname = dlsym(libconfig, "getlibname"))) {
       printf("invalid config file\n");
       goto EXIT_CLOSE_CONFIG;
    }

    addrlen = 0;
    list = 0;
    cl_list[0] = NULL;
    cl_list[1] = NULL;
    cl_count[0] = 0;
    cl_count[1] = 0;

    do {
        if((n = epoll_wait(efd, events, 16, -1)) < 0) {
            break;
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
                epoll_ctl(efd, EPOLL_CTL_ADD, client, ev); //handle epoll_ctl error
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

                ioctl(cl->sock, FIONREAD, &len); /* get bytes available */
                cl->data = realloc(cl->data, cl->dlen + len); //handle realloc error

                cl = ev->data.ptr;
                if(recv(cl->sock, cl->data + cl->dlen, len, 0) != len) {
                    client_free(cl);
                    continue;
                }
                cl->dlen += len;
                do_request(cl);
            }
        } while(ev++, ev != ev_last);
    } while(1);

EXIT_CLOSE_CONFIG:
    dlclose(libconfig);
EXIT_CLOSE_EFD:
    close(efd);
EXIT_CLOSE_TFD:
    close(tfd);
EXIT_CLOSE_SOCK:
    close(sock);
    return 1;
}
