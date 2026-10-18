# SPDX-License-Identifier: Apache-2.0

"""A declared runfile whose contents change without changing the probe spec."""

def _impl(ctx):
    out = ctx.actions.declare_file("cache_value")
    ctx.actions.write(out, ctx.attr.value)
    return [DefaultInfo(files = depset([out]))]

cache_input = rule(implementation = _impl, attrs = {"value": attr.string()})
