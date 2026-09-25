##################
# Source Cleanup #
##################
extra_clean += core.* fwdsp.core rrclient.core rrserver.core
extra_clean += compile_commands.json

clean: ${extra_clean_targets}
	@echo "[clean]"
	${RM} ${bins} ${rrserver_objs} ${rrgtk_objs} ${extra_clean} *.log
	${RM} -fr audits-logs/*

ifneq (y${extra_clean_targets},y)
	${MAKE} ${extra_clean_targets}
endif

distclean: clean
	@echo "[distclean]"
	${RM} -rf ${OBJ_DIR}
	# Audit reports are historical artifacts and must survive clean/distclean.
	${RM} -r build/ config/archive db/ run/ ${extra_distclean}
	${RM} -f *.log
