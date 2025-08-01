!define /date DATE "%Y%m%d"
!ifndef OUTFILE
  !define OUTFILE "rrclient.win64.${DATE}.exe"
!endif

Outfile "..\build\${OUTFILE}"
InstallDir $PROGRAMFILES\PripyatAutomations\rustyrig-client

Section
SetOutPath $INSTDIR
# Copy client exe
File "..\build\rrclient.exe"
# Copy needed DLLs
File "C:\msys64\mingw64\bin\libglib-2.0-0.dll"
File "C:\msys64\mingw64\bin\libgdk-3-0.dll"
File "C:\msys64\mingw64\bin\libgstapp-1.0-0.dll"
File "C:\msys64\mingw64\bin\libgobject-2.0-0.dll"
File "C:\msys64\mingw64\bin\libgtk-3-0.dll"
File "C:\msys64\mingw64\bin\libmbedcrypto-16.dll"
File "C:\msys64\mingw64\bin\libmbedtls-21.dll"
File "C:\msys64\mingw64\bin\libgstreamer-1.0-0.dll"
File "C:\msys64\mingw64\bin\libmbedx509-7.dll"
File "C:\msys64\mingw64\bin\libpango-1.0-0.dll"
File "C:\msys64\mingw64\bin\libgstbase-1.0-0.dll"
File "C:\msys64\mingw64\bin\libintl-8.dll"
File "C:\msys64\mingw64\bin\libffi-8.dll"
File "C:\msys64\mingw64\bin\libcairo-gobject-2.dll"
File "C:\msys64\mingw64\bin\libcairo-2.dll"
File "C:\msys64\mingw64\bin\libepoxy-0.dll"
File "C:\msys64\mingw64\bin\libfribidi-0.dll"
File "C:\msys64\mingw64\bin\libgdk_pixbuf-2.0-0.dll"
File "C:\msys64\mingw64\bin\libgio-2.0-0.dll"
File "C:\msys64\mingw64\bin\libpangocairo-1.0-0.dll"
File "C:\msys64\mingw64\bin\libpangowin32-1.0-0.dll"
File "C:\msys64\mingw64\bin\libpcre2-8-0.dll"
File "C:\msys64\mingw64\bin\libgcc_s_seh-1.dll"
File "C:\msys64\mingw64\bin\libwinpthread-1.dll"
File "C:\msys64\mingw64\bin\libgmodule-2.0-0.dll"
File "C:\msys64\mingw64\bin\libharfbuzz-0.dll"
File "C:\msys64\mingw64\bin\libthai-0.dll"
File "C:\msys64\mingw64\bin\libiconv-2.dll"
File "C:\msys64\mingw64\bin\libstdc++-6.dll"
File "C:\msys64\mingw64\bin\libfontconfig-1.dll"
File "C:\msys64\mingw64\bin\libfreetype-6.dll"
File "C:\msys64\mingw64\bin\libpixman-1-0.dll"
File "C:\msys64\mingw64\bin\libpng16-16.dll"
File "C:\msys64\mingw64\bin\libjpeg-8.dll"
File "C:\msys64\mingw64\bin\libtiff-6.dll"
File "C:\msys64\mingw64\bin\libpangoft2-1.0-0.dll"
File "C:\msys64\mingw64\bin\libatk-1.0-0.dll"
File "C:\msys64\mingw64\bin\libdatrie-1.dll"
File "C:\msys64\mingw64\bin\libgraphite2.dll"
File "C:\msys64\mingw64\bin\libexpat-1.dll"
File "C:\msys64\mingw64\bin\libbrotlidec.dll"
File "C:\msys64\mingw64\bin\libbz2-1.dll"
File "C:\msys64\mingw64\bin\libdeflate.dll"
File "C:\msys64\mingw64\bin\libjbig-0.dll"
File "C:\msys64\mingw64\bin\libLerc.dll"
File "C:\msys64\mingw64\bin\liblzma-5.dll"
File "C:\msys64\mingw64\bin\libwebp-7.dll"
File "C:\msys64\mingw64\bin\libzstd.dll"
File "C:\msys64\mingw64\bin\libbrotlicommon.dll"
File "C:\msys64\mingw64\bin\libsharpyuv-0.dll"
File /r "etc"
File /r "share"

# Copy gstreamer DLLs - Make sure you have -good and -bad plugins installed
SetOutPath "$INSTDIR\lib\gstreamer-1.0"
File /r "C:\msys64\mingw64\lib\gstreamer-1.0\*.dll"

# Default config
CreateDirectory "$APPDATA\rrclient"
SetOutPath "$APPDATA\rrclient"
File "..\..\..\config\rrclient.cfg.example"

IfFileExists "$APPDATA\rrclient\rrclient.cfg" 0 +1
  Goto config_done
Rename "$APPDATA\rrclient\rrclient.cfg.example" "$APPDATA\rrclient\rrclient.cfg"

config_done:

CreateShortCut "$DESKTOP\rrclient.lnk" "$INSTDIR\rrclient.exe"
SectionEnd
