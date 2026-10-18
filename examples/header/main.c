// SPDX-License-Identifier: Apache-2.0

#include "header/config.h"
#if !HAVE_INT128 || !HAVE_STDINT || !HAVE_ASSEMBLER || !HAVE_LINK
#error This example requires int128, stdint, assembly, and executable linking
#endif
#if HAVE_IMPOSSIBLE
#error False capabilities must be defined to zero
#endif
#include <stdint.h>
int main(void) { __int128 x = UINT32_MAX; return x != 4294967295ULL; }
