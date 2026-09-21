# Static-analysis targets.  Each invocation gets its own directory so reports
# are never overwritten and a failed analyzer cannot hide behind tee(1).
SCANBUILD ?= scan-build-21
CLANG_TIDY_RUNNER ?= run-clang-tidy
AUDIT_TIDY_JOBS ?= 4
ifndef AUDIT_DIR
AUDIT_DIR := audit-logs/$(shell date +%Y%m%d.%H%M%S)
endif
audit_files = $(wildcard librustyaxe/*.[ch] librrprotocol/*.[ch] rrclient/*.[ch] rrserver/*.[ch] fwdsp/*.[ch])
# Use only translation units that were actually compiled.  Passing directory
# globs directly to clang-tidy makes it invent command lines for generated
# headers and files from the wrong build profile.  The compile database also
# lets the audit honor the same feature defines and include paths as the build.
# Keep checks focused on correctness and portability. clang-tidy's generic
# security.insecureAPI checks flag every bounded snprintf/memset in this C
# codebase and drown out actual defects; flawfinder/cppcheck cover that class
# separately in the same audit.
AUDIT_TIDY_CHECKS ?= clang-analyzer-core-*,clang-analyzer-unix-*,clang-analyzer-deadcode.*,bugprone-*,-bugprone-narrowing-conversions,-bugprone-casting-through-void,-bugprone-easily-swappable-parameters,-bugprone-multi-level-implicit-pointer-conversion,-bugprone-signal-handler,-performance-no-int-to-ptr,-clang-analyzer-security.insecureAPI.*,performance-*,portability-*

.PHONY: audit audit-init audit-scanbuild audit-printf audit-cppcheck audit-flawfinder audit-deps

audit: audit-init
	@status=0; \
	$(MAKE) --no-print-directory AUDIT_DIR=$(AUDIT_DIR) audit-scanbuild || status=$$?; \
	$(MAKE) --no-print-directory AUDIT_DIR=$(AUDIT_DIR) audit-printf || status=$$?; \
	$(MAKE) --no-print-directory AUDIT_DIR=$(AUDIT_DIR) audit-cppcheck || status=$$?; \
	$(MAKE) --no-print-directory AUDIT_DIR=$(AUDIT_DIR) audit-flawfinder || status=$$?; \
	$(MAKE) --no-print-directory AUDIT_DIR=$(AUDIT_DIR) audit-clang-tidy || status=$$?; \
	echo "Audit logs: $(AUDIT_DIR)"; \
	exit $$status

audit-init:
	@mkdir -p "$(AUDIT_DIR)"
	@ln -sfn "$(notdir $(AUDIT_DIR))" audit-logs/latest
	@printf 'RustyRig audit run %s\n' "$(AUDIT_DIR)" > "$(AUDIT_DIR)/README"
	@printf 'Full analyzer output is stored in this directory.\n' >> "$(AUDIT_DIR)/README"

# audit-scanbuild MUST run first because it starts with a clean build.
audit-scanbuild: audit-init
	@echo "*** scan-build audit (full log: $(AUDIT_DIR)/scanbuild.log) ***"
	@command -v "$(SCANBUILD)" >/dev/null 2>&1 || { echo "$(SCANBUILD) not installed; audit skipped" > "$(AUDIT_DIR)/scanbuild.log"; echo "$(SCANBUILD) not installed (skipped)"; exit 0; }
	@"$(SCANBUILD)" $(MAKE) -j4 world > "$(AUDIT_DIR)/scanbuild.log" 2>&1; rc=$$?; \
		echo "scan-build exit status: $$rc" >> "$(AUDIT_DIR)/scanbuild.log"; \
		tail -n 12 "$(AUDIT_DIR)/scanbuild.log"; exit $$rc

audit-printf: audit-init
	@echo "*** format-string audit (full log: $(AUDIT_DIR)/printf.log) ***"
	@if command -v pscan >/dev/null 2>&1; then \
		pscan -w $(audit_files) > "$(AUDIT_DIR)/printf.log" 2>&1; rc=$$?; \
		tail -n 12 "$(AUDIT_DIR)/printf.log"; exit $$rc; \
	else \
		echo 'pscan not installed; audit skipped' > "$(AUDIT_DIR)/printf.log"; echo 'pscan not installed (skipped)'; \
	fi

audit-cppcheck: audit-init
	@echo "*** cppcheck audit (full log: $(AUDIT_DIR)/cppcheck.log) ***"
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck -j8 --std=c11 -q -v --check-level=exhaustive --force \
			-I./inc/ --enable=warning,performance,portability --inline-suppr \
			--checkers-report="$(AUDIT_DIR)/cppcheck.report.txt" --language=c \
			$(audit_files) > "$(AUDIT_DIR)/cppcheck.log" 2>&1; rc=$$?; \
		tail -n 20 "$(AUDIT_DIR)/cppcheck.log"; exit $$rc; \
	else \
		echo 'cppcheck not installed; audit skipped' > "$(AUDIT_DIR)/cppcheck.log"; echo 'cppcheck not installed (skipped)'; \
	fi

audit-flawfinder: audit-init
	@echo "*** flawfinder audit (full log: $(AUDIT_DIR)/flawfinder.log) ***"
	@if command -v flawfinder >/dev/null 2>&1; then \
		flawfinder -m 2 -Q $(audit_files) > "$(AUDIT_DIR)/flawfinder.log" 2>&1; rc=$$?; \
		tail -n 20 "$(AUDIT_DIR)/flawfinder.log"; exit $$rc; \
	else \
		echo 'flawfinder not installed; audit skipped' > "$(AUDIT_DIR)/flawfinder.log"; echo 'flawfinder not installed (skipped)'; \
	fi

compile_commands.json:
	@command -v bear >/dev/null 2>&1 || { echo 'bear is required to create compile_commands.json' >&2; exit 1; }
	@rm -f compile_commands.json
	@bear -- make world || { rc=$$?; rm -f compile_commands.json; exit $$rc; }
	@test -s compile_commands.json || { rm -f compile_commands.json; echo 'bear produced an empty compile database' >&2; exit 1; }

clang-tidy: compile_commands.json audit-init
	@echo "*** clang-tidy audit (full log: $(AUDIT_DIR)/clang-tidy.log) ***"
	@command -v clang-tidy >/dev/null 2>&1 || { echo 'clang-tidy not installed; audit skipped' > "$(AUDIT_DIR)/clang-tidy.log"; echo 'clang-tidy not installed (skipped)'; exit 0; }
	@test -s compile_commands.json && python3 -c 'import json, sys; x=json.load(open("compile_commands.json")); sys.exit(0 if x else 1)' || { echo 'compile_commands.json is missing or empty; run make compile_commands.json after configuring the build' > "$(AUDIT_DIR)/clang-tidy.log"; echo 'compile_commands.json missing or empty (audit failed)'; exit 2; }
	@raw="$(AUDIT_DIR)/clang-tidy.raw.log"; \
	if command -v "$(CLANG_TIDY_RUNNER)" >/dev/null 2>&1; then \
		"$(CLANG_TIDY_RUNNER)" -p . -j "$(AUDIT_TIDY_JOBS)" -quiet -use-color false \
			-checks '$(AUDIT_TIDY_CHECKS)' \
			-header-filter '^$(CURDIR)/(librustyaxe|librrprotocol|rrclient|rrserver|libfwdspmgr|fwdsp)/' \
			'librustyaxe/.*|librrprotocol/.*|rrclient/.*|rrserver/.*|libfwdspmgr/.*|fwdsp/.*' > "$$raw" 2>&1; rc=$$?; \
	else \
		files=$$(find librustyaxe librrprotocol rrclient rrserver libfwdspmgr fwdsp -maxdepth 1 -type f -name '*.c' -print); \
		clang-tidy -p . --quiet --use-color=false -checks='$(AUDIT_TIDY_CHECKS)' \
			-header-filter='^$(CURDIR)/(librustyaxe|librrprotocol|rrclient|rrserver|libfwdspmgr|fwdsp)/' \
			$$files > "$$raw" 2>&1; rc=$$?; \
	fi; \
	sed -E '/^[[:space:]]*[0-9]+ warnings generated\.$$/d; /^[[:space:]]*\[[[:space:]]*[0-9]+\/[0-9]+\]\[[^]]+\] clang-tidy-/d; /^[[]([0-9]+)\/[0-9]+[]] Processing file /d' "$$raw" > "$(AUDIT_DIR)/clang-tidy.log"; \
	# Keep the unfiltered runner output for troubleshooting; the main log is
	# cleaned of progress and compiler-summary noise for routine review.
	tail -n 20 "$(AUDIT_DIR)/clang-tidy.log"; exit $$rc

audit-clang-tidy: clang-tidy

audit-deps:
	apt install -y cppcheck pscan flawfinder
