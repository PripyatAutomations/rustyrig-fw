You may notice there's not much TUI code here, that's because it belongs to
librustyaxe. The TUI interface is designed to be reusable, whereas the GTK3
interface is designed to have components that could later be reused such as
gtk-freqentry widget.
