load("@rules_cc//cc:defs.bzl", "cc_library")

genrule(
    name = "default_config",
    srcs = ["cbor_tags_config.h.in"],
    outs = ["generated/cbor_tags/cbor_tags_config.h"],
    cmd = "sed -E 's/^#cmakedefine01 (.*)/#define \\1 0/' $< > $@",
)

cc_library(
    name = "config",
    hdrs = [":default_config"],
    strip_include_prefix = "generated",
)

cc_library(
    name = "cbor_tags",
    hdrs = glob(["include/**/*.h"]),
    # Keep library headers out of -isystem so LLVM records their source coverage.
    strip_include_prefix = "include",
    deps = [":config", "@fmt//:fmt", "@tl_expected//:expected", "@nameof//:nameof"],
    visibility = ["//visibility:public"],
)
