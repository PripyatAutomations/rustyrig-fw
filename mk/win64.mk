${BUILD_DIR}/.stamp-win64dep: win64-deps

win64-deps:
	pacman -Syu
	pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-plugins-base nsis
	touch ${BUILD_DIR}.stamp-win64dep

win64-installer: ${INSTALLER} ${BUILD_DIR}/.stamp-win64dep

${INSTALLER}: ${bin}
	makensis //DOUTFILE=${INSTALLER} win32/rrclient.nsi

${OBJ_DIR}/icon.res: ../res/rustyrig.ico ../res/win64-icon.rc
	windres.exe ../res/win64-icon.rc -O coff -o $@
