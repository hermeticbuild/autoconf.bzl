# autoconf.bzl

Autoconf-style checks for Bazel's configured C/C++ toolchain. Compile, assembly,
link, and custom-tool checks run as build actions and produce typed results or a
`config.h` file. The helpers are written in C++; there is no Python runtime.

## Generate a config header

Load the rules from `@autoconf.bzl//:autoconf.bzl`:

```starlark
load("@autoconf.bzl//:autoconf.bzl", "autoconf", "autoconf_header", "compile_test")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

autoconf(
    name = "features",
    tests = {
        "HAVE_INT128": compile_test("__int128 value;"),
        "HAVE_STDINT_H": compile_test("#include <stdint.h>\n"),
    },
)

autoconf_header(name = "config", probes = ":features")

cc_library(
    name = "example",
    srcs = ["example.c"],
    hdrs = [":config"],
)
```

For a target in `mypkg/`, include `"mypkg/config.h"`. Boolean answers are always
defined, with value `1` or `0`, so use `#if HAVE_INT128` rather than `#ifdef`.
Integers become decimal macro values; strings become quoted literals.

| Constructor | What it checks |
| --- | --- |
| `compile_test(source, flags=[])` | Compile inline C to an object |
| `assemble_test(source, flags=[])` | Compile preprocessed assembly (`.S`) |
| `link_test(source, flags=[])` | Compile and link a C program |
| `command_test(tool, arguments=[], result="bool")` | Invoke a declared executable; return a bool, int, or string |

Compile and link checks never run the target program. A rejected feature normally
means exit status 1. Missing tools, unexpected statuses, signals, and timeouts
fail the build. `failure_codes` can change the accepted rejection statuses.
Integer and string commands must exit successfully; strings are limited to
120 characters from `A-Za-z0-9_.+-`, excluding `.` and `..`.

The checks use toolchain features and flags. A consuming target's includes,
defines, `copts`, and library dependencies are not automatically inherited:
pass the needed probe `flags` and declare any extra input files through `data`.

## Custom checks and stages

`autoconf.tools` maps executable labels to aliases used by `command_test`.
Tools run on the execution platform with their runfiles. Arguments are passed
directly, without a shell, and subprocesses do not inherit the host environment.
Use `environment` for explicit overrides.

Source text and arguments can contain `{cc}`, `{ld}`, `{ar}`, `{work}`, and
`{result:NAME}`. A custom argument equal to `{compile_flags}` or `{link_flags}`
expands to multiple arguments. See [the custom-probe example](examples/custom)
for a target-ABI measurement tool and its declared runfile.

Set `previous = ":features"` to extend an earlier stage. Answers carry forward
into the next stage and its header; inherited names cannot be overwritten.
Tests run in declaration order, so later tests can also use earlier answers in
the same stage. Each stage is cached as one Bazel action.

## Decisions in Starlark

A generated header works with ordinary C/C++ rules. Choosing sources or flags
from measured answers requires a custom rule using `ctx.actions.map_directory`:
analysis cannot read results from actions it has just declared.

[The selection example](examples/selection/build.bzl) shows the pattern:
freeze the toolchain with `toolchain_context(ctx)`, obtain the tree from
`AutoconfInfo.results`, and call `read_results` in the execution-time callback.
`check_toolchain` rejects mismatched probe and consumer configurations. The
callback then declares compile, archive, and link actions inside output trees.
It cannot create configured targets or change an ordinary `select()`.

`read_payload` supports a separate structured-JSON tree for more complex
consumers. [The planning example](examples/planning) uses it for bounded query
discovery and fails if the stage limit leaves an answer unmeasured.

The exports have short descriptions in [autoconf.bzl](autoconf.bzl).
Implementation files live under `private/`; the public Starlark library target
is `@autoconf.bzl//:autoconf`.

## Build and test

Use Bazel 9.3.0 and enable `--experimental_allow_map_directory`. The example and
test modules already enable it and register LLVM toolchains. The root ruleset
does not register a compiler or fetch example projects.

Run the core tests and all examples from `tests/`:

```console
bazel test //...
```

You can also run `bazel test //...` from `examples/` for the examples alone.
The modules import the parent `.bazelrc`, including CI and RBE settings.
Linux and Windows CI clients use Linux RBE workers; macOS executes locally.
The buildifier check runs locally without a sandbox when the target matches the
client host. Cross-target jobs rely on the Linux and macOS formatting checks.

| Example | What the test verifies |
| --- | --- |
| [header](examples/header) | Compiler/header checks and a false macro in an ordinary C test |
| [custom](examples/custom) | Typed custom answers, runfiles, and a staged ABI assertion |
| [selection](examples/selection) | Both branches change archive members and linked behavior |
| [planning](examples/planning) | Query discovery, convergence, and an insufficient stage bound |
| [libogg](examples/libogg) | Real upstream sources built with measured headers, both upstream self-tests, and a native configure comparison |

libogg 1.3.5 is fetched by a checksum-pinned `http_archive` in
[examples/MODULE.bazel](examples/MODULE.bazel). Its upstream sources and BSD
license remain in that archive. The Bazel build uses this ruleset's headers.
The reference test runs upstream configure independently with the same compiler
and target, compares the relevant macros and typedefs, and reports differences.
Only equivalent boolean undef/0 conventions are normalized. The upstream
self-tests are Bazel `cc_test` targets; no build step invokes make.

Probe trees contain `results.json`, `diagnostics.json`, and per-probe stdout and
stderr. The native comparison writes a fresh `comparison.json`, `configure.log`,
and `toolchain.json` under its Bazel `test.outputs/` directory. For example,
after testing from `examples/`, the report is at:

```text
bazel-testlogs/libogg/native_reference_test/test.outputs/comparison.json
```

Reports are generated by the tests. CI downloads them from RBE and uploads them
with the test logs.

The execution helpers require POSIX and C++17. Cross-compilation is supported
for compile/link probes; runnable examples and the native reference require
matching target and test execution platforms. The reference uses declared LLVM
binutils and POSIX utilities from the pinned CI image. Toolchain files must be
declared inputs; files reached through undeclared system paths are outside
Bazel's cache guarantees.

## License

Apache-2.0. See [LICENSE](LICENSE). Bazel license metadata is attached through
`REPO.bazel`; third-party projects retain their own licenses.
