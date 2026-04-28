#pragma once

typedef struct screenshot_viewer_entry_t {
    char name[64];
    char path[128];
} screenshot_viewer_entry_t;

int screenshot_viewer_load_entries(screenshot_viewer_entry_t *entries,
                                   int max_entries,
                                   char *status,
                                   unsigned status_size);
