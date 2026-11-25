set pagination off
set breakpoint pending on
set print thread-events off

break gtk_icon_theme_lookup_icon
commands
  silent
  printf "Icon lookup: %s\n", (char *)$rsi
  continue
end

break gdk_pixbuf_new_from_file_at_scale
commands
  silent
  printf "Image load: %s\n", (char *)$rdi
  continue
end
