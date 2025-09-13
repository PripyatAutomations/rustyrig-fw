##################
# Source Cleanup #
##################
clean: ${extra_clean_targets}
	@echo "[clean]"
	${RM} ${bins} ${rrserver_objs} ${rrclient_objs} ${fwdsp_objs} ${extra_clean}
ifneq (y${extra_clean_targets},y)
	${MAKE} ${extra_clean_targets}
endif

distclean: clean
	@echo "[distclean]"
	${RM} -rf ${OBJ_DIR}
	${RM} -r audit-logs/ build/ config/archive db/ run/
	${RM} -f *.log
