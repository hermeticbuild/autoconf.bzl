# SPDX-License-Identifier: Apache-2.0

"""Exercise the same public decoders used by actual template callbacks."""

load("@autoconf.bzl//:autoconf.bzl", "read_payload", "read_results")

def _directory(names):
    # Match the filename-only surface of ExpandedDirectory; contents are absent.
    return struct(children = [struct(basename = name) for name in names])

def _impl(ctx):
    a = read_results(_directory(["Z=string=clang", "B=int=-8", "log.stderr", "A=bool=0"]))
    b = read_results(_directory(["A=bool=0", "B=int=-8", "Z=string=clang"]))
    if a != b or a != {"A": False, "B": -8, "Z": "clang"}:
        fail("scalar decoding failed")
    if read_payload(_directory(["00000001=317d", "payload.json", "00000000=7b2278223a"])) != {"x": 1}:
        fail("structured payload decoding failed")
    return [AnalysisTestResultInfo(success = True, message = "wire decoding passed")]

wire_test = rule(implementation = _impl, test = True, analysis_test = True)
