Vendor:       SuSE GmbH, Nuernberg, Germany
Distribution: SuSE Linux 8.2 (i386)
Name:         km_newhisax
Release:      18
Packager:     feedback@suse.de

Copyright:    Karsten Keil GPL
Group:        unsorted
Provides:     hisaxcapi_modules
Autoreqprov:  on
Version:      1.0
Summary:      capi driver for hisax
Source:       newhisax.tar.bz2
#Patch:       isdn4k-utils.dif
Buildroot:    /var/tmp/newhisax.build

%description
This package provides the new hisax capidriver sourcecode for kernelmodules
Attention!!! These modules are alpha code and experimental, they may be
crash your machine. Here is no support from SuSE for it.

Authors:
--------
	Karsten Keil

SuSE series: unsorted

%prep
%setup -n newhisax
#%patch

%build
mv Makefile.standalone Makefile

%install
rm -f -r $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT/usr/src/kernel-modules/newhisax
mkdir -p $DESTDIR
install -p Makefile* $DESTDIR
install -p Rules.make.ext $DESTDIR
install -p add.config $DESTDIR
mkdir -p $DESTDIR/newinclude/linux
install -p include/linux/*.h $DESTDIR/newinclude/linux
mkdir -p $DESTDIR/drivers/isdn/hisax
install -p drivers/isdn/hisax/Makefile $DESTDIR/drivers/isdn/hisax
install -p drivers/isdn/hisax/*.[ch] $DESTDIR/drivers/isdn/hisax

#
%{?suse_check}

%clean

%files
%dir %attr (-,root,root) /usr/src/kernel-modules/newhisax
%attr (-,root,root) /usr/src/kernel-modules/newhisax/*

%changelog -n km_newhisax

* Mon Oct 01 2001 - kkeil@suse.de
  - first version
