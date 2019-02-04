package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
)

cc_library(
    name = "hdrhistogram_c",
    srcs = glob(["hdrhistogram_c/src/*.c"]),
    hdrs = glob(["hdrhistogram_c/src/*.h"]),
    visibility = ["//visibility:public"],
)

envoy_cc_binary(
    name = "nighthawk_client",
    repository = "@envoy",
    stamped = True,
    deps = [
        "//source/exe:nighthawk_client_entry_lib",
    ],
)
