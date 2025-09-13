###################
# install targets #
###################
windows-install:
	@echo "Windows builds do not need to install. A NSIS installer can be built using win64-installer target"

posix-install:
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	install -Dm755 ${bin} ${INSTALL_DIR}/bin/$(shell basename "${bin}")
	install -Dm644 rustyrig.png /usr/share/icons/hicolor/48x48/apps/rustyrig.png
	# Transform the .desktop shortcut to use $PREFIX
	sed rrclient.desktop -e "s%/usr/local/%${PREFIX}%g" > ..tmp.rrclient.desktop
	install -Dm644 ..tmp.rrclient.desktop /usr/share/applications/rrclient.desktop
	@${RM} -f ..tmp.rrclient.desktop

install: ${real_install}

install:
	mkdir -p ${INSTALL_DIR}/bin ${INSTALL_DIR}/etc ${INSTALL_DIR}/share
	cp -av ${bins} ${INSTALL_DIR}/bin
#	cp -av archive-config.sh *-rigctld.sh killall.sh rrclient.sh test-run.sh ${INSTALL_DIR}/bin
#	cp -aiv config/${PROFILE}.*.json config/client.config.json ${INSTALL_DIR}/etc
