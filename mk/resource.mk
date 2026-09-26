# Runtime configuration files are tracked in config/ and are usable as-is.
CONFIGS=config/rrserver.cfg config/rrclient.cfg

.PHONY: config-files
config-files:
	@test -f config/rrserver.cfg -a -f config/rrclient.cfg

convert-icon:
	convert res/rustyrig.png -define icon:auto-resize=16,32,48,64,128,256 res/rustyrig.ico
