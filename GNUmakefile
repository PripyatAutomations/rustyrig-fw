all: world

world test run install gdb:
	@${MAKE} -C src $@

help:
	@echo "build ctrl: world|clean|test"
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
