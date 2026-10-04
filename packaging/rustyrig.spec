Name: rustyrig
Version: 20261004.02
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
Requires: %{name}-libs = %{version}-%{release}, jq, sqlite, systemd
%description server
Backend server for remote radio operation.

%package -n rustyrig-server-gpsd
Summary: GPSD receiver module for RustyRig server
Requires: %{name}-server = %{version}-%{release}
%description -n rustyrig-server-gpsd
Optional GPSD connection for station and rig coordinates.

%package client
Summary: RustyRig client
Requires: %{name}-libs = %{version}-%{release}, glib2, gstreamer1
%description client
RustyRig client with TUI interface; the GTK interface ships separately
in rustyrig-client-gtk as a loadable module.
%package client-gtk
Summary: RustyRig GTK client module
Requires: %{name}-client = %{version}-%{release}, gtk3
%description client-gtk
GTK frontend for the RustyRig client, loaded from
/usr/lib/rustyrig/modules/rrclient by the core client's [modules] configuration.

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
%make_build -C callsign-lookup

%install
install -Dpm0755 bin/rrserver %{buildroot}%{_bindir}/rrserver
for module in bin/rrserver-gps*.so; do install -Dpm0755 "$module" "%{buildroot}/usr/lib/rustyrig/modules/rrserver/${module##*/}"; done
install -Dpm0755 tools/rr-get-audit-log %{buildroot}%{_bindir}/rr-get-audit-log
install -Dpm0755 tools/rr-get-chat-log %{buildroot}%{_bindir}/rr-get-chat-log
install -Dpm0755 tools/rr-get-ptt-log %{buildroot}%{_bindir}/rr-get-ptt-log
install -Dpm0755 bin/rrclient %{buildroot}%{_bindir}/rrclient
install -Dpm0755 bin/rrclient-gtk.so %{buildroot}/usr/lib/rustyrig/modules/rrclient/rrclient-gtk.so
install -Dpm0755 bin/fwdsp %{buildroot}%{_bindir}/fwdsp
install -Dpm0755 bin/callsign-lookup %{buildroot}%{_bindir}/callsign-lookup
install -Dpm0755 librustyaxe.so %{buildroot}%{_libdir}/librustyaxe.so.0
install -Dpm0755 librrprotocol.so %{buildroot}%{_libdir}/librrprotocol.so.0
install -Dpm0755 libfwdspmgr.so %{buildroot}%{_libdir}/libfwdspmgr.so.0
install -Dpm0644 packaging/rrserver.service %{buildroot}%{_unitdir}/rustyrig-server.service
install -Dpm0755 packaging/rrserver.rc %{buildroot}%{_sysconfdir}/init.d/rrserver
install -Dpm0644 packaging/rustyrig.tmpfiles %{buildroot}%{_tmpfilesdir}/rustyrig.conf
install -Dpm0644 packaging/rustyrig-client.desktop %{buildroot}%{_datadir}/applications/rustyrig-client.desktop
install -Dpm0644 res/rustyrig.png %{buildroot}%{_datadir}/icons/hicolor/48x48/apps/rustyrig.png
for f in rrserver.cfg rrclient.cfg callsign-lookup.cfg callsign-lookup.srv.cfg callsign-lookup.cli.cfg; do install -Dpm0644 config/$f %{buildroot}%{_sysconfdir}/rustyrig/$f; done
install -Dpm0644 config/radio.config.json %{buildroot}%{_sysconfdir}/rustyrig/radio.config.json
install -Dpm0644 config/ua-bans %{buildroot}%{_sysconfdir}/rustyrig/ua-bans.txt
install -Dpm0644 sql/sqlite.master.sql %{buildroot}%{_sharedstatedir}/rustyrig/sql/sqlite.master.sql
install -Dpm0644 sql/sqlite.master.preload.sql %{buildroot}%{_sharedstatedir}/rustyrig/sql/sqlite.master.preload.sql
install -Dpm0755 tools/dummy-rigctld.sh %{buildroot}%{_sharedstatedir}/rustyrig/tools/dummy-rigctld.sh
mkdir -p %{buildroot}%{_sharedstatedir}/rustyrig/{db,recordings,modems,www,help}
mkdir -p %{buildroot}%{_localstatedir}/log/rustyrig
chown rustyrig:rustyrig %{buildroot}%{_localstatedir}/log/rustyrig
chmod 0770 %{buildroot}%{_localstatedir}/log/rustyrig
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
%{_bindir}/rr-get-audit-log
%{_bindir}/rr-get-chat-log
%{_bindir}/rr-get-ptt-log
%{_unitdir}/rustyrig-server.service
%{_tmpfilesdir}/rustyrig.conf
%config(noreplace) %{_sysconfdir}/init.d/rrserver
%config(noreplace) %{_sysconfdir}/rustyrig/rrserver.cfg
%config(noreplace) %{_sysconfdir}/rustyrig/radio.config.json
%config(noreplace) %{_sysconfdir}/rustyrig/callsign-lookup.srv.cfg
%config(noreplace) %{_sysconfdir}/rustyrig/ua-bans.txt
%dir %attr(0770,rustyrig,rustyrig) %{_localstatedir}/log/rustyrig
%{_sharedstatedir}/rustyrig
/usr/lib/rustyrig/modules/rrserver/rrserver-gps-nmea.so
%files -n rustyrig-server-gpsd
/usr/lib/rustyrig/modules/rrserver/rrserver-gpsd.so
%files client
%{_bindir}/rrclient
%config(noreplace) %{_sysconfdir}/rustyrig/rrclient.cfg
%config(noreplace) %{_sysconfdir}/rustyrig/callsign-lookup.cli.cfg
%dir /usr/lib/rustyrig/modules/rrclient
%files client-gtk
/usr/lib/rustyrig/modules/rrclient/rrclient-gtk.so
%{_datadir}/applications/rustyrig-client.desktop
%{_datadir}/icons/hicolor/48x48/apps/rustyrig.png
%files fwdsp
%{_bindir}/fwdsp
%files callsign-lookup
%{_bindir}/callsign-lookup
%config(noreplace) %{_sysconfdir}/rustyrig/callsign-lookup.cfg
