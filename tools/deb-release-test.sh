#!/bin/bash
# Maintainer script, used for testing during development

# It will do the following:
# 	Move .debs from parent directory (after make deb) in ../releases/
# 	Install them (replacing old versions)

MYDEBS="{lib{fwdspmgr,rrprotocol,rustyaxe},rustyrig-{callsign-lookup,client,fwdsp,server}}"
rm -f *.buildinfo *.changes
mv ../*.deb ../rustyrig*.buildinfo ../rustyrig*.changes ../releases/

# This has sudo here to avoid having to run make under sudo
$SUDO dpkg -i $(ls ../releases/*.deb | egrep -v '(dbgsym|-tui)')
