all clean distclean install world:
	${MAKE} -C .. $@

ls-src:
	find . -size +1k | xargs wc -c | head -n -1 | sort -n
