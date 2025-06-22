
update:
	git pull
	git submodule init
	git submodule update --remote --recursive

recent-diff:
	git diff '@{6 hours ago}'

ext/libmongoose/mongoose.c:
	@echo "You forgot to git submodule init; git submodule update. Doing it for you!"
	git submodule init
	git submodule update
