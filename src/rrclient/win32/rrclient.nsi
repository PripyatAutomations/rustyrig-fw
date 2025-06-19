# Name of the installer
!define /date DATE "%Y%m%d"
!define OUTFILE "rrclient.win64.$DATE.exe"
Outfile $OUTFILE

#!define GSTVER=
# Directory to install to
InstallDir $PROGRAMFILES\PripyatAutomations\rustyrig-client

# Default section
Section

# Output path for files
SetOutPath $INSTDIR

# Copy your built binaries (executables, DLLs, etc.)
File ".\build\rrclient.exe"
#File ".\ext\*.dll"
#File ".\ext\*.exe"


# If we want to properly install gstreamer Run this
# msiexec /passive INSTALLDIR=$INSTDIR /i gstreamer-$GSTVER.msi

# Create shortcuts
CreateShortCut "$DESKTOP\rrclient.lnk" "$INSTDIR\rrclient.exe"

SectionEnd

