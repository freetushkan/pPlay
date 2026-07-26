//
// PS4 platform support: monotonic clock, stdout/stderr capture, crash log.
//

#ifndef PPLAY_PS4_PLATFORM_H
#define PPLAY_PS4_PLATFORM_H

#ifdef __PS4__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Microseconds since the clock was first used (process startup in practice).
/// Paced by the wall clock, never decreasing, unaffected by system time
/// changes. See src/ps4/ps4_time.c for why the libc clock cannot be used.
uint64_t pplay_ps4_monotonic_us(void);

/// Microseconds since the unix epoch, straight from the kernel.
uint64_t pplay_ps4_realtime_us(void);

/// Measures every clock source the kernel exposes against the wall clock and
/// logs the result. Takes about 250ms, meant to be called once from main().
void pplay_ps4_time_probe(void);

/// Points fd 1 and 2 at /data/pplay/stdio.log. Call before anything that may
/// print, ideally the first statement of main().
void pplay_ps4_debug_init(void);

/// Flushes the captured stdout/stderr to disk.
void pplay_ps4_debug_flush(void);

/// Installs the fatal signal handlers writing to /data/pplay/crash.log.
/// Idempotent; also runs automatically as a library constructor.
void pplay_ps4_crashlog_init(void);

#ifdef __cplusplus
}
#endif

#endif // __PS4__

#endif //PPLAY_PS4_PLATFORM_H
