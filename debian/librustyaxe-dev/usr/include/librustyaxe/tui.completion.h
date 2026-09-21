#ifndef TUI_COMPLETION_H
#define TUI_COMPLETION_H

#include <stdbool.h>

#define TUI_MAX_COMPLETIONS_SHOWN 32

typedef char **(*tui_completion_provider_t)(const char *line,
                                            const char *word);

bool tui_register_completion_provider(tui_completion_provider_t fn);
bool tui_unregister_completion_provider(tui_completion_provider_t fn);

char **completion_collect(const char *line, const char *word);
void completion_free(char **matches);

#endif
