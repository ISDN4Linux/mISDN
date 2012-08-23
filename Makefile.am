CLEANFILES = *~

export PATCH FIND SED DIFF GREP AWK SORT IFNAMES INSTALL_MOD_PATH

SUBDIRS = standalone

modules modules_install unpatched_tree patched_tree modules_ quilt_tree:
	$(MAKE) -C standalone INSTALL_MOD_PATH=$(INSTALL_MOD_PATH) $@

