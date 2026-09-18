rustyrig fwdsp build fix 1

Changes:
- Fix libfwdspmgr_srcs typo (libfwdsmgr -> libfwdspmgr).
- Normalize component object lists and use addprefix for exact object paths.
- Use $(dir $@) rather than shell dirname for build directories.
- Remove redundant libfwdspmgr dependency from bin/fwdsp.
- Link bin/fwdsp against librrprotocol, which owns cfg.fwdsp.c and therefore
  config_fwdsp_section_cb/config_pipeline_section_cb.
- Remove recipe-level '|| exit' wrappers; GNUmake already stops on a failed
  command and the project's shell uses -e.
