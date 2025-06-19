win32-deps:
	pacman -Syu
	pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3 mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-plugins-base
