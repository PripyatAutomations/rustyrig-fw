all clean distclean install world:
	${MAKE} -C .. $@

ls-src:
	find . -type f -size +1k -name \*.c | xargs wc -c | head -n -1 | sort -n
