#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dlfcn.h>

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
    .port = 257,
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

    a = path;
    while(a != limit && *a != ' ') {
        a++;
    }
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
    free(cl);
}

int main(int argc, char *argv[])
{
    int efd, sock, n, client, len;
    socklen_t addrlen;
    struct epoll_event events[16], *ev, *ev_last;
    CLIENT *cl;

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return 1;
    }

    n = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)&n, sizeof(int));

    if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("bind() failed\n");
        return 1;
    }

    if(listen(sock, SOMAXCONN) < 0) {
        return 1;
    }

    if((efd = epoll_create(1)) < 0) {
        return 1;
    }

    ev = &events[0];
    ev->events = EPOLLIN;
    ev->data.ptr = NULL;
    epoll_ctl(efd, EPOLL_CTL_ADD, sock, ev); //check epoll_ctl error

    //todo: add timeouts http://linux.die.net/man/2/timerfd_create
    //use edge-triggered mode
    //cleanup when initiation fails and on exit

    //lib system init
    if(!(libconfig = dlopen("./config.so", RTLD_NOW | RTLD_LOCAL))) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        printf("no config file\n");
        return 1;
    }

    if(!(getlibname = dlsym(libconfig, "getlibname"))) {
       printf("invalid config file\n");
       return 1;
    }

    do {
        if((n = epoll_wait(efd, events, 16, -1)) < 0) {
            break;
        }

        ev = events;
        ev_last = ev + n;
        do {
            if(!ev->data.ptr) {
                if((client = accept(sock, (struct sockaddr*)&addr, &addrlen)) < 0) {
                    continue;
                }

                printf("accepted\n");

                cl = malloc(sizeof(*cl)); //check malloc error
                cl->sock = client;
                cl->rlen = 0;
                cl->request = malloc(2048); //check malloc error

                ev->events = EPOLLIN;// | EPOLLET;
                ev->data.ptr = cl;
                epoll_ctl(efd, EPOLL_CTL_ADD, client, ev); //check epoll_ctl error
            } else {
                cl = ev->data.ptr;
                if((len = recv(cl->sock, cl->request, 2048, 0)) <= 0) {
                    client_free(cl);
                    continue;
                }

                cl->rlen += len;
                cl->request = realloc(cl->request, cl->rlen + 2048); //check realloc error

                printf("read on fd\n%.*s\n", cl->rlen, cl->request);

                if(do_request(cl->sock, cl->rlen, cl->request)) {
                    client_free(cl);
                }
            }
        } while(ev++, ev != ev_last);
    } while(1);

    return 0;
}
