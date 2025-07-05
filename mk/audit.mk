audit: audit-printf audit-cppcheck audit-flawfinder

audit-printf:
	@echo "****************************"
	@echo "*** Audit Format Strings ***"
	@echo "****************************"
	which pscan >/dev/null && pscan -w src/*/*.c inc/*/*.h

audit-cppcheck:
	@echo "**********************"
	@echo "*** cppcheck audit ***"
	@echo "**********************"
	which cppcheck >/dev/null && cppcheck -j8 --std=c11 -q -v src/*/*.c inc/*/*.h --check-level=exhaustive --force -I./inc/ --enable=warning,performance,portability --inline-suppr --checkers-report=audit-logs/cppcheck.report.txt --language=c

audit-flawfinder:
	@echo "********************"
	@echo "* flawfinder audit *"
	@echo "********************"
	which flawfinder >/dev/null && flawfinder -Q -i src/*/*.c inc/*/*.h

audit-deps:
	apt install -y cppcheck pscan flawfinder
