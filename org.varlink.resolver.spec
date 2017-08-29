Name:           org.varlink.resolver
Version:        1
Release:        1%{?dist}
Summary:        Varlink Service Activator
License:        ASL2.0
URL:            https://github.com/varlink/org.varlink.resolver
Source0:        https://github.com/varlink/org.varlink.resolver/archive/%{name}-%{version}.tar.gz
BuildRequires:  autoconf automake pkgconfig
BuildRequires:  libvarlink-devel

%description
Varlink service registry and on-demand activator.

%prep
%setup -q

%build
./autogen.sh
%configure
make %{?_smp_mflags}

%install
%make_install

%files
%license AUTHORS
%license COPYRIGHT
%license LICENSE
%{_bindir}/org.varlink.resolver

%changelog
* Tue Aug 29 2017 <info@varlink.org> 1-1
- org.varlink.resolver 1
