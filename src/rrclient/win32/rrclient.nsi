# Name of the installer
!define /date DATE "%Y%m%d"
!define OUTFILE "rrclient.win64.${DATE}.exe"
Outfile "${OUTFILE}"

#!define GSTVER=
# Directory to install to
InstallDir $PROGRAMFILES\PripyatAutomations\rustyrig-client

# Default section
Section

# Output path for files
SetOutPath $INSTDIR

# Copy your built binaries (executables, DLLs, etc.)
File ".\build\rrclient.exe"
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

#File ".\ext\*.dll"
#File ".\ext\*.exe"
File /r "etc"
File /r "share"

# If we want to properly install gstreamer Run this
# msiexec /passive INSTALLDIR=$INSTDIR /i gstreamer-$GSTVER.msi

# Create shortcuts
CreateShortCut "$DESKTOP\rrclient.lnk" "$INSTDIR\rrclient.exe"

SectionEnd

