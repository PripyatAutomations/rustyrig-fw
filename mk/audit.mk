SCANBUILD := scan-build-21
audit_files = $(wildcard librustyaxe/*.[ch] librrprotocol/*.[ch] rrclient/*.[ch] rrserver/*.[ch] fwdsp/*.[ch])

# audit-scanbuild *MUST* be first as it does a make clean!
audit: audit-log/scanbuild.log audit-printf audit-cppcheck audit-flawfinder audit-clang-tidy

audit-printf: audit-log/scanbuild.log
	@echo "****************************"
	@echo "*** Audit Format Strings ***"
	@echo "****************************"
	@which pscan >/dev/null && pscan -w ${audit_files} 2>&1 | tee audit-logs/audit-printf.log

audit-cppcheck: audit-log/scanbuild.log
	@echo "**********************"
	@echo "*** cppcheck audit ***"
	@echo "**********************"
	@which cppcheck >/dev/null && cppcheck -j8 --std=c11 -q -v --check-level=exhaustive --force \
		-I./inc/ --enable=warning,performance,portability --inline-suppr --std=c11 \
		--checkers-report=audit-logs/cppcheck.report.txt --language=c ${audit_files} 2>&1 | tee audit-logs/cppcheck.log

audit-flawfinder: audit-log/scanbuild.log
	@echo "********************"
	@echo "* flawfinder audit *"
	@echo "********************"
	@which flawfinder >/dev/null && flawfinder -m 2 -Q ${audit_files} 2>&1 | tee audit-logs/flawfinder.log

audit-deps:
	apt install -y cppcheck pscan flawfinder

compile_commands.json: distclean
	bear -- make world

clang-tidy: compile_commands.json audit-log/scanbuild.log
	@echo "************************"
	@echo "*** clang-tidy audit ***"
	@echo "************************"
	clang-tidy -p . -checks='bugprone-*,performance-*,portability-*,readability-*' rrclient/*.c rrserver/*.c librustyaxe/*.c librrprotocol/*.c 2>&1 | tee audit-logs/clang-tidy.log
	# Should ignore readability-use-concise-preprocessor-directives
	# and such

audit-clang-tidy: clang-tidy

audit-log/scanbuild.log: audit-scanbuild

audit-scanbuild:
	@echo "************************"
	@echo "*** scan-build audit ***"
	@echo "************************"
	${MAKE} clean
	${SCANBUILD} ${MAKE} -j4 world 2>&1 | tee audit-logs/scanbuild.log
