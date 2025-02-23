# Pass through most things to the src subdir, but catch git operations here
all: world

world debug run install:
	@${MAKE} -C src $@

help:
	@echo "run: run|debug"
	@echo "build ctrl: world|clean"
	@echo "source mgmt: pull|push|commit"

push: commit
	git push

pull:
	git pull

commit:
	git commit


clean distclean:
	@echo "=> src"
	@${MAKE} -C src $@
