# Optional source fetching uses the selected profile rather than --recursive.
.PHONY: update git-pull
update git-pull:
	git pull --ff-only
	PROFILE='$(PROFILE)' USE_MONGOOSE='$(USE_MONGOOSE)' ./tools/update-optional-sources.sh

recent-diff:
	git diff '@{6 hours ago}'

ifeq ($(USE_MONGOOSE),true)
ext/libmongoose/mongoose.c:
	./tools/update-optional-sources.sh
endif
