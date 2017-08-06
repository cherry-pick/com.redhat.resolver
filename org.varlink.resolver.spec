%define build_date %(date +"%%a %%b %%d %%Y")
%define build_timestamp %(date +"%%Y%%m%%d.%%H%M%%S")

Name:           org.varlink.resolver
Version:        1
Release:        %{build_timestamp}%{?dist}
Summary:        Varlink Service Activator
License:        ASL2.0
URL:            https://github.com/varlink/org.varlink.resolver
Source0:        https://github.com/varlink/org.varlink.resolver/archive/v%{version}.tar.gz
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
* %{build_date} <info@varlink.org> %{version}-%{build_timestamp}
- %{name} %{version}
