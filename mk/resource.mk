EXAMPLES=config/http.users config/rrgtk.cfg config/rrcli.cfg config/rrserver.cfg

${EXAMPLES}:
	@echo "*** You do not have a $@ i've copied the default file from $@.example"
	cp -i $@.example $@

convert-icon:
	convert res/rustyrig.png -define icon:auto-resize=16,32,48,64,128,256 res/rustyrig.ico
