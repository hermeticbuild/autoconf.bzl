// SPDX-License-Identifier: Apache-2.0

#if SELECTED_VALUE != 73
#error Wrong feature-dependent flag
#endif
int enabled_symbol(void) { return SELECTED_VALUE; }
int selected_value(void) { return enabled_symbol(); }
