%define name     baresip
%define ver      0.4.20
%define rel      1

Summary: Modular SIP useragent
Name: %name
Version: %ver
Release: %rel
License: BSD
Group: Applications/Internet
Source0: file://%{name}-%{version}.tar.gz
URL: http://www.creytiv.com/
Vendor: Creytiv
Packager: Alfred E. Heggestad <aeh@db.org>
BuildRoot: /var/tmp/%{name}-build-root

%description
Baresip is a portable and modular SIP User-Agent with audio and video support

%prep
%setup

%build
make RELEASE=1

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install \
%ifarch x86_64
	LIBDIR=/usr/lib64
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/chkconfig --add baresip
/sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_bindir}/* 
%{_libdir}/%name/modules/*.so
/usr/share/%name/*
%doc


%changelog
* Fri Nov 5 2010 Alfred E. Heggestad <aeh@db.org> -
- Initial build.

