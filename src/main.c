#include "service.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <varlink.h>

#include "org.varlink.activator.varlink.c.inc"
#include "org.varlink.resolver.varlink.c.inc"

typedef struct {
        const char *name;
        Service *service;
} Interface;

typedef struct {
        VarlinkService *service;

        int epoll_fd;
        int signal_fd;

        char *vendor;
        char *product;
        char *version;
        char *url;

        Service **services;
        unsigned long n_services;
        unsigned long n_services_allocated;

        Interface *interfaces;
        unsigned long n_interfaces;
} Manager;

static void manager_free(Manager *m) {
        for (unsigned long i = 0; i < m->n_services; i += 1)
                service_free(m->services[i]);
        free(m->services);

        if (m->epoll_fd >= 0)
                close(m->epoll_fd);

        if (m->signal_fd >= 0)
                close(m->signal_fd);

        free(m->vendor);
        free(m->product);
        free(m->version);
        free(m->url);

        if (m->service)
                varlink_service_free(m->service);

        free(m->interfaces);

        free(m);
}

static void manager_freep(Manager **mp) {
        if (*mp)
                manager_free(*mp);
}

static long manager_new(Manager **mp) {
        _cleanup_(manager_freep) Manager *m = NULL;

        m = calloc(1, sizeof(Manager));
        m->epoll_fd = -1;
        m->signal_fd = -1;

        *mp = m;
        m = NULL;

        return 0;
}

static long manager_watch_service(Manager *m, Service *service) {
        struct epoll_event ev = {};

        ev.events = EPOLLIN;
        ev.data.ptr = service;
        if (epoll_ctl(m->epoll_fd, EPOLL_CTL_ADD, service->listen_fd, &ev) < 0)
                return -errno;

        return 0;
}

static long manager_unwatch_service(Manager *m, Service *service) {
        if (epoll_ctl(m->epoll_fd, EPOLL_CTL_DEL, service->listen_fd, NULL) < 0)
                return -errno;

        return 0;
}

static long manager_add_service(Manager *m, Service *service) {
        long r;

        if (m->n_services == m->n_services_allocated) {
                m->n_services_allocated = MAX(m->n_services_allocated * 2, 8);
                m->services = realloc(m->services, m->n_services_allocated * sizeof(Service *));
        }

        service->index = m->n_services;
        m->services[m->n_services] = service;
        m->n_services += 1;

        if (service->executable) {
                r = manager_watch_service(m, service);
                if (r < 0)
                        return r;
        }

        return 0;
}

static long manager_remove_service(Manager *m, Service *service) {
        /* Move last service to current slot */
        if (service->index + 1 < m->n_services) {
                m->services[service->index] = m->services[m->n_services - 1];
                m->services[service->index]->index = service->index;
        }

        m->n_services -= 1;
        manager_unwatch_service(m, service);
        service_free(service);

        return 0;
}

static int interfaces_compare(const void *p1, const void *p2) {
        Interface *i1 = (Interface *)p1;
        Interface *i2 = (Interface *)p2;

        return strcmp(i1->name, i2->name);
}

static long manager_update_interface_index(Manager *m) {
        unsigned long n_interfaces_allocated = 0;

        m->n_interfaces = 0;
        free(m->interfaces);
        m->interfaces = NULL;

        for (unsigned long s = 0; s < m->n_services; s += 1) {
                Service *service = m->services[s];

                for (unsigned long i = 0; i < service->n_interfaces; i += 1) {
                        if (m->n_interfaces == n_interfaces_allocated) {
                                n_interfaces_allocated = MAX(n_interfaces_allocated * 2, 8);
                                m->interfaces = realloc(m->interfaces, n_interfaces_allocated * sizeof(Interface));
                        }

                        m->interfaces[m->n_interfaces].name = service->interfaces[i];
                        m->interfaces[m->n_interfaces].service = service;
                        m->n_interfaces += 1;
                }
        }

        qsort(m->interfaces, m->n_interfaces, sizeof(Interface), interfaces_compare);

        /* Check for duplicates. */
        for (unsigned long i = 0; i + 1 < m->n_interfaces; i += 1)
                if (strcmp(m->interfaces[i].name, m->interfaces[i + 1].name) == 0)
                        return -ENOTUNIQ;

        return 0;
}

static long manager_find_service_by_interface(Manager *m, const char *interface_name, Service **servicep) {
        Interface interf = {
                .name = (char *)interface_name,
        };
        Interface *interface;

        interface = bsearch(&interf, m->interfaces, m->n_interfaces, sizeof(Interface), interfaces_compare);
        if (!interface)
                return -ESRCH;

        *servicep = interface->service;

        return 0;
}

static long manager_find_service_by_pid(Manager *m, Service **servicep, pid_t pid) {
        assert(pid > 0);

        for (unsigned long i = 0; i < m->n_services; i += 1) {
                Service *service = m->services[i];

                if (service->pid == pid) {
                        *servicep = service;

                        return 0;
                }
        }

        return -ESRCH;
}

static long manager_find_service_by_address(Manager *m, Service **servicep, const char *address) {
        for (unsigned long i = 0; i < m->n_services; i += 1) {
                Service *service = m->services[i];

                if (strcmp(service->address, address) == 0) {
                        *servicep = service;

                        return 0;
                }
        }

        return -ESRCH;
}

static long org_varlink_resolver_Resolve(VarlinkService *resolver_service,
                                         VarlinkCall *call,
                                         VarlinkObject *parameters,
                                         uint64_t flags,
                                         void *userdata) {
        Manager *m = userdata;
        const char *interface_name = NULL;
        Service *service;
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        long r;

        r = varlink_object_get_string(parameters, "interface", &interface_name);
        if (r < 0)
                return varlink_call_reply_invalid_parameter(call, "interface");

        r = manager_find_service_by_interface(m, interface_name, &service);
        if (r < 0) {
                switch (r) {
                        case -ESRCH:
                                return varlink_call_reply_error(call, "org.varlink.resolver.InterfaceNotFound", NULL);

                        default:
                                return r;
                }
        }

        varlink_object_new(&out);
        varlink_object_set_string(out, "address", service->address);

        return varlink_call_reply(call, out, 0);
}

static long org_varlink_activator_GetConfig(VarlinkService *resolver_service,
                                            VarlinkCall *call,
                                            VarlinkObject *parameters,
                                            uint64_t flags,
                                            void *userdata) {
        Manager *m = userdata;
        _cleanup_(varlink_array_unrefp) VarlinkArray *servicesv = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *configv = NULL;
        long r;

        varlink_object_new(&configv);

        if (m->vendor)
                varlink_object_set_string(configv, "vendor", m->vendor);
        if (m->product)
                varlink_object_set_string(configv, "product", m->product);
        if (m->version)
                varlink_object_set_string(configv, "version", m->version);
        if (m->url)
                varlink_object_set_string(configv, "url", m->url);

        varlink_array_new(&servicesv);
        for (unsigned long s = 0; s < m->n_services; s += 1) {
                Service *service = m->services[s];
                _cleanup_(varlink_object_unrefp) VarlinkObject *servicev = NULL;
                _cleanup_(varlink_array_unrefp) VarlinkArray *interfacesv = NULL;
                _cleanup_(varlink_object_unrefp) VarlinkObject *executablev = NULL;

                varlink_array_new(&interfacesv);

                for (unsigned long i = 0; i < service->n_interfaces; i += 1) {
                        r = varlink_array_append_string(interfacesv, service->interfaces[i]);
                        if (r < 0)
                                return r;
                }

                varlink_object_new(&executablev);
                if (service->executable)
                        varlink_object_set_string(executablev, "path", service->executable);
                varlink_object_set_int(executablev, "user_id", service->uid);
                varlink_object_set_int(executablev, "group_id", service->gid);

                varlink_object_new(&servicev);
                varlink_object_set_string(servicev, "address", service->address);
                varlink_object_set_array(servicev, "interfaces", interfacesv);
                varlink_object_set_object(servicev, "executable", executablev);
                varlink_object_set_bool(servicev, "run_at_startup", service->activate_at_startup);

                r = varlink_array_append_object(servicesv, servicev);
                if (r < 0)
                        return r;
        }

        varlink_object_set_array(configv, "services", servicesv);

        return varlink_call_reply(call, configv, 0);
}

static long org_varlink_resolver_GetInfo(VarlinkService *service,
                                         VarlinkCall *call,
                                         VarlinkObject *parameters,
                                         uint64_t flags,
                                         void *userdata) {
        Manager *m = userdata;
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        _cleanup_(varlink_array_unrefp) VarlinkArray *interfaces = NULL;

        varlink_object_new(&reply);

        if (m->vendor)
                varlink_object_set_string(reply, "vendor", m->vendor);
        if (m->product)
                varlink_object_set_string(reply, "product", m->product);
        if (m->version)
                varlink_object_set_string(reply, "version", m->version);
        if (m->url)
                varlink_object_set_string(reply, "url", m->url);

        varlink_array_new(&interfaces);
        for (unsigned long i = 0; i < m->n_interfaces; i += 1)
                varlink_array_append_string(interfaces, m->interfaces[i].name);
        varlink_object_set_array(reply, "interfaces", interfaces);

        return varlink_call_reply(call, reply, 0);
}

static long org_varlink_activator_AddServices(VarlinkService *resolver_service,
                                              VarlinkCall *call,
                                              VarlinkObject *parameters,
                                              uint64_t flags,
                                              void *userdata) {
        Manager *m = userdata;
        long n_services;
        _cleanup_(varlink_array_unrefp) VarlinkArray *servicesv = NULL;
        long r;

        r = varlink_object_get_array(parameters, "services", &servicesv);
        if (r < 0)
                return r;

        n_services = varlink_array_get_n_elements(servicesv);
        if (n_services < 0)
                return n_services;

        for (long s = 0; s < n_services; s += 1) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *servicev = NULL;
                _cleanup_(varlink_object_unrefp) VarlinkObject *executablev = NULL;
                _cleanup_(varlink_array_unrefp) VarlinkArray *interfacesv = NULL;
                const char *address;
                const char *executable = NULL;
                bool activate = false;
                _cleanup_(freep) const char **interfaces = NULL;
                unsigned long n_interfaces;
                _cleanup_(service_freep) Service *service = NULL;
                Service *service_old;

                r = varlink_array_get_object(servicesv, s, &servicev);
                if (r < 0)
                        return r;

                if (varlink_object_get_string(servicev, "address", &address) < 0)
                        return -EUCLEAN;

                if (varlink_object_get_object(servicev, "executable", &executablev) >= 0) {
                        r = varlink_object_get_string(executablev, "path", &executable);
                        if (r < 0)
                                return r;
                }

                varlink_object_get_bool(executablev, "activate_at_startup", &activate);

                r = varlink_object_get_array(servicev, "interfaces", &interfacesv);
                if (r < 0)
                        return r;

                n_interfaces = varlink_array_get_n_elements(interfacesv);
                interfaces = malloc(n_interfaces * sizeof(char *));
                for (unsigned long i = 0; i < n_interfaces; i += 1) {
                        r = varlink_array_get_string(interfacesv, i, &interfaces[i]);
                        if (r < 0)
                                return r;
                }

                r = service_new(&service,
                                address,
                                interfaces, n_interfaces,
                                executable,
                                activate,
                                NULL);
                if (r < 0)
                        return r;

                r = manager_find_service_by_address(m, &service_old, service->address);
                if (r >= 0) {
                        r = manager_remove_service(m, service_old);
                        if (r < 0)
                                return r;
                }

                r = manager_add_service(m, service);
                if (r < 0)
                        return r;

                service = NULL;
        }

        r = manager_update_interface_index(m);
        if (r < 0)
                return r;

        return varlink_call_reply(call, NULL, 0);
}

static long manager_activate_service(Manager *m, Service *service) {
        assert(service->pid < 0);

        manager_unwatch_service(m, service);

        return service_activate(service);
}

static long manager_activate_configured_services(Manager *m) {
        long r;

        for (unsigned long i = 0; i < m->n_services; i += 1) {
                Service *service = m->services[i];

                if (!service->activate_at_startup)
                        continue;

                r = manager_activate_service(m, service);
                if (r < 0)
                        return r;
        }

        return 0;
}

static long manager_reset_failed_services(Manager *m) {
        long r;

        for (unsigned long i = 0; i < m->n_services; i += 1) {
                Service *service = m->services[i];

                if (!service->failed)
                        continue;

                service->failed = false;

                r = manager_watch_service(m, service);
                if (r < 0)
                        return r;
        }

        return 0;
}

static long manager_read_config(Manager *m, const char *config) {
        _cleanup_(fclosep) FILE *f = NULL;
        char json[0xffff];
        _cleanup_(varlink_object_unrefp) VarlinkObject *configv = NULL;
        const char *str;
        VarlinkArray *servicesv;
        long n_services;
        long r;

        f = fopen(config, "re");
        if (!f) {
                /* treat no file the same as '{}' */
                if (errno == ENOENT)
                        return 0;

                return -errno;
        }

        r = fread(json, 1, sizeof(json), f);
        if (r == 0)
                return -ferror(f);

        if (r == sizeof(json))
                return -EFBIG;

        json[r - 1] = '\0';

        r = varlink_object_new_from_json(&configv, json);
        if (r < 0)
                return r;

        if (varlink_object_get_string(configv, "vendor", &str) >= 0)
                m->vendor = strdup(str);

        if (varlink_object_get_string(configv, "product", &str) >= 0)
                m->product = strdup(str);

        if (varlink_object_get_string(configv, "version", &str) >= 0)
                m->version = strdup(str);

        if (varlink_object_get_string(configv, "url", &str) >= 0)
                m->url = strdup(str);

        r = varlink_object_get_array(configv, "services", &servicesv);
        if (r < 0)
                return r;

        n_services = varlink_array_get_n_elements(servicesv);
        if (n_services < 0)
                return n_services;

        for (long s = 0; s < n_services; s += 1) {
                VarlinkObject *servicev;
                VarlinkObject *executablev;
                VarlinkArray *interfacesv;
                const char *address;
                _cleanup_(freep) const char **interfaces = NULL;
                unsigned long n_interfaces;
                const char *executable = NULL;
                bool activate = false;
                _cleanup_(service_freep) Service *service = NULL;

                r = varlink_array_get_object(servicesv, s, &servicev);
                if (r < 0)
                        return r;

                if (varlink_object_get_string(servicev, "address", &address) < 0)
                        return -EUCLEAN;

                if (varlink_object_get_object(servicev, "executable", &executablev) >= 0) {
                        r = varlink_object_get_string(executablev, "path", &executable);
                        if (r < 0)
                                return r;
                }

                varlink_object_get_bool(servicev, "activate_at_startup", &activate);

                r = varlink_object_get_array(servicev, "interfaces", &interfacesv);
                if (r < 0)
                        return r;

                n_interfaces = varlink_array_get_n_elements(interfacesv);
                interfaces = malloc(n_interfaces * sizeof(char *));
                for (unsigned long i = 0; i < n_interfaces; i += 1) {
                        r = varlink_array_get_string(interfacesv, i, &interfaces[i]);
                        if (r < 0)
                                return r;
                }

                r = service_new(&service,
                                address,
                                interfaces, n_interfaces,
                                executable,
                                activate,
                                NULL);
                if (r < 0)
                        return r;

                r = manager_add_service(m, service);
                if (r < 0)
                        return r;

                service = NULL;
        }

        return 0;
}

int main(int argc, char **argv) {
        _cleanup_(manager_freep) Manager *m = NULL;
        const char *address;
        int fd = -1;
        sigset_t mask;
        struct epoll_event ev = {};
        bool exit = false;
        int timeout = -1;
        long r;

        r = manager_new(&m);
        if (r < 0)
                return EXIT_FAILURE;

        address = argv[1];
        if (!address) {
                fprintf(stderr, "Error: missing address.\n");

                return EXIT_FAILURE;
        }

        /* An activator passed us our connection. */
        if (read(3, NULL, 0) == 0)
                fd = 3;

        r = varlink_service_new(&m->service,
                                "Varlink",
                                "Resolver",
                                VERSION,
                                "https://github.com/varlink/org.varlink.resolver",
                                address,
                                fd);
        if (r < 0)
                return EXIT_FAILURE;

        r = varlink_service_add_interface(m->service, org_varlink_resolver_varlink,
                                          "Resolve", org_varlink_resolver_Resolve, m,
                                          "GetInfo", org_varlink_resolver_GetInfo, m,
                                          NULL);
        if (r < 0)
                return EXIT_FAILURE;

        r = varlink_service_add_interface(m->service, org_varlink_activator_varlink,
                                          "GetConfig", org_varlink_activator_GetConfig, m,
                                          "AddServices", org_varlink_activator_AddServices, m,
                                          NULL);
        if (r < 0)
                return EXIT_FAILURE;

        m->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (m->epoll_fd < 0)
                return EXIT_FAILURE;

        ev.events = EPOLLIN;
        ev.data.fd = varlink_service_get_fd(m->service);
        if (epoll_ctl(m->epoll_fd, EPOLL_CTL_ADD, varlink_service_get_fd(m->service), &ev) < 0)
                return EXIT_FAILURE;

        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        m->signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (m->signal_fd < 0)
                return EXIT_FAILURE;

        ev.events = EPOLLIN;
        ev.data.fd = m->signal_fd;
        if (epoll_ctl(m->epoll_fd, EPOLL_CTL_ADD, m->signal_fd, &ev) < 0)
                return EXIT_FAILURE;

        if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
                return EXIT_FAILURE;

        if (argv[2]) {
                r = manager_read_config(m, argv[2]);
                if (r < 0) {
                        fprintf(stderr, "Error: reading configuration: %s.\n", strerror(-r));

                        return EXIT_FAILURE;
                }
        }

        r = manager_update_interface_index(m);
        if (r < 0)
                return EXIT_FAILURE;

        r = manager_activate_configured_services(m);
        if (r < 0)
                return EXIT_FAILURE;

        while (!exit) {
                int n;

                n = epoll_wait(m->epoll_fd, &ev, 1, timeout);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;

                        return EXIT_FAILURE;
                }

                if (n == 0) {
                        timeout = -1;

                        r = manager_reset_failed_services(m);
                        if (r < 0)
                                return r;

                        continue;
                }

                if (ev.data.fd == varlink_service_get_fd(m->service)) {
                        r = varlink_service_process_events(m->service);
                        if (r < 0) {
                                fprintf(stderr, "varlink: %s\n", strerror(-r));

                                if (r != -EPIPE)
                                        return EXIT_FAILURE;
                        }

                } else if (ev.data.fd == m->signal_fd) {
                        struct signalfd_siginfo fdsi;
                        long size;

                        size = read(m->signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
                        if (size != sizeof(struct signalfd_siginfo))
                                continue;

                        switch (fdsi.ssi_signo) {
                                case SIGTERM:
                                case SIGINT:
                                        exit = true;
                                        break;

                                case SIGCHLD:
                                        for (;;) {
                                                siginfo_t si = {};
                                                Service *service;

                                                if (waitid(P_ALL, 0, &si, WEXITED|WNOHANG) < 0) {
                                                        if (errno == EINTR)
                                                                continue;

                                                        if (errno == ECHILD)
                                                                break;

                                                        return -errno;
                                                }

                                                if (si.si_pid < 0)
                                                        return EXIT_FAILURE;

                                                if (si.si_pid == 0)
                                                        break;

                                                r = manager_find_service_by_pid(m, &service, si.si_pid);
                                                if (r < 0) {
                                                        if (r == -ESRCH)
                                                                break;

                                                        return EXIT_FAILURE;
                                                }

                                                service->pid = -1;

                                                if (si.si_code == CLD_EXITED && si.si_status == 0) {
                                                        r = manager_watch_service(m, service);
                                                        if (r < 0)
                                                                return EXIT_FAILURE;

                                                        break;
                                                }

                                                if (si.si_code == CLD_EXITED)
                                                        fprintf(stderr, "%s: exit code: %s\n", service->executable, strerror(si.si_status));
                                                else if (si.si_code == CLD_KILLED || si.si_code == CLD_DUMPED)
                                                        fprintf(stderr, "%s: killed by signal: %s\n", service->executable, strsignal(si.si_status));
                                                else
                                                        fprintf(stderr, "%s: status %i:%i\n", service->executable, si.si_code, si.si_status);

                                                if (service_reset(service) < 0)
                                                        return EXIT_FAILURE;

                                                service->failed = true;
                                                timeout = 1000;
                                                fprintf(stderr, "%s: disable re-execution for %i msec\n", service->executable, timeout);
                                        }

                                        break;

                                default:
                                        abort();
                        }

                } else {
                        Service *service = ev.data.ptr;

                        r = manager_activate_service(m, service);
                        if (r < 0)
                                return EXIT_FAILURE;
                }
        }

        return EXIT_SUCCESS;
}
