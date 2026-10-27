#pragma once

#ifdef __PS4__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SIGPIPE handling and timezone, call first.
void ps4_compat_init(void);

// TZ from the console settings (updated only when the offset changes).
void ps4_update_timezone(void);

// UTC offset in minutes.
int ps4_get_utc_offset(void);

// Clocks report (debug).
void ps4_clock_report(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif
