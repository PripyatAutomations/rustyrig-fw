; Experimental native Win64 installer. Build a distribution tree with
; tools/win64-stage.sh, then pass its absolute Windows path as STAGE_DIR.
; No versioned DLL names or MSYS2 installation paths belong in this script.
Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!ifndef STAGE_DIR
  !error "Pass /DSTAGE_DIR=<absolute Windows path to staged runtime>"
!endif
!ifndef OUTFILE
  !define OUTFILE "rustyrig-win64-unsigned.exe"
!endif

Name "RustyRig Client (experimental Win64)"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\RustyRig"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "RustyRig Client" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\RustyRig"
  CreateShortCut "$SMPROGRAMS\RustyRig\RustyRig Client.lnk" "$INSTDIR\RustyRig.cmd" "" "$INSTDIR\bin\rrclient.exe" 0 SW_SHOWNORMAL "" "Start RustyRig Client"
  CreateShortCut "$SMPROGRAMS\RustyRig\Uninstall.lnk" "$INSTDIR\Uninstall.exe"

  ; First-run configuration only: never overwrite an existing profile.
  CreateDirectory "$APPDATA\rustyrigs"
  IfFileExists "$APPDATA\rustyrigs\rrclient.cfg" config_done
  CopyFiles /SILENT "$INSTDIR\config\rrclient.cfg.example" "$APPDATA\rustyrigs\rrclient.cfg"
config_done:

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RustyRigClient" "DisplayName" "RustyRig Client (experimental Win64)"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RustyRigClient" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RustyRigClient" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RustyRigClient" "NoModify" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\RustyRig\RustyRig Client.lnk"
  Delete "$SMPROGRAMS\RustyRig\Uninstall.lnk"
  RMDir "$SMPROGRAMS\RustyRig"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\RustyRigClient"
  ; All installed files live in this dedicated directory. User settings
  ; in %APPDATA% are deliberately left intact.
  RMDir /r "$INSTDIR"
SectionEnd
