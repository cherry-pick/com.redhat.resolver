Name:           com.redhat.resolver
Version:        1
Release:        1%{?dist}
Summary:        Varlink Service Activator
License:        ASL2.0
URL:            https://github.com/varlink/com.redhat.resolver
Source0:        https://github.com/varlink/com.redhat.resolver/archive/%{name}-%{version}.tar.gz
BuildRequires:  meson
BuildRequires:  gcc
BuildRequires:  pkgconfig
BuildRequires:  libvarlink-devel

%description
Varlink service registry and on-demand activator.

%prep
%setup -q

%build
%meson
%meson_build

%check
export LC_CTYPE=C.utf8
%meson_test

%install
%meson_install

%files
%license LICENSE
%{_bindir}/com.redhat.resolver

%changelog
* Mon Mar 19 2018 <info@varlink.org> 1-1
- com.redhat.resolver 1
