Vendor:       SuSE GmbH, Nuernberg, Germany
Distribution: SuSE Linux 8.2 (i386)
Name:         km_mISDN
Release:      14
Packager:     feedback@suse.de

Copyright:    Karsten Keil GPL
Group:        unsorted
Autoreqprov:  on
Version:      1.0
Summary:      modular ISDN driver architecture
Source:       mISDN-%{version}-%{release}.tar.bz2
#Patch:       isdn4k-utils.dif
Buildroot:    /var/tmp/mISDN.build

%description
This package provides the mISDN sourcecode for kernelmodules
Attention!!! These modules are BETA code and experimental, they may be
crash your machine. Here is no support from SuSE for it.

Authors:
--------
	Karsten Keil

SuSE series: unsorted

%prep
%setup -n mISDN
#%patch

%build
mv Makefile.standalone Makefile

%install
rm -f -r $RPM_BUILD_ROOT
DESTDIR=$RPM_BUILD_ROOT/usr/src/kernel-modules/mISDN
mkdir -p $DESTDIR
install -p Makefile* $DESTDIR
install -p Rules.make.ext $DESTDIR
install -p add.config $DESTDIR
mkdir -p $DESTDIR/newinclude/linux
install -p include/linux/*.h $DESTDIR/newinclude/linux
mkdir -p $DESTDIR/drivers/isdn/hardware/mISDN
install -p drivers/isdn/hardware/mISDN/Makefile $DESTDIR/drivers/isdn/hardware/mISDN
install -p drivers/isdn/hardware/mISDN/*.[ch] $DESTDIR/drivers/isdn/hardware/mISDN
install -p drivers/isdn/hardware/mISDN/Rules.mISDN.v2.4 $DESTDIR/drivers/isdn/hardware/mISDN/Rules.mISDN

#
%{?suse_check}

%clean

%files
%dir %attr (-,root,root) /usr/src/kernel-modules/mISDN
%attr (-,root,root) /usr/src/kernel-modules/mISDN/*

%changelog -n km_mISDN

* Tue Jul 01 2003 - kkeil@suse.de
  - first version
