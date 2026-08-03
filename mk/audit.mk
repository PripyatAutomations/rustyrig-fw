audit_files = $(wildcard librustyaxe/*.[ch] librrprotocol/*.[ch] rrclient/*.[ch] rrserver/*.[ch] fwdsp/*.[ch])
audit: audit-printf audit-cppcheck audit-flawfinder

audit-printf:
	@echo "****************************"
	@echo "*** Audit Format Strings ***"
	@echo "****************************"
	which pscan >/dev/null && pscan -w ${audit_files}

audit-cppcheck:
	@echo "**********************"
	@echo "*** cppcheck audit ***"
	@echo "**********************"
	which cppcheck >/dev/null && cppcheck -j8 --std=c11 -q -v --check-level=exhaustive --force -I./inc/ --enable=warning,performance,portability --inline-suppr --checkers-report=audit-logs/cppcheck.report.txt --language=c ${audit_files}

audit-flawfinder:
	@echo "********************"
	@echo "* flawfinder audit *"
	@echo "********************"
	which flawfinder >/dev/null && flawfinder -m 3 -Q -i ${audit_files}

audit-deps:
	apt install -y cppcheck pscan flawfinder
