# SPDX-License-Identifier: Apache-2.0

"""libogg configuration belongs to this consumer, never the core API."""

load("@autoconf.bzl//:autoconf.bzl", "AutoconfInfo", "autoconf", "command_test", "compile_test", "read_results")

# libogg's native configure adds these flags for its supported hosted setups.
MACOS_FLAGS = ["-fno-common", "-O4", "-Wall", "-fsigned-char", "-ffast-math"]
LINUX_FLAGS = ["-O2", "-Wall", "-ffast-math", "-fsigned-char"]
PACKAGE_FLAGS = select({"@platforms//os:macos": MACOS_FLAGS, "@platforms//os:linux": LINUX_FLAGS})

def _probes(name, flags):
    tests = {}
    for header in ["dlfcn.h", "inttypes.h", "memory.h", "stdint.h", "stdlib.h", "strings.h", "string.h", "sys/stat.h", "sys/types.h", "unistd.h"]:
        tests["HAVE_" + header.upper().replace("/", "_").replace(".", "_")] = compile_test("#include <" + header + ">\n", flags = flags)
    tests["STDC_HEADERS"] = compile_test("#include <stdlib.h>\n#include <stdarg.h>\n#include <string.h>\n#include <float.h>\n", flags = flags)
    for typename in ["int16_t", "uint16_t", "u_int16_t", "int32_t", "uint32_t", "u_int32_t", "int64_t", "uint64_t", "short", "int", "long", "long long"]:
        tests["SIZEOF_" + typename.upper().replace(" ", "_")] = command_test("sizeof", ["{work}", typename, "{cc}", "{compile_flags}"] + flags, result = "int", failure_codes = [])
    autoconf(name = name, tests = tests, tools = {":measure": "sizeof"})

def _types(tctx, *, input_directories, output_directories, additional_inputs, tools, additional_params):
    values = read_results(input_directories["probes"])
    lines = ["#pragma once"]
    for header in ["inttypes.h", "stdint.h", "sys/types.h"]:
        name = "HAVE_" + header.upper().replace("/", "_").replace(".", "_")
        if not values[name]:
            fail("libogg requires " + name)
        lines.append("#include <" + header + ">")
    for width in [16, 32, 64]:
        for prefix in ["int", "uint"]:
            name = prefix + str(width) + "_t"
            if values["SIZEOF_" + name.upper()] != width // 8:
                fail("unsupported exact-width type: " + name)
            lines.append("typedef " + name + " ogg_" + name + ";")
    out = tctx.declare_file("ogg/config_types.h", directory = output_directories["include"])
    tctx.run(executable = tools["writer"], outputs = [out], arguments = ["write", out.path, "\n".join(lines) + "\n"])

def _types_impl(ctx):
    out = ctx.actions.declare_directory("include")
    ctx.actions.map_directory(
        input_directories = {"probes": ctx.attr.probes[AutoconfInfo].results},
        output_directories = {"include": out},
        tools = {"writer": ctx.attr._writer[DefaultInfo].files_to_run},
        implementation = _types,
    )
    return [DefaultInfo(files = depset([out]))]

ogg_types = rule(implementation = _types_impl, attrs = {
    "probes": attr.label(providers = [AutoconfInfo]),
    "_writer": attr.label(default = "@autoconf.bzl//private:executor", executable = True, cfg = "exec"),
})

def ogg_probes(name):
    _probes(name + "_macos", MACOS_FLAGS)
    _probes(name + "_linux", LINUX_FLAGS)
    native.alias(name = name, actual = select({"@platforms//os:macos": ":" + name + "_macos", "@platforms//os:linux": ":" + name + "_linux"}))
