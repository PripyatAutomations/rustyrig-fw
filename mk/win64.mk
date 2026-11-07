# Experimental MSYS2 UCRT64 packaging. The port must supply native PE
# bin/rrclient.exe and bin/rrclient-gtk.dll first; staging deliberately
# does not disguise compilation failures or depend on Unix .so targets.
WIN64_STAGE ?= build/win64-stage
WIN64_INSTALLER ?= build/rustyrig-win64-unsigned.exe

.PHONY: win64-deps win64-stage win64-installer win64-preflight
win64-preflight:
	@test "$$MSYSTEM" = UCRT64 || { echo "Use the MSYS2 UCRT64 shell (not MSYS/MINGW64)." >&2; exit 1; }
	@command -v makensis >/dev/null || { echo "Install NSIS (pacman -S nsis)." >&2; exit 1; }
	@command -v cygpath >/dev/null || { echo "Missing MSYS2 cygpath." >&2; exit 1; }

# Package installation is explicit, never a surprise prerequisite of 'make'.
win64-deps:
	@bash rrclient/win32/install-deps.sh

win64-stage:
	@bash tools/win64-stage.sh "$(WIN64_STAGE)"

win64-installer: win64-preflight win64-stage
	@mkdir -p "$(dir $(WIN64_INSTALLER))"
	@makensis -DSTAGE_DIR="$$(cygpath -aw "$(WIN64_STAGE)")" -DOUTFILE="$$(cygpath -aw "$(WIN64_INSTALLER)")" rrclient/win32/rrclient.nsi
	@echo "Experimental installer: $(WIN64_INSTALLER)"
