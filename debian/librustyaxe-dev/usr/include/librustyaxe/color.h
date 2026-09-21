/*
 * librustyaxe/color.h: shared color parsing/conversion helpers.
 *
 *    This is part of rustyrig-fw.
 * https://github.com/pripyatautomations/rustyrig-fw
 *
 * Do not pay money for this, except donations to the project, if you wish to.
 * The software is not for sale. It is freely available, always.
 *
 * Licensed under MIT license, if built without mongoose or GPL if built with.
 */
#ifndef __RUSTYAXE_COLOR_H
#define __RUSTYAXE_COLOR_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Parse a color tag key as produced by the {tag} colorizer syntax:
 *   "#rgb"                  short form hex
 *   "#rrggbb"               full hex
 *   "#rrggbb:fallback"      hex with a named fallback tag (16-color terms)
 *   "bg-<any of the above>" background variant (e.g. "bg-#ff0000:red")
 *
 * Outputs:
 *   hex   - canonical "#rrggbb" form (zero-filled short form), buffer >= 8
 *   fb    - fallback tag name ("" if none), buffer >= 32
 *   is_bg - true if the key requested a background color
 *
 * Returns true if the key is a valid hex color spec. An unknown fallback
 * name is passed through verbatim; callers resolve it against their tables.
 */
extern bool color_tag_parse(const char *key, char *hex, size_t hexlen,
                            char *fb, size_t fblen, bool *is_bg);

/* Parse "#rgb" or "#rrggbb" into components (0-255). Returns false on error. */
extern bool color_parse_hex(const char *hex, int *r, int *g, int *b);

/*
 * Nearest xterm-256 color index for an RGB triple using the standard
 * 6x6x6 color cube + 24-step grayscale ramp distance model.
 */
extern int color_rgb_to_ansi256(int r, int g, int b);

/*
 * Nearest of the 16 base ANSI colors as a tag name ("black", "bright-red",
 * ...), using simple weighted-RGB distance. Good fallback for terminals
 * with neither truecolor nor 256-color support.
 */
extern const char *color_nearest_named(int r, int g, int b);

#endif // __RUSTYAXE_COLOR_H
