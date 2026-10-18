# SPDX-License-Identifier: Apache-2.0

"""Bazel analysis tests: expected failures are assertions, not failed builds."""

load("@autoconf.bzl//:autoconf.bzl", "AutoconfInfo")

def _failure_impl(ctx):
    target = ctx.attr.target_under_test
    if type(target) == "list":
        target = target[0]
    messages = []
    if AnalysisFailureInfo in target:
        messages = [cause.message for cause in target[AnalysisFailureInfo].causes.to_list()]
    found = any([ctx.attr.expected in message for message in messages])
    return [AnalysisTestResultInfo(
        success = found,
        message = "expected diagnostic %r; got %s" % (ctx.attr.expected, messages),
    )]

failure_test = rule(
    implementation = _failure_impl,
    test = True,
    analysis_test = True,
    attrs = {
        "expected": attr.string(mandatory = True),
        "target_under_test": attr.label(cfg = analysis_test_transition(settings = {
            "//command_line_option:allow_analysis_failures": True,
        })),
    },
)

def failure_tests(name):
    cases = {
        "duplicate_alias": "duplicate custom tool alias",
        "not_executable": "must provide an executable",
        "missing_alias": "undeclared tool alias",
        "bad_schema": "unsupported probe kind",
        "bad_codes": "positive exit statuses",
        "mismatched_toolchain": "toolchain mismatch",
        "bad_bool": "invalid boolean marker",
        "bad_int": "noncanonical integer",
        "bad_type": "unknown marker type",
        "duplicate_marker": "duplicate answer",
        "unsafe_marker": "unsafe string",
        "payload_gap": "noncontiguous",
        "payload_hex": "invalid payload hex",
        "payload_duplicate": "duplicate index",
        "planning_bound": "stage bound exhausted; unmeasured query:",
    }
    for target, expected in cases.items():
        failure_test(name = target + "_test", target_under_test = ":" + target, expected = expected)
    native.test_suite(name = name, tests = [":" + target + "_test" for target in cases])

_Actions = provider(fields = ["actions"])

def _actions_impl(target, ctx):
    return [_Actions(actions = target.actions)]

_actions = aspect(implementation = _actions_impl)

def _dependencies_impl(ctx):
    first = [a for a in ctx.attr.first[_Actions].actions if a.mnemonic == "Autoconf"]
    second = [a for a in ctx.attr.second[_Actions].actions if a.mnemonic == "Autoconf"]
    failures = []
    if len(first) != 1 or len(second) != 1:
        failures.append("each stage must register exactly one probe action")
    else:
        if ctx.attr.first[AutoconfInfo].results not in second[0].inputs.to_list():
            failures.append("dependent stage must declare the previous result tree as an action input")
        if ctx.file.data not in first[0].inputs.to_list():
            failures.append("declared data must invalidate the first probe action")
        if ctx.executable.tool not in first[0].inputs.to_list():
            failures.append("declared executable must invalidate the first probe action")
    return [AnalysisTestResultInfo(success = not failures, message = "\n".join(failures))]

dependency_test = rule(
    implementation = _dependencies_impl,
    test = True,
    analysis_test = True,
    attrs = {
        "first": attr.label(aspects = [_actions]),
        "second": attr.label(aspects = [_actions]),
        "data": attr.label(allow_single_file = True),
        "tool": attr.label(executable = True, cfg = "exec"),
    },
)
