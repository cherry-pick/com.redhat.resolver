# com.redhat.resolver

Service [activator](https://github.com/varlink/com.redhat.resolver/blob/master/src/com.redhat.resolver.varlink) implementing
the common varlink [resolver](https://github.com/varlink/com.redhat.resolver/blob/master/src/org.varlink.resolver.varlink) interface.

Listening at _unix:/run/org.varlink.resolver_ to resolve varlink interface names to varlink addresses. Configured services will be
activated on-demand.
