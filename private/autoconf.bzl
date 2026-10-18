# SPDX-License-Identifier: Apache-2.0

"""Configured-toolchain probes; measured values become available only at execution."""

load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")

visibility("//...")

_CC_TOOLCHAIN = "@rules_cc//cc:toolchain_type"
AutoconfInfo = provider(
    doc = "Artifact handles and frozen toolchain recipe; answers exist only at execution.",
    fields = {
        "results": "Tree artifact containing cumulative typed markers, JSON, and stage diagnostics.",
        "toolchain_id": "Resolved CcToolchain identifier used to check stage/consumer compatibility.",
        "toolchain": "Serializable dictionary of selected tools, expanded flags, and environments.",
        "tool_files": "Depset of declared toolchain files to supply as consumer action inputs.",
    },
)

_ALPHA = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"
_DIGITS = "0123456789"
_TOKEN = _ALPHA + _DIGITS + ".+-"

def _name(name):
    return type(name) == "string" and bool(name) and name[0] in _ALPHA and all([c in _ALPHA + _DIGITS for c in name.elems()])

def _strings(value):
    return type(value) == "list" and all([type(v) == "string" for v in value])

def _validate(name, test, aliases):
    if not _name(name) or type(test) != "dict":
        fail("invalid probe name or descriptor: " + str(name))
    kind = test.get("kind")
    if kind in ["compile", "assemble", "link"]:
        keys = ["kind", "source", "flags", "failure_codes"]
        if type(test.get("source")) != "string" or not _strings(test.get("flags")):
            fail(name + ": source/flags must be string/list of strings")
    elif kind == "command":
        keys = ["kind", "tool", "arguments", "result", "failure_codes", "timeout"]
        if test.get("tool") not in aliases:
            fail(name + ": undeclared tool alias")
        if not _strings(test.get("arguments")) or test.get("result") not in ["bool", "int", "string"]:
            fail(name + ": invalid arguments or result type")
        if type(test.get("timeout")) != "int" or test["timeout"] <= 0:
            fail(name + ": timeout must be positive seconds")
    else:
        fail(name + ": unsupported probe kind: " + str(kind))
    if sorted(test.keys()) != sorted(keys):
        fail(name + ": invalid descriptor schema")
    codes = test.get("failure_codes")
    if type(codes) != "list" or not all([type(c) == "int" and c > 0 and c <= 255 for c in codes]):
        fail(name + ": failure_codes must be positive exit statuses (1..255)")

def compile_test(source, flags = [], failure_codes = [1]):
    """Describe an inline-C compile-only check; accepted rejection statuses yield false."""
    return dict(kind = "compile", source = source, flags = flags, failure_codes = failure_codes)

def assemble_test(source, flags = [], failure_codes = [1]):
    """Describe a preprocessed-assembly (.S) compile-only check returning a boolean."""
    return dict(kind = "assemble", source = source, flags = flags, failure_codes = failure_codes)

def link_test(source, flags = [], failure_codes = [1]):
    """Describe a C program link check; compile and link, but never execute the program."""
    return dict(kind = "link", source = source, flags = flags, failure_codes = failure_codes)

def command_test(tool, arguments = [], result = "bool", failure_codes = [1], timeout = 30):
    """Describe a declared tool-alias invocation yielding bool, int, or a safe string.

    Arguments support scalar substitutions and whole-argument flag-list splicing.
    Only boolean checks accept rejection statuses; int/string checks must succeed.
    """
    if result not in ["bool", "int", "string"]:
        fail("unsupported result type: " + result)
    return dict(kind = "command", tool = tool, arguments = arguments, result = result, failure_codes = failure_codes, timeout = timeout)

def toolchain_context(ctx):
    """Return an analysis-time struct(id, files, spec) for custom consumer actions.

    The recipe captures requested toolchain features, not target-specific CcInfo
    includes, defines, or dependencies; consumers must supply those separately.
    """
    cc = find_cc_toolchain(ctx)
    features = cc_common.configure_features(ctx = ctx, cc_toolchain = cc, requested_features = ctx.features, unsupported_features = ctx.disabled_features)
    cv = cc_common.create_compile_variables(feature_configuration = features, cc_toolchain = cc)
    lv = cc_common.create_link_variables(feature_configuration = features, cc_toolchain = cc, is_linking_dynamic_library = False, is_static_linking_mode = False)
    spec = {}
    for key, action in [("cc", ACTION_NAMES.c_compile), ("ld", ACTION_NAMES.cpp_link_executable), ("ar", ACTION_NAMES.cpp_link_static_library)]:
        spec[key] = cc_common.get_tool_for_action(feature_configuration = features, action_name = action)
    for prefix, action, variables in [("compile", ACTION_NAMES.c_compile, cv), ("link", ACTION_NAMES.cpp_link_executable, lv)]:
        spec[prefix + "_flags"] = cc_common.get_memory_inefficient_command_line(feature_configuration = features, action_name = action, variables = variables)
        spec[prefix + "_env"] = cc_common.get_environment_variables(feature_configuration = features, action_name = action, variables = variables)
    return struct(id = cc.toolchain_id, files = cc.all_files, spec = spec)

def check_toolchain(probes, context):
    """Compare AutoconfInfo with toolchain_context; fail on an id or recipe mismatch."""
    if probes.toolchain_id != context.id or probes.toolchain != context.spec:
        fail("autoconf toolchain mismatch: identifiers and frozen descriptions must match")

def _autoconf_impl(ctx):
    tc = toolchain_context(ctx)
    tools = {}
    runners = []
    for target, alias in ctx.attr.tools.items():
        if not _name(alias) or alias in tools:
            fail("invalid or duplicate custom tool alias: " + alias)
        runner = target[DefaultInfo].files_to_run
        if runner.executable == None or runner.executable.is_source:
            fail("custom tool must provide an executable: " + str(target.label))
        tools[alias] = runner.executable.path
        runners.append(runner)
    tests = json.decode(ctx.attr.tests)
    if type(tests) != "dict":
        fail("tests must be a dictionary")
    for name, test in tests.items():
        _validate(name, test, tools)
    previous = ctx.attr.previous[AutoconfInfo] if ctx.attr.previous else None
    if previous:
        check_toolchain(previous, tc)
    spec = ctx.actions.declare_file(ctx.label.name + ".spec.json")
    results = ctx.actions.declare_directory(ctx.label.name + ".results")
    ctx.actions.write(spec, json.encode({
        "tests": tests,
        "test_order": ctx.attr.test_order,
        "toolchain": tc.spec,
        "tools": tools,
        "previous": previous.results.path if previous else None,
        "environment": ctx.attr.environment,
    }))
    ctx.actions.run(
        executable = ctx.attr._runner[DefaultInfo].files_to_run,
        arguments = [spec.path, results.path],
        inputs = depset(
            [spec] + ctx.files.data + ([previous.results] if previous else []),
            transitive = [tc.files],
        ),
        tools = runners,
        outputs = [results],
        mnemonic = "Autoconf",
        toolchain = _CC_TOOLCHAIN,
    )
    return [
        AutoconfInfo(results = results, toolchain_id = tc.id, toolchain = tc.spec, tool_files = tc.files),
        DefaultInfo(files = depset([results])),
        OutputGroupInfo(spec = depset([spec])),
    ]

_autoconf = rule(
    implementation = _autoconf_impl,
    attrs = {
        "tests": attr.string(mandatory = True),
        "test_order": attr.string_list(),
        "tools": attr.label_keyed_string_dict(cfg = "exec", allow_files = True),
        "data": attr.label_list(allow_files = True),
        "previous": attr.label(providers = [AutoconfInfo]),
        "environment": attr.string_dict(),
        "_runner": attr.label(default = "//private:runner", executable = True, cfg = "exec"),
    },
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def autoconf(name, tests, tools = {}, data = [], previous = None, environment = {}, **kwargs):
    """Register one probe stage producing AutoconfInfo and a cumulative result tree.

    tests maps answer names to constructor descriptors. tools maps executable
    labels to aliases; data adds declared inputs without path substitution.
    previous inherits an earlier stage's answers; inherited names cannot be reused.
    environment overrides the toolchain environment without inheriting the host's.
    """
    if type(tests) != "dict":
        fail("tests must be a dictionary")
    _autoconf(
        name = name,
        tests = json.encode(tests),
        test_order = tests.keys(),
        tools = tools,
        data = data,
        previous = previous,
        environment = environment,
        **kwargs
    )

def _decode_markers(basenames):
    """Decode names only; reject every malformed marker-like entry and duplicate."""
    answers = {}
    for basename in sorted(basenames):
        if "=" not in basename:
            continue
        fields = basename.split("=")
        if len(fields) != 3 or not _name(fields[0]):
            fail("malformed result marker: " + basename)
        name, tag, value = fields
        if name in answers:
            fail("duplicate answer: " + name)
        if tag == "bool":
            if value not in ["0", "1"]:
                fail("invalid boolean marker: " + basename)
            value = value == "1"
        elif tag == "int":
            digits = value[1:] if value.startswith("-") else value
            if not digits or not all([c in _DIGITS for c in digits.elems()]) or (len(digits) > 1 and digits.startswith("0")) or value == "-0":
                fail("noncanonical integer marker: " + basename)
            value = int(value)
        elif tag == "string":
            if not value or len(value) > 120 or value in [".", ".."] or not all([c in _TOKEN for c in value.elems()]):
                fail("unsafe string marker: " + basename)
        else:
            fail("unknown marker type: " + basename)
        answers[name] = value
    return answers

def read_results(expanded_directory):
    """Read typed filenames from an ExpandedDirectory; never opens file contents."""
    return _decode_markers([f.basename for f in expanded_directory.children])

def _header_define(file):
    values = _decode_markers([file.basename])
    lines = []
    for name, value in values.items():
        if type(value) == "bool":
            value = "1" if value else "0"
        elif type(value) == "string":
            value = json.encode(value)
        else:
            value = str(value)
        lines.append("#define " + name + " " + value)
    return lines

def _header_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".h")
    tree = ctx.attr.probes[AutoconfInfo].results
    lines = ctx.actions.args()
    lines.add_all([tree], map_each = _header_define)
    lines.use_param_file("@%s", use_always = True)
    lines.set_param_file_format("multiline")
    ctx.actions.run(
        executable = ctx.attr._writer[DefaultInfo].files_to_run,
        inputs = [tree],
        outputs = [out],
        arguments = ["header", out.path, lines],
        mnemonic = "AutoconfHeader",
    )
    return [DefaultInfo(files = depset([out]))]

autoconf_header = rule(
    doc = "Generate <name>.h from probes, including inherited answers. Test booleans with #if, since false is defined as 0.",
    implementation = _header_impl,
    attrs = {
        "probes": attr.label(doc = "Probe stage whose cumulative answers become C macro definitions.", mandatory = True, providers = [AutoconfInfo]),
        "_writer": attr.label(default = "//private:executor", executable = True, cfg = "exec"),
    },
)
