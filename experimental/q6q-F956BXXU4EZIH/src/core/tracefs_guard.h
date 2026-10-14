#ifndef TRACEFS_GUARD_H
#define TRACEFS_GUARD_H

#include <stdio.h>
#include <string.h>

/* Never silently discard somebody else's trace, or join active tracing. */
static int tracefs_diagnostic_can_sample(unsigned int tracing_on,
                                        unsigned int event_enabled,
                                        const char *header) {
  if (tracing_on != 0 || event_enabled != 0 || !header) return 0;
  const char *counts = strstr(header, "# entries-in-buffer/entries-written:");
  unsigned long long entries = 0, written = 0;
  if (!counts || sscanf(counts, "# entries-in-buffer/entries-written: %llu/%llu",
                        &entries, &written) != 2) return 0;
  return entries == 0;
}

#endif
