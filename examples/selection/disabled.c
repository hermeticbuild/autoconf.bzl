// SPDX-License-Identifier: Apache-2.0

#if SELECTED_VALUE != 19
#error Wrong fallback flag
#endif
int disabled_symbol(void) { return SELECTED_VALUE; }
int selected_value(void) { return disabled_symbol(); }
