Name: rustyrig
Version: 20260920.05
Release: 1%{?dist}
Summary: RustyRig remote radio software
License: MIT
URL: https://github.com/PripyatAutomations/rustyrig-fw
Source0: %{name}-%{version}.tar.gz
BuildRequires: gcc, make, pkgconfig, jq, perl
BuildRequires: pkgconfig(glib-2.0), pkgconfig(sqlite3), pkgconfig(libcurl)
BuildRequires: pkgconfig(gtk+-3.0), pkgconfig(gstreamer-1.0), pkgconfig(gstreamer-base-1.0)
BuildRequires: pkgconfig(libgpiod), pkgconfig(hamlib), ncurses-devel
BuildRequires: systemd-rpm-macros

%description
RustyRig software for remote operation of amateur radio stations.

%package libs
Summary: RustyRig shared libraries
%description libs
Runtime libraries used by RustyRig programs.

%package server
Summary: RustyRig radio server
Requires: %{name}-libs = %{version}-%{release}, systemd
%description server
Backend server for remote radio operation.

%package client
Summary: RustyRig GTK client
Requires: %{name}-libs = %{version}-%{release}, gtk3, gstreamer1
%description client
GTK client for accessing a RustyRig server.

%package client-tui
Summary: RustyRig terminal client
Requires: %{name}-libs = %{version}-%{release}, ncurses
%description client-tui
Terminal-only RustyRig client.

%package fwdsp
Summary: RustyRig GStreamer audio DSP service
Requires: %{name}-libs = %{version}-%{release}, gstreamer1, gstreamer1-plugins-base
%description fwdsp
GStreamer-based audio codec and pipeline service.

%package callsign-lookup
Summary: RustyRig callsign lookup service
Requires: %{name}-libs = %{version}-%{release}, libcurl, sqlite
%description callsign-lookup
Callsign lookup helper using local databases and the QRZ XML API.

%prep
%autosetup

%build
%make_build PROFILE=radio
cp -f bin/rrclient rrclient-gtk
%make_build PROFILE=radio USE_GTK=false BUILD_DIR=build/radio-tui bin/rrclient
%make_build -C callsign-lookup

%install
install -Dpm0755 bin/rrserver %{buildroot}%{_bindir}/rrserver
install -Dpm0755 rrclient-gtk %{buildroot}%{_bindir}/rrclient
install -Dpm0755 bin/rrclient %{buildroot}%{_bindir}/rrclient-tui
install -Dpm0755 bin/fwdsp %{buildroot}%{_bindir}/fwdsp
install -Dpm0755 bin/callsign-lookup %{buildroot}%{_bindir}/callsign-lookup
install -Dpm0755 librustyaxe.so %{buildroot}%{_libdir}/librustyaxe.so.0
install -Dpm0755 librrprotocol.so %{buildroot}%{_libdir}/librrprotocol.so.0
install -Dpm0755 libfwdspmgr.so %{buildroot}%{_libdir}/libfwdspmgr.so.0
install -Dpm0644 packaging/rrserver.service %{buildroot}%{_unitdir}/rustyrig-server.service
install -Dpm0755 packaging/rrserver.rc %{buildroot}%{_sysconfdir}/init.d/rrserver
install -Dpm0644 packaging/rustyrig.tmpfiles %{buildroot}%{_tmpfilesdir}/rustyrig.conf
for f in rrserver.cfg.example rrclient.cfg.example callsign-lookup.cfg.example callsign-lookup.srv.cfg.example callsign-lookup.cli.cfg.example; do install -Dpm0644 config/$f %{buildroot}%{_sysconfdir}/rustyrig/$f; done
install -Dpm0644 sql/sqlite.master.sql %{buildroot}%{_sharedstatedir}/rustyrig/sql/sqlite.master.sql
install -Dpm0644 sql/sqlite.master.preload.sql %{buildroot}%{_sharedstatedir}/rustyrig/sql/sqlite.master.preload.sql
install -Dpm0755 tools/dummy-rigctld.sh %{buildroot}%{_sharedstatedir}/rustyrig/tools/dummy-rigctld.sh
mkdir -p %{buildroot}%{_sharedstatedir}/rustyrig/{db,recordings,modems,www,help} %{buildroot}%{_localstatedir}/log/rustyrig
find www -mindepth 1 -maxdepth 1 ! -name .git -exec cp -a {} %{buildroot}%{_sharedstatedir}/rustyrig/www/ \;
find help -mindepth 1 -maxdepth 1 ! -name .git -exec cp -a {} %{buildroot}%{_sharedstatedir}/rustyrig/help/ \;

%pre server
getent group rustyrig >/dev/null || groupadd -r rustyrig
getent passwd rustyrig >/dev/null || useradd -r -g rustyrig -d /var/lib/rustyrig -s /sbin/nologin rustyrig
%post libs -p /sbin/ldconfig
%postun libs -p /sbin/ldconfig
%systemd_post rustyrig-server.service
%preun server
%systemd_preun rustyrig-server.service
%postun server
%systemd_postun_with_restart rustyrig-server.service

%files libs
%{_libdir}/librustyaxe.so.0
%{_libdir}/librrprotocol.so.0
%{_libdir}/libfwdspmgr.so.0
%files server
%{_bindir}/rrserver
%{_unitdir}/rustyrig-server.service
%{_tmpfilesdir}/rustyrig.conf
%config(noreplace) %{_sysconfdir}/init.d/rrserver
%config(noreplace) %{_sysconfdir}/rustyrig/rrserver.cfg.example
%config(noreplace) %{_sysconfdir}/rustyrig/callsign-lookup.srv.cfg.example
%{_sharedstatedir}/rustyrig
%files client
%{_bindir}/rrclient
%config(noreplace) %{_sysconfdir}/rustyrig/rrclient.cfg.example
%config(noreplace) %{_sysconfdir}/rustyrig/callsign-lookup.cli.cfg.example
%files client-tui
%{_bindir}/rrclient-tui
%files fwdsp
%{_bindir}/fwdsp
%files callsign-lookup
%{_bindir}/callsign-lookup
%config(noreplace) %{_sysconfdir}/rustyrig/callsign-lookup.cfg.example
