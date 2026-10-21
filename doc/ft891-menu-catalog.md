# FT-891 MENU inventory

The FT-891 Advanced Manual menu table (pages 69–73) lists 159 entries
organized in 18 sections. `rrserver/backend.yaesu.c` mirrors the printed
section order and numeric identifiers. `rrserver/objects.c` publishes
section nodes below a generic `MENU` node in Resource Explorer.

The catalog is model-specific and requires CAT `ID0650;`. The FT-991A
retains its own separate, preliminary catalog.

This stage supplies **menu labels and numbering only**. The rows are
intentionally read-only, and there is no EX read request or live value in
inventory output. On-demand reads, editable type/range validation and
failure/status reporting remain necessary before the values displayed
in RustyRig can be compared with the settings displayed on the radio.

Firmware revisions can change available entries. Compare each section
and report discrepancies by menu ID. The catalog references the FT-891
Advanced Manual (1806-F), pages 69–73.
