/*
 * Based on 'src/ps5/elfldr.h' (commit 7748df9) from websrv by John Törnblom.
 * Copyright (C) 2024 John Törnblom.
 * Licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later).
 * Project URL: https://github.com/ps5-payload-dev/websrv
 */


#pragma once

#include <unistd.h>


/**
 * Escape jail and raise privileges.
 **/
int elfldr_raise_privileges(pid_t pid);


/**
 * Execute an ELF inside a new process.
 **/
int elfldr_spawn(const char* cwd, int stdio, uint8_t* elf, char** argv,
                 char** envp);


/**
 * Execute an ELF inside the process with the given pid.
 **/
int elfldr_exec(pid_t pid, uint8_t* elf);


/**
 * Set environmental variables in the given process.
 **/
int elfldr_set_environ(pid_t pid, char** envp);


/**
 * Set the name of a process.
 **/
int elfldr_set_procname(pid_t pid, const char* name);


/**
 * Set stdout and stderr file descriptors of the given process.
 **/
int elfldr_set_stdio(pid_t pid, int stdio);


/**
 * Set the current working directory.
 **/
int elfldr_set_cwd(pid_t pid, const char* cwd);


/**
 * Set the heap size for libc.
 **/
int elfldr_set_heap_size(pid_t pid, ssize_t size);
