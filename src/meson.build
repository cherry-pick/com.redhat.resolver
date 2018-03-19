com_redhat_resolver_sources = files('''
        main.c
        service.c
        service.h
        util.h
'''.split())

com_redhat_resolver_varlink_c_inc = custom_target(
        'com.redhat.resolver.varlink',
        input : 'com.redhat.resolver.varlink',
        output : 'com.redhat.resolver.varlink.c.inc',
        command : [varlink_wrapper_py, '@INPUT@', '@OUTPUT@'])

org_varlink_resolver_varlink_c_inc = custom_target(
        'org.varlink.resolver.varlink',
        input : 'org.varlink.resolver.varlink',
        output : 'org.varlink.resolver.varlink.c.inc',
        command : [varlink_wrapper_py, '@INPUT@', '@OUTPUT@'])

exe = executable(
        'com.redhat.resolver',
        com_redhat_resolver_sources,
        org_varlink_resolver_varlink_c_inc,
        com_redhat_resolver_varlink_c_inc,
        dependencies : [libvarlink],
        install : true)
