audit: audit-printf audit-cppcheck audit-flawfinder

audit-printf:
	@echo "****************************"
	@echo "*** Audit Format Strings ***"
	@echo "****************************"
	which pscan && pscan -w src/*/*.c inc/*/*.h

audit-cppcheck:
	@echo "**********************"
	@echo "*** cppcheck audit ***"
	@echo "**********************"
	which cppcheck && cppcheck -q -v src/ inc/

audit-flawfinder:
	@echo "********************"
	@echo "* flawfinder audit *"
	@echo "********************"
	which flawfinder && flawfinder src/ inc/

audit-deps:
	apt install -y cppcheck pscan flawfinder
