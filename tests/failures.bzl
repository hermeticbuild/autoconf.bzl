# SPDX-License-Identifier: Apache-2.0

"""Negative fixtures exercised by Bazel analysis tests."""

load("@autoconf.bzl//:autoconf.bzl", "read_payload", "read_results")
load("@autoconf_examples//planning:planning.bzl", "require_complete")

def _impl(ctx):
    directory = struct(children = [struct(basename = name) for name in ctx.attr.markers])
    if ctx.attr.planning:
        require_complete({"answers": {"WIDE_LONG": True}})
    elif ctx.attr.payload:
        read_payload(directory)
    else:
        read_results(directory)
    fail("negative fixture unexpectedly accepted invalid input")

invalid_wire = rule(implementation = _impl, attrs = {"markers": attr.string_list(), "payload": attr.bool(), "planning": attr.bool()})
