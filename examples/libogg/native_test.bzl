# SPDX-License-Identifier: Apache-2.0

"""Native-reference test with toolchain files declared as runfiles."""

load("@autoconf.bzl//:autoconf.bzl", "AutoconfInfo")

def _impl(ctx):
    files = ctx.attr.probes[AutoconfInfo].tool_files.to_list() + ctx.files.data
    roots = {}
    for file in files:
        short = file.short_path
        if short.startswith("../"):
            repo, _, relative = short[3:].partition("/")
            roots[file.path[:-len(relative)].rstrip("/")] = "../" + repo
        elif short:
            roots[file.path[:-len(short)].rstrip("/")] = "."
    manifest = ctx.actions.declare_file(ctx.label.name + ".paths.json")
    ctx.actions.write(manifest, json.encode(roots))
    driver = ctx.attr._driver[DefaultInfo]
    executable = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.symlink(output = executable, target_file = driver.files_to_run.executable, is_executable = True)
    runfiles = ctx.runfiles(files = files + [manifest]).merge(driver.default_runfiles)
    return [DefaultInfo(executable = executable, runfiles = runfiles)]

native_reference_test = rule(
    implementation = _impl,
    test = True,
    attrs = {
        "data": attr.label_list(allow_files = True),
        "probes": attr.label(mandatory = True, providers = [AutoconfInfo]),
        "_driver": attr.label(default = "//libogg:native_reference", executable = True, cfg = "exec"),
    },
)
