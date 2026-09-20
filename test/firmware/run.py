#!/usr/bin/env python3
"""Compile actual firmware/loader functions with deterministic host-side shims.

This exercises source logic, not a running kernel or boot loader. Generated
translation units and executables stay in the requested build directory.
"""
import argparse
import pathlib
import re
import resource
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("build", type=pathlib.Path)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    args.build.mkdir(parents=True, exist_ok=True)
    here = pathlib.Path(__file__).resolve().parent
    failures = 0
    suites = {
        "firmware": ["file_names", "preload_names", "references", "reacquire",
                     "file_errors", "size_limit", "preload_boundary", "allocation_failure", "registry_full",
                     "module_fallback", "module_parent", "search_order"],
        "ucode": ["amd_update", "newest", "oversize", "truncated", "no_match", "intel", "unknown", "missing"],
        "loader": ["microcode", "microcode_override", "firmware_list", "modules",
                   "repeat", "kernel_failure", "firmware_separators"],
    }
    for suite, cases in suites.items():
        if suite == "firmware":
            source = (args.source / "sys/kern/subr_firmware.c").read_text()
            source = source[:source.index("/*\n * Module glue.")]
            source = re.sub(r"^#include .*\n", "", source, flags=re.M)
        elif suite == "ucode":
            source = (args.source / "sys/platform/pc64/x86_64/ucode.c").read_text()
            source = re.sub(r"^#include .*\n", "", source, flags=re.M)
        else:
            source = (args.source / "stand/boot/dloader/cmds.c").read_text()
            source = source[source.index("static int\ncommand_loadall("):]
            source = source[:source.index("/*\n * Clear all menus")]
        unit = args.build / (suite + ".c")
        shim = (here / (suite + "_test.c")).read_text()
        unit.write_text(shim.replace("/* SOURCE_UNDER_TEST */", source))
        binary = args.build / suite
        subprocess.run([args.cc, "-std=gnu11", "-O0", "-g", "-Wall", "-Wextra",
                        "-Werror", "-Wno-unused-parameter", "-Wno-unused-function",
                        "-Wno-unused-variable", "-Wno-sign-compare", "-Wno-pointer-sign", str(unit), "-o", str(binary)], check=True)
        for case in cases:
            result = subprocess.run([str(binary), case], capture_output=True, text=True)
            print(f"{suite}/{case}: {'PASS' if result.returncode == 0 else 'FAIL'}")
            if result.returncode:
                failures += 1
                print(result.stdout + result.stderr, end="")
    print(f"FIRMWARE-REGRESSION: {sum(map(len, suites.values())) - failures} passed, {failures} failed")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
