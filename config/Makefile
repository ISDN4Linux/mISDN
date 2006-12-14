
all:
	@echo "Please run 'make install'."

install:
	install -D -m755 mISDN $(INSTALL_PREFIX)/usr/sbin/mISDN
	for file in $(shell echo *.xsl); do install -D -m644 $${file} $(INSTALL_PREFIX)/usr/lib/mISDN/$${file}; done
	if [ -d $(INSTALL_PREFIX)/etc/init.d ]; then \
		if [ -e $(INSTALL_PREFIX)/etc/init.d/mISDN ]; then rm -rf $(INSTALL_PREFIX)/etc/init.d/mISDN; fi; \
		ln -s $(INSTALL_PREFIX)/usr/sbin/mISDN $(INSTALL_PREFIX)/etc/init.d/mISDN; \
	fi
