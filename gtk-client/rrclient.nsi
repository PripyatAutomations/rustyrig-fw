# Name of the installer
Outfile "MyAppInstaller.exe"

# Directory to install to
InstallDir $PROGRAMFILES\MyApp

# Default section
Section

# Output path for files
SetOutPath $INSTDIR

# Copy your built binaries (executables, DLLs, etc.)
File "path\to\your\app.exe"
File "path\to\your\gstreamer\*.dll"
File "path\to\your\gtk\*.dll"

# Create shortcuts
CreateShortCut "$DESKTOP\MyApp.lnk" "$INSTDIR\app.exe"

# Run this
# msiexec /passive INSTALLDIR=C:\Desired\Folder /i gstreamer-1.0-x86-1.26.2.msi
SectionEnd
