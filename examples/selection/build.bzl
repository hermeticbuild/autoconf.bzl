# SPDX-License-Identifier: Apache-2.0

"""Example consumer: execution-time source and flag selection, archive, then link."""

load("@autoconf.bzl//:autoconf.bzl", "AutoconfInfo", "check_toolchain", "read_results", "toolchain_context")
load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")

def _select(tctx, *, input_directories, output_directories, additional_inputs, tools, additional_params):
    answers = read_results(input_directories["probes"])
    enabled = answers["HAVE_CAPABILITY"]
    tc = json.decode(additional_params["toolchain"])
    tree = output_directories["build"]
    src = additional_inputs["enabled" if enabled else "disabled"]
    obj = tctx.declare_file("enabled.o" if enabled else "disabled.o", directory = tree)
    flag = "-DSELECTED_VALUE=" + ("73" if enabled else "19")
    tctx.run(executable = tools["executor"], tools = [tools["cc_files"]], inputs = [src], outputs = [obj], arguments = ["exec", json.encode(tc["compile_env"]), tc["cc"]] + tc["compile_flags"] + [flag, "-c", src.path, "-o", obj.path])
    archive = tctx.declare_file("libselected.a", directory = tree)
    tctx.run(executable = tools["executor"], tools = [tools["cc_files"]], inputs = [obj], outputs = [archive], arguments = ["exec", additional_params["archive_env"], tc["ar"]] + [f.replace("__ARCHIVE__", archive.path) for f in json.decode(additional_params["archive_flags"])] + [obj.path])
    main = additional_inputs["main"]
    main_obj = tctx.declare_file("main.o", directory = tree)
    tctx.run(executable = tools["executor"], tools = [tools["cc_files"]], inputs = [main], outputs = [main_obj], arguments = ["exec", json.encode(tc["compile_env"]), tc["cc"]] + tc["compile_flags"] + [flag, "-c", main.path, "-o", main_obj.path])
    exe = tctx.declare_file("app", directory = tree)
    tctx.run(executable = tools["executor"], tools = [tools["cc_files"]], inputs = [main_obj, archive], outputs = [exe], arguments = ["exec", json.encode(dict(tc["compile_env"], **tc["link_env"])), tc["ld"]] + tc["link_flags"] + [main_obj.path, archive.path, "-o", exe.path])

def _impl(ctx):
    tc = toolchain_context(ctx)
    cc = find_cc_toolchain(ctx)
    features = cc_common.configure_features(ctx = ctx, cc_toolchain = cc, requested_features = ctx.features, unsupported_features = ctx.disabled_features)
    av = cc_common.create_link_variables(feature_configuration = features, cc_toolchain = cc, is_using_linker = False, output_file = "__ARCHIVE__")
    archive_flags = cc_common.get_memory_inefficient_command_line(feature_configuration = features, action_name = ACTION_NAMES.cpp_link_static_library, variables = av)
    archive_env = cc_common.get_environment_variables(feature_configuration = features, action_name = ACTION_NAMES.cpp_link_static_library, variables = av)
    probes = ctx.attr.probes[AutoconfInfo]
    check_toolchain(probes, tc)
    tree = ctx.actions.declare_directory(ctx.label.name + ".build")
    ctx.actions.map_directory(input_directories = {"probes": probes.results}, output_directories = {"build": tree}, additional_inputs = {"enabled": ctx.file.enabled, "disabled": ctx.file.disabled, "main": ctx.file.main}, tools = {"cc_files": tc.files, "executor": ctx.attr._executor[DefaultInfo].files_to_run}, additional_params = {"toolchain": json.encode(tc.spec), "archive_flags": json.encode(archive_flags), "archive_env": json.encode(archive_env)}, implementation = _select, toolchain = "@rules_cc//cc:toolchain_type", env = dict(tc.spec["compile_env"], **tc.spec["link_env"]))
    return [DefaultInfo(files = depset([tree]), runfiles = ctx.runfiles(files = [tree]))]

selected_application = rule(implementation = _impl, attrs = {"_executor": attr.label(default = "@autoconf.bzl//private:executor", executable = True, cfg = "exec"), "probes": attr.label(providers = [AutoconfInfo]), "enabled": attr.label(allow_single_file = True), "disabled": attr.label(allow_single_file = True), "main": attr.label(allow_single_file = True)}, fragments = ["cpp"], toolchains = use_cc_toolchain())
