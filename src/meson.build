org_varlink_resolver_sources = files('''
        main.c
        service.c
        service.h
        util.h
'''.split())

org_varlink_resolver_varlink_c_inc = custom_target(
        'org.varlink.resolver.varlink',
        input : 'org.varlink.resolver.varlink',
        output : 'org.varlink.resolver.varlink.c.inc',
        command : [varlink_wrapper_py, '@INPUT@', '@OUTPUT@'])

org_varlink_activator_varlink_c_inc = custom_target(
        'org.varlink.activator.varlink',
        input : 'org.varlink.activator.varlink',
        output : 'org.varlink.activator.varlink.c.inc',
        command : [varlink_wrapper_py, '@INPUT@', '@OUTPUT@'])

exe = executable(
        'org.varlink.resolver',
        org_varlink_resolver_sources,
        org_varlink_resolver_varlink_c_inc,
        org_varlink_activator_varlink_c_inc,
        dependencies : [libvarlink],
        install : true)
