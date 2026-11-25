/* SPDX-License-Identifier: GPL-3.0-only
 * The PC twin on macOS (Makefile: build/twin-mac). Mach-O sections need a "segment,section" name, so each
 * section attribute becomes `used`: the twin then runs without its RAM garbling and its window checks. */
#define section(x) used
#include <unistd.h>
