// SPDX-License-Identifier: Apache-2.0

#include "custom/config.h"
#include <string.h>
_Static_assert(sizeof(long) == SIZEOF_LONG, "measured target ABI");
#if !TOOL_RUNFILES_OK || !HAVE_EXPECTED_LONG || !A_ORDERED || !Z_FIRST
#error Missing inherited answers
#endif
int main(void) { if (strcmp(COMPILER_FAMILY, "clang") == 0) return 0;
    return strcmp(COMPILER_FAMILY, "gcc") != 0; }
