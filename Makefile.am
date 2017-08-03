# ------------------------------------------------------------------------------
# autotools

SHELL = bash
ACLOCAL_AMFLAGS = -I build/m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
AUTOMAKE_OPTIONS = color-tests parallel-tests

GCC_COLORS ?= 'ooh, shiny!'
export GCC_COLORS

# remove targets if the command fails
.DELETE_ON_ERROR:

# keep intermediate files
.SECONDARY:

# keep the test-suite.log and Makefile around at all times
.PRECIOUS: $(TEST_SUITE_LOG) Makefile

BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST =
bin_PROGRAMS =

INSTALL_EXEC_HOOKS =
UNINSTALL_EXEC_HOOKS =
.PHONY: $(INSTALL_EXEC_HOOKS) $(UNINSTALL_EXEC_HOOKS)
install-exec-hook: $(INSTALL_EXEC_HOOKS)
uninstall-hook: $(UNINSTALL_DATA_HOOKS) $(UNINSTALL_EXEC_HOOKS)

AM_CPPFLAGS = \
	-include $(top_builddir)/build/config.h \
	-I $(top_srcdir)/src \
	-I $(top_builddir)/src \
	$(OUR_CPPFLAGS)

AM_CFLAGS = $(OUR_CFLAGS)
AM_LDFLAGS = $(OUR_LDFLAGS)

# ------------------------------------------------------------------------------
# varlink interface files

%.varlink.h: %.varlink
	$(AM_V_GEN) \
		echo -e "#pragma once\n" > $@ && \
		echo "static const char *$(subst .,_,$(patsubst %.h,%,$(notdir $<))) = R\"(" >> $@ && \
		cat $< >> $@ && \
		echo ")\";" >> $@

# ------------------------------------------------------------------------------
bin_PROGRAMS += \
	org.varlink.resolver

org_varlink_resolver_SOURCES = \
	src/main.c \
	src/org.varlink.activator.varlink.h \
	src/org.varlink.resolver.varlink.h \
	src/service.c \
	src/service.h \
	src/util.h

org_varlink_resolver_CFLAGS = \
	$(AM_CFLAGS) \
	$(VARLINK_CFLAGS)

org_varlink_resolver_LDADD = \
	$(VARLINK_LIBS)

BUILT_SOURCES += \
	src/org.varlink.activator.varlink.h \
	src/org.varlink.resolver.varlink.h

EXTRA_DIST += \
	src/org.varlink.activator.varlink \
	src/org.varlink.resolver.varlink

# ------------------------------------------------------------------------------
if HAVE_ASTYLE
check-format:
	@for f in src/*.[ch]; do \
		echo "  CHECK-FORMAT $$f"; \
		$(ASTYLE) --quiet --options=$(abs_srcdir)/.astylerc < $$f | cmp -s $$f -; \
		if [ $$? -ne 0 ]; then \
			$(ASTYLE) --quiet --options=$(abs_srcdir)/.astylerc < $$f | diff -u $$f -; \
			exit 1; \
		fi; \
	done

format:
	@for f in src/*.[ch]; do \
		echo "  FORMAT $$f"; \
		$(ASTYLE) --quiet --options=$(abs_srcdir)/.astylerc $$f; \
	done
endif
.PHONY: check-format
.PHONY: format

install-tree: all
	rm -rf $(abs_builddir)/install-tree
	$(MAKE) install DESTDIR=$(abs_builddir)/install-tree
	tree $(abs_builddir)/install-tree
.PHONY: install-tree
