#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dlfcn.h>

#ifndef PORT
#define PORT 80
#endif

#define CONN_TIMEOUT 10 /* max time allowed between accepting connection and receiving full http request */
#define MAX_REQUEST_SIZE 0x10000

typedef struct {
    int sock, rlen;
    char *request;
} CLIENT;

typedef struct {
    char name[12];
    uint32_t last_modified;
    void *lib;
    int (*getpage)(char *dest, char *path, char *post);
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


static const char
    error404[] = "HTTP/1.0 404\r\nContent-type: text/html\r\n\r\n"
                "<html>"
                "<head><title>404 Not Found</title></head>"
                "<body bgcolor=\"white\"><center><h1>404 Not Found</h1></center><hr></body>"
                "</html>",
    header[] = "HTTP/1.0 200\r\nContent-type: text/html; charset=utf-8\r\n\r\n";

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

/* GET requests are always single-read (up to 2048 large),
   POST requests can be up to 64k
 */

static _Bool do_request(int sock, int rlen, char *request)
{
    uint32_t type;
    int len;
    char libname[64];
    char *path, *limit, *a;
    LIB *lib;
    char tmp[32768];

    limit = request + rlen;
    type = *(uint32_t*)request;
    if(type == htonl('GET ')) {
        path = request + 4;
    } else if(type == htonl('POST')) {
        path = request + 5;
    } else {
        return 1;
    }

    for(a = path; a != limit && *a != ' '; a++);
    *a = 0;

    getlibname(libname, path, NULL);
    printf("request path: %s\nrequest lib: %s\n", path, libname);
    if(!(lib = libs_get(libname))) {
        printf("invalid lib\n");
        return 1;
    }

    if((len = lib->getpage(tmp, path, NULL)) < 0) {
        send(sock, error404, sizeof(error404) - 1, 0);
    } else {
        send(sock, header, sizeof(header) - 1, 0);
        send(sock, tmp, len, 0);
    }

    return 1;
}

static void client_free(CLIENT *cl)
{
    close(cl->sock);
    free(cl->request);
    cl->sock = -1;
}

int main(int argc, char *argv[])
{
    int efd, tfd, sock, n, client, len;
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

    //use edge-triggered mode

    //lib system init
    if(!(libconfig = dlopen("./config.so", RTLD_NOW | RTLD_LOCAL))) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        printf("no config file\n");
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
                printf("alpha %i %i\n", sock, addrlen);
                if((client = accept(sock, (struct sockaddr*)&addr, &addrlen)) < 0) {
                    printf("accept failed\n");
                    continue;
                }

                if(!(cl = realloc(cl_list[list], (cl_count[list] + 1) * sizeof(CLIENT)))) {
                    continue;
                }

                cl_list[list] = cl;
                cl += cl_count[list]++;
                cl->sock = client;
                cl->rlen = 0;
                cl->request = malloc(2048); //handle malloc error

                ev->events = EPOLLIN;// | EPOLLET;
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
                }
                /* write above as a for() loop? */
            } else { /* client socket event */
                cl = ev->data.ptr;
                if((len = recv(cl->sock, cl->request, 2048, 0)) <= 0) {
                    client_free(cl);
                    continue;
                }

                cl->rlen += len;
                cl->request = realloc(cl->request, cl->rlen + 2048); //handle realloc error

                printf("read on fd\n%.*s\n", cl->rlen, cl->request);

                if(do_request(cl->sock, cl->rlen, cl->request)) {
                    client_free(cl);
                }
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
