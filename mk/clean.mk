
##################
# Source Cleanup #
##################
clean:
	@echo "[clean]"
	${RM} ${bins} ${real_fw_objs} ${real_objs} ${extra_clean} ${real_comm_objs}
	${RM} ${bin} ${real_objs} ${extra_clean}

distclean: clean
	@echo "[distclean]"
	${RM} -rf ${BUILD_DIR}
	${RM} -r build config/archive run/
	${RM} -f *.log
