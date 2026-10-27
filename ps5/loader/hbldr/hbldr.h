/*
 * Based on 'src/ps5/hbldr.h' (commit 7748df9) from websrv by John Törnblom.
 * Copyright (C) 2024 John Törnblom.
 * Licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later).
 * Project URL: https://github.com/ps5-payload-dev/websrv
 */


#pragma once

#include <unistd.h>


pid_t hbldr_launch(const char* cwd, const char* path, int stdio, char** argv,
		   char** envp);

// reason of the last hbldr_launch() failure
const char* hbldr_error(void);

// pid of another process with this thread name, -1 if none
pid_t hbldr_find_pid(const char* name);
