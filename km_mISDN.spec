Vendor:       SuSE GmbH, Nuernberg, Germany
Distribution: SuSE Linux 8.2 (i386)
Name:         km_newmISDN
Release:      2
Packager:     feedback@suse.de

Copyright:    Karsten Keil GPL
Group:        unsorted
Provides:     mISDNcapi_modules
Autoreqprov:  on
Version:      1.1
Summary:      capi driver for mISDN
Source:       newmISDN-%{version}-%{release}.tar.bz2
#Patch:       isdn4k-utils.dif
Buildroot:    /var/tmp/newmISDN.build

%description
This package provides the new mISDN capidriver sourcecode for kernelmodules
Attention!!! These modules are alpha code and experimental, they may be
crash your machine. Here is no support from SuSE for it.

Authors:
--------
	Karsten Keil

SuSE series: unsorted

%prep
%setup -n newmISDN
#%patch

%build
mv Makefile.standalone Makefile

%install
rm -f -r $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT/usr/src/kernel-modules/newmISDN
mkdir -p $DESTDIR
install -p Makefile* $DESTDIR
install -p Rules.make.ext $DESTDIR
install -p add.config $DESTDIR
mkdir -p $DESTDIR/newinclude/linux
install -p include/linux/*.h $DESTDIR/newinclude/linux
mkdir -p $DESTDIR/drivers/isdn/mISDN
install -p drivers/isdn/mISDN/Makefile $DESTDIR/drivers/isdn/mISDN
install -p drivers/isdn/mISDN/*.[ch] $DESTDIR/drivers/isdn/mISDN

#
%{?suse_check}

%clean

%files
%dir %attr (-,root,root) /usr/src/kernel-modules/newmISDN
%attr (-,root,root) /usr/src/kernel-modules/newmISDN/*

%changelog -n km_newmISDN

* Mon Oct 01 2001 - kkeil@suse.de
  - first version
