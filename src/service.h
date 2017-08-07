#include "util.h"

#include <string.h>
#include <unistd.h>
#include <varlink.h>

typedef struct {
        char *address;
        unsigned long index;

        int listen_fd;
        char *path_to_unlink;

        unsigned long n_interfaces;
        char **interfaces;

        char *executable;
        char *config;
        uid_t uid;
        gid_t gid;
        char **argv;
        bool activate_at_startup;

        pid_t pid;
        bool failed;
} Service;

long service_new(Service **servicep,
                 const char *address,
                 const char **interfaces, unsigned long n_interfaces,
                 const char *executable,
                 bool activate,
                 const char *config);
Service *service_free(Service *service);
void service_freep(Service **servicep);
long service_reset(Service *service);
long service_activate(Service *service);
