# SPDX-License-Identifier: Apache-2.0

"""Compare real artifacts across configurations in one Bazel invocation."""

def _variant_impl(settings, attr):
    features = [f for f in settings["//command_line_option:features"] if f not in ["thin_lto", "-thin_lto"]]
    return {
        "//command_line_option:features": features + (["thin_lto"] if attr.thin_lto else []),
        "//:cache_mode": attr.mode,
    }

_variant = transition(
    implementation = _variant_impl,
    inputs = ["//command_line_option:features"],
    outputs = ["//command_line_option:features", "//:cache_mode"],
)

def _impl(ctx):
    target = ctx.attr.target[0]
    def_copy = []
    spec_copy = []
    for group, files in [("files", target[DefaultInfo].files), ("spec", target[OutputGroupInfo].spec if OutputGroupInfo in target and hasattr(target[OutputGroupInfo], "spec") else depset())]:
        for source in files.to_list():
            path = ctx.label.name + "/" + group + "/" + source.basename
            out = ctx.actions.declare_directory(path) if source.is_directory else ctx.actions.declare_file(path)
            ctx.actions.run(executable = ctx.attr._copy[DefaultInfo].files_to_run, inputs = [source], outputs = [out], arguments = [source.path, out.path], mnemonic = "CopyConfiguredArtifact")
            (def_copy if group == "files" else spec_copy).append(out)
    return [DefaultInfo(files = depset(def_copy)), OutputGroupInfo(spec = depset(spec_copy))]

configured_target = rule(
    implementation = _impl,
    attrs = {
        "target": attr.label(cfg = _variant, mandatory = True),
        "_copy": attr.label(default = "//:copy", cfg = "exec", executable = True),
        "mode": attr.string(default = "default", values = ["default", "alternate"]),
        "thin_lto": attr.bool(),
        "_allowlist_function_transition": attr.label(default = "@bazel_tools//tools/allowlists/function_transition_allowlist"),
    },
)

def _setting_impl(ctx):
    return []

cache_mode = rule(implementation = _setting_impl, build_setting = config.string(flag = True))
