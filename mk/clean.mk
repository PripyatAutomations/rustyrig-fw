##################
# Source Cleanup #
##################
clean:
	@echo "[clean]"
	${RM} ${bins} ${rrserver_objs} ${rrclient_objs} ${fwdsp_objs} ${extra_clean}

distclean: clean
	@echo "[distclean]"
	${RM} -rf ${BUILD_DIR}
	${RM} -r build/ config/archive run/ db/
	${RM} -f *.log
