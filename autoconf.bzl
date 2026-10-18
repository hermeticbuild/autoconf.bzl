# SPDX-License-Identifier: Apache-2.0

"""Public entry point for configured-toolchain probes and result consumers.

Most BUILD files need only autoconf, probe constructors, and autoconf_header.
The remaining exports support custom Starlark consumers.
Probe answers are available at execution time, never during ordinary analysis.
"""

load(
    "//private:autoconf.bzl",
    _AutoconfInfo = "AutoconfInfo",
    _assemble_test = "assemble_test",
    _autoconf = "autoconf",
    _autoconf_header = "autoconf_header",
    _check_toolchain = "check_toolchain",
    _command_test = "command_test",
    _compile_test = "compile_test",
    _link_test = "link_test",
    _read_results = "read_results",
    _toolchain_context = "toolchain_context",
)
load(
    "//private:payload.bzl",
    _read_payload = "read_payload",
)

# BUILD-file API. Constructors describe tests; autoconf registers their actions.

# Run named tests with the configured C/C++ toolchain; optionally extend a stage.
autoconf = _autoconf

# Generate an ordinary .h file from cumulative answers; false booleans become 0.
autoconf_header = _autoconf_header

# Describe a boolean inline-C compile check without linking or running code.
compile_test = _compile_test

# Describe a boolean preprocessed-assembly (.S) compile check.
assemble_test = _assemble_test

# Describe a boolean C program link check; the target executable is never run.
link_test = _link_test

# Describe a declared execution-tool check returning bool, int, or a safe string.
command_test = _command_test

# Custom-consumer API. These wire artifacts and toolchain recipes into actions.

# Provider holding the results tree and toolchain snapshot, not measured scalars.
AutoconfInfo = _AutoconfInfo

# Analysis helper returning the configured toolchain's id, files, and recipe spec.
toolchain_context = _toolchain_context

# Fail if an AutoconfInfo and toolchain_context differ in identifier or recipe.
check_toolchain = _check_toolchain

# Decode scalar answer filenames from an ExpandedDirectory in a map callback.
read_results = _read_results

# Optional structured-JSON channel for consumers needing more than scalar answers.
# Takes an ExpandedDirectory from a separate payload tree, not a scalar result tree.
read_payload = _read_payload
