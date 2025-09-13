config/http.users:
	@echo "*** You do not have a config/http.users so i've copied the default file from doc/"
	cp -i doc/http.users.example config/http.users

convert-icon:
	convert res/rustyrig.png -define icon:auto-resize=16,32,48,64,128,256 res/rustyrig.ico
