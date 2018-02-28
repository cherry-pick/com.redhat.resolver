#include "service.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

long service_new(Service **servicep,
                 const char *address,
                 const char **interfaces, unsigned long n_interfaces,
                 const char *executable,
                 uid_t uid,
                 gid_t gid,
                 bool activate,
                 const char *config) {
        _cleanup_(service_freep) Service *service = NULL;

        service = calloc(1, sizeof(Service));
        service->pid = -1;

        service->address = strdup(address);

        service->interfaces = calloc(n_interfaces, sizeof(char *));
        for (unsigned long i = 0; i < n_interfaces; i += 1)
                service->interfaces[i] = strdup(interfaces[i]);
        service->n_interfaces = n_interfaces;

        if (executable) {
                int listen_fd;

                service->executable = strdup(executable);

                if (config)
                        service->config = strdup(config);

                service->argv = calloc(3, sizeof(char *));
                service->argv[0] = service->executable;
                service->argv[1] = service->address;
                service->argv[2] = service->config;

                listen_fd = varlink_listen(service->address, &service->path_to_unlink);
                if (listen_fd < 0)
                        return listen_fd;

                service->listen_fd = listen_fd;
        }

        service->activate_at_startup = activate;

        *servicep = service;
        service = NULL;

        return 0;
}

long service_reset(Service *service) {
        int listen_fd;

        close(service->listen_fd);
        service->listen_fd = -1;

        if (service->path_to_unlink) {
                unlink(service->path_to_unlink);
                free(service->path_to_unlink);
                service->path_to_unlink = NULL;
        }

        listen_fd = varlink_listen(service->address, &service->path_to_unlink);
        if (listen_fd < 0)
                return listen_fd;

        service->listen_fd = listen_fd;

        return 0;
}

Service *service_free(Service *service) {
        if (service->pid >= 0)
                kill(service->pid, SIGTERM);

        if (service->listen_fd >= 0)
                close(service->listen_fd);

        if (service->path_to_unlink) {
                unlink(service->path_to_unlink);
                free(service->path_to_unlink);
        }

        for (unsigned long i = 0; i < service->n_interfaces; i += 1)
                free(service->interfaces[i]);
        free(service->interfaces);

        free(service->address);
        free(service->executable);
        free(service->config);
        free(service->argv);
        free(service);

        return NULL;
}

void service_freep(Service **servicep) {
        if (*servicep)
                service_free(*servicep);
}

long service_activate(Service *service) {
        char s[32];

        assert(service->executable);
        assert(service->pid < 0);

        service->pid = fork();
        if (service->pid < 0)
                return -errno;

        if (service->pid > 0)
                return 0;

        sprintf(s, "%d", getpid());
        setenv("LISTEN_PID", s, true);
        setenv("LISTEN_FDS", "1", true);

        /* Move activator fd to fd 3. All other fds have CLOEXEC set. */
        if (dup2(service->listen_fd, 3) < 0)
                return -errno;

        if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
                return -errno;

        if (service->executable[0] == '/' && chdir("/") < 0)
                return -errno;

        if (setsid() < 0)
                return -errno;

        if (service->gid > 0 && setresgid(service->gid, service->gid, service->gid) < 0)
                return -errno;

        if (service->uid > 0 && setresuid(service->uid, service->uid, service->uid) < 0)
                return -errno;

        execve(service->argv[0], service->argv, environ);
        _exit(errno);
}
