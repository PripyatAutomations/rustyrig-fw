UNAME_M=$(uname -m)
case ${UNAME_M} in
   # XXX: Add aarch64 translations
   x86_64)
      ARCH=amd64
      ;;
     *)
      # Hope for the best!
      ARCH=${UNAME_M}
      ;;
esac

DEBS="../libfwdspmgr_*_${ARCH}.deb"
DEBS="$DEBS ../libfwdspmgr-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../libfwdspmgr-dev_*_${ARCH}.deb"
DEBS="$DEBS ../librrprotocol_*_${ARCH}.deb"
DEBS="$DEBS ../librrprotocol-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../librrprotocol-dev_*_${ARCH}.deb"
DEBS="$DEBS ../librustyaxe_*_${ARCH}.deb"
DEBS="$DEBS ../librustyaxe-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../librustyaxe-dev_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-callsign-lookup_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-callsign-lookup-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-client_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-client-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-client-gtk_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-client-gtk-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-fwdsp_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-fwdsp-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-server_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-server-dbgsym_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-server-gpsd_*_${ARCH}.deb"
DEBS="$DEBS ../rustyrig-server-gpsd-dbgsym_*_${ARCH}.deb"

dpkg -i ${DEBS}
