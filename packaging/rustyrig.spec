Name: rustyrig
Version: 1.0.2
Release: 2%{?dist}
Summary: RustyRig remote radio software
License: MIT
URL: https://github.com/PripyatAutomations/rustyrig-fw
Source0: %{name}-%{version}.tar.gz
BuildRequires: gcc, make, pkgconfig, jq, perl
BuildRequires: pkgconfig(librustyaxe), pkgconfig(librustyirc), pkgconfig(librrprotocol), pkgconfig(glib-2.0), pkgconfig(sqlite3), pkgconfig(libcurl)
BuildRequires: pkgconfig(gtk+-3.0), pkgconfig(gstreamer-1.0), pkgconfig(gstreamer-base-1.0)
BuildRequires: pkgconfig(libgpiod), pkgconfig(hamlib), ncurses-devel
BuildRequires: systemd-rpm-macros

%description
RustyRig software for remote operation of amateur radio stations.

%package libs
Summary: RustyRig shared libraries
%description libs
Runtime libraries used by RustyRig programs.

%package -n rustyrig-rrbridge
Summary: RustyRig SDR bridge and KiwiSDR module
%description -n rustyrig-rrbridge
Standalone SDR bridge executable and KiwiSDR driver for clients and servers.

%package server
Summary: RustyRig radio server
Requires: %{name}-libs = %{version}-%{release}, jq, sqlite, systemd
Recommends: rustyrig-rrbridge, rustyrig-callsign-lookup
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
Recommends: rustyrig-rrbridge, rustyrig-callsign-lookup
%description client
RustyRig client with TUI interface; the GTK interface ships separately
in rustyrig-client-gtk as a loadable module.
%package client-gtk
Summary: RustyRig GTK client module and GStreamer pipeline editor
Requires: %{name}-client = %{version}-%{release}, gtk3
%description client-gtk
GTK frontend for the RustyRig client, loaded from
/usr/lib/rustyrig/modules/rrclient by the core client's [modules] configuration,
and a standalone graph editor for GStreamer pipelines.

%package fwdsp
Summary: RustyRig GStreamer audio DSP service
Requires: %{name}-libs = %{version}-%{release}, gstreamer1, gstreamer1-plugins-base
%description fwdsp
GStreamer-based audio codec and pipeline service.

%prep
%autosetup

%build
%make_build PROFILE=radio LIBRUSTYAXE_PROVIDER=system LIBRRPROTOCOL_PROVIDER=system LIBRUSTYIRC_PROVIDER=system

%install
install -Dpm0755 bin/rrbridge %{buildroot}%{_bindir}/rrbridge
install -Dpm0755 bin/rrbridge-kiwi.so %{buildroot}/usr/lib/rustyrig/modules/rrbridge/rrbridge-kiwi.so
install -Dpm0755 bin/rrserver %{buildroot}%{_bindir}/rrserver
for module in bin/rrserver-gps*.so; do install -Dpm0755 "$module" "%{buildroot}/usr/lib/rustyrig/modules/rrserver/${module##*/}"; done
install -Dpm0755 tools/rr-get-audit-log %{buildroot}%{_bindir}/rr-get-audit-log
install -Dpm0755 tools/rr-get-chat-log %{buildroot}%{_bindir}/rr-get-chat-log
install -Dpm0755 tools/rr-get-ptt-log %{buildroot}%{_bindir}/rr-get-ptt-log
install -Dpm0755 bin/rrclient %{buildroot}%{_bindir}/rrclient
install -Dpm0755 bin/rrclient-gtk.so %{buildroot}/usr/lib/rustyrig/modules/rrclient/rrclient-gtk.so
install -Dpm0755 bin/rrpipelines %{buildroot}%{_bindir}/rrpipelines
install -Dpm0755 bin/fwdsp %{buildroot}%{_bindir}/fwdsp
install -Dpm0755 libfwdspmgr.so %{buildroot}%{_libdir}/libfwdspmgr.so.0
install -Dpm0644 packaging/rrserver.service %{buildroot}%{_unitdir}/rustyrig-server.service
install -Dpm0755 packaging/rrserver.rc %{buildroot}%{_sysconfdir}/init.d/rrserver
install -Dpm0644 packaging/rustyrig.tmpfiles %{buildroot}%{_tmpfilesdir}/rustyrig.conf
install -Dpm0644 packaging/rustyrig-client.desktop %{buildroot}%{_datadir}/applications/rustyrig-client.desktop
install -Dpm0644 res/rustyrig.png %{buildroot}%{_datadir}/icons/hicolor/48x48/apps/rustyrig.png
for f in rrserver.cfg rrclient.cfg callsign-lookup.srv.cfg callsign-lookup.cli.cfg; do install -Dpm0644 config/$f %{buildroot}%{_sysconfdir}/rustyrig/$f; done
install -Dpm0644 config/radio.config.json %{buildroot}%{_sysconfdir}/rustyrig/radio.config.json
install -Dpm0644 config/ua-bans %{buildroot}%{_sysconfdir}/rustyrig/ua-bans.txt
install -Dpm0644 sql/sqlite.master.sql %{buildroot}%{_sharedstatedir}/rustyrig/sql/sqlite.master.sql
install -Dpm0644 sql/sqlite.master.preload.sql %{buildroot}%{_sharedstatedir}/rustyrig/sql/sqlite.master.preload.sql
install -Dpm0755 tools/dummy-rigctld.sh %{buildroot}%{_sharedstatedir}/rustyrig/tools/dummy-rigctld.sh
mkdir -p %{buildroot}%{_sharedstatedir}/rustyrig/{db,recordings,modems,www,help}
mkdir -p %{buildroot}%{_localstatedir}/log/rustyrig
chmod 0770 %{buildroot}%{_localstatedir}/log/rustyrig
if [ -d www ]; then find www -mindepth 1 -maxdepth 1 ! -name .git -exec cp -a {} %{buildroot}%{_sharedstatedir}/rustyrig/www/ \; ; fi
find help -mindepth 1 -maxdepth 1 ! -name .git -exec cp -a {} %{buildroot}%{_sharedstatedir}/rustyrig/help/ \;

%pre server
getent group rustyrig >/dev/null || groupadd -r rustyrig
getent passwd rustyrig >/dev/null || useradd -r -g rustyrig -d /var/lib/rustyrig -s /sbin/nologin rustyrig
getent group rustyrig-modems >/dev/null || groupadd -r rustyrig-modems
getent passwd rustyrig-modems >/dev/null || useradd -r -g rustyrig-modems -d /var/lib/rustyrig -s /sbin/nologin rustyrig-modems
%post libs -p /sbin/ldconfig
%postun libs -p /sbin/ldconfig
%systemd_post rustyrig-server.service
%preun server
%systemd_preun rustyrig-server.service
%postun server
%systemd_postun_with_restart rustyrig-server.service

%files -n rustyrig-rrbridge
%{_bindir}/rrbridge
/usr/lib/rustyrig/modules/rrbridge/rrbridge-kiwi.so
%files libs
%{_libdir}/libfwdspmgr.so.0
%files server
%dir %attr(0770,rustyrig,rustyrig) %{_sharedstatedir}/rustyrig/db
%dir %attr(0770,rustyrig,rustyrig) %{_sharedstatedir}/rustyrig/recordings
%dir %attr(0770,rustyrig-modems,rustyrig-modems) %{_sharedstatedir}/rustyrig/modems
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
%dir %attr(0770,rustyrig,rustyrig) %{_sharedstatedir}/rustyrig/db
%dir %attr(0770,rustyrig,rustyrig) %{_sharedstatedir}/rustyrig/recordings
%{_bindir}/rrclient
%config(noreplace) %{_sysconfdir}/rustyrig/rrclient.cfg
%config(noreplace) %{_sysconfdir}/rustyrig/callsign-lookup.cli.cfg
%dir /usr/lib/rustyrig/modules/rrclient
%files client-gtk
/usr/lib/rustyrig/modules/rrclient/rrclient-gtk.so
%{_bindir}/rrpipelines
%{_datadir}/applications/rustyrig-client.desktop
%{_datadir}/icons/hicolor/48x48/apps/rustyrig.png
%files fwdsp
%{_bindir}/fwdsp
