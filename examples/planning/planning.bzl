# SPDX-License-Identifier: Apache-2.0

"""Reusable pattern: bounded templates plan, normal actions measure, then replay."""

load("@autoconf.bzl//:autoconf.bzl", "read_payload", "toolchain_context")
load("@rules_cc//cc:find_cc_toolchain.bzl", "use_cc_toolchain")

def _missing(answers):
    # Consumer decision logic stays outside the core. No placeholder reaches final build.
    if "WIDE_LONG" not in answers:
        return {"name": "WIDE_LONG", "prior": {}, "source": "_Static_assert(sizeof(long) >= 8, \"wide\");"}
    name = "INT128" if answers["WIDE_LONG"] else "INT64"
    if name not in answers:
        return {"name": name, "prior": {"WIDE_LONG": answers["WIDE_LONG"]}, "source": "__int128 x;" if name == "INT128" else "long long x;"}
    return None

def _state(directory):
    state = read_payload(directory)
    if type(state) != "dict" or sorted(state.keys()) != ["answers", "cache", "history"] or type(state["answers"]) != "dict" or type(state["history"]) != "list" or type(state["cache"]) != "dict":
        fail("invalid consumer payload schema")
    for name, value in state["answers"].items():
        if name not in ["WIDE_LONG", "INT128", "INT64"] or type(value) != "bool":
            fail("invalid measured answer")
    for identity, value in state["cache"].items():
        if type(identity) != "string" or type(value) != "bool":
            fail("invalid query cache")
    for record in state["history"]:
        if type(record) != "dict" or sorted(record.keys()) != ["identity", "value"] or record["identity"] not in state["cache"] or state["cache"][record["identity"]] != record["value"]:
            fail("invalid measured query history")
    return state

def _plan(tctx, *, input_directories, output_directories, additional_inputs, tools, additional_params):
    state = _state(input_directories["state"])
    request = {"query": _missing(state["answers"]), "toolchain": json.decode(additional_params["toolchain"])}
    out = tctx.declare_file("request.json", directory = output_directories["manifest"])
    tctx.run(executable = tools["writer"], outputs = [out], arguments = ["write", out.path, json.encode(request)])

def require_complete(state):
    query = _missing(state["answers"])
    if query:
        fail("planning stage bound exhausted; unmeasured query: " + query["name"])
    answers = state["answers"]
    name = "INT128" if answers["WIDE_LONG"] else "INT64"
    if not answers[name]:
        fail("example requires the measured integer capability")
    return answers

def _finish(tctx, *, input_directories, output_directories, additional_inputs, tools, additional_params):
    state = _state(input_directories["state"])
    answers = require_complete(state)
    name = "INT128" if answers["WIDE_LONG"] else "INT64"
    source = tctx.declare_file("configured.c", directory = output_directories["build"])
    obj = tctx.declare_file("configured.o", directory = output_directories["build"])
    tc = json.decode(additional_params["toolchain"])
    text = ("__int128" if name == "INT128" else "long long") + " configured_integer;\n"
    tctx.run(executable = tools["writer"], outputs = [source], arguments = ["write", source.path, text])
    tctx.run(executable = tools["writer"], tools = [tools["cc_files"]], inputs = [source], outputs = [obj], arguments = ["exec", json.encode(tc["compile_env"]), tc["cc"]] + tc["compile_flags"] + ["-c", source.path, "-o", obj.path])

def _impl(ctx):
    tc = toolchain_context(ctx)
    state = ctx.actions.declare_directory(ctx.label.name + ".state0")
    helper = ctx.attr._planner[DefaultInfo].files_to_run
    writer = ctx.attr._writer[DefaultInfo].files_to_run
    ctx.actions.run(executable = helper, arguments = ["initial", state.path], outputs = [state], toolchain = "@rules_cc//cc:toolchain_type")
    for i in range(ctx.attr.stages):
        manifest = ctx.actions.declare_directory(ctx.label.name + ".plan" + str(i))
        ctx.actions.map_directory(input_directories = {"state": state}, output_directories = {"manifest": manifest}, tools = {"writer": writer}, additional_params = {"toolchain": json.encode(tc.spec)}, implementation = _plan, toolchain = "@rules_cc//cc:toolchain_type")
        next_state = ctx.actions.declare_directory(ctx.label.name + ".state" + str(i + 1))
        ctx.actions.run(executable = helper, inputs = depset([manifest, state], transitive = [tc.files]), arguments = [manifest.path, state.path, next_state.path], outputs = [next_state], toolchain = "@rules_cc//cc:toolchain_type")
        state = next_state
    build = ctx.actions.declare_directory(ctx.label.name + ".build")
    ctx.actions.map_directory(input_directories = {"state": state}, output_directories = {"build": build}, tools = {"writer": writer, "cc_files": tc.files}, additional_params = {"toolchain": json.encode(tc.spec)}, implementation = _finish, toolchain = "@rules_cc//cc:toolchain_type")
    return [DefaultInfo(files = depset([build, state])), OutputGroupInfo(state = depset([state]))]

planned_build = rule(implementation = _impl, attrs = {"stages": attr.int(default = 3, values = [1, 2, 3, 4]), "_planner": attr.label(default = "//planning:planner", executable = True, cfg = "exec"), "_writer": attr.label(default = "@autoconf.bzl//private:executor", executable = True, cfg = "exec")}, fragments = ["cpp"], toolchains = use_cc_toolchain())
