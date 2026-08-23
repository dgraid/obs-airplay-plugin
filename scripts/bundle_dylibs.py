#!/usr/bin/env python3
"""Copy non-system dylibs next to a Mach-O and rewrite install names."""
import os
import re
import shutil
import subprocess
import sys

SYSTEM_PREFIXES = ("/usr/lib/", "/System/", "/Library/Apple/")

def otool_libs(path):
    out = subprocess.check_output(["otool", "-L", path], text=True)
    libs = []
    for line in out.splitlines()[1:]:
        line = line.strip()
        m = re.match(r"^(.+?)\s+\(", line)
        if m:
            libs.append(m.group(1))
    return libs

def collect(binary):
    seen = set()
    stack = [os.path.realpath(binary)]
    needed = []
    while stack:
        cur = stack.pop()
        for lib in otool_libs(cur):
            if lib.startswith("@") or lib.startswith(SYSTEM_PREFIXES):
                continue
            if not os.path.exists(lib):
                continue
            real = os.path.realpath(lib)
            if real in seen or real == os.path.realpath(binary):
                continue
            seen.add(real)
            needed.append(real)
            stack.append(real)
    return needed

def main():
    binary = sys.argv[1]
    fw = os.path.abspath(os.path.join(os.path.dirname(binary), "..", "Frameworks"))
    os.makedirs(fw, exist_ok=True)
    deps = collect(binary)
    mapping = {}
    for src in deps:
        dst = os.path.join(fw, os.path.basename(src))
        if os.path.exists(dst):
            os.chmod(dst, 0o755)
        shutil.copy2(src, dst)
        os.chmod(dst, 0o755)
        mapping[src] = dst
        subprocess.check_call(["install_name_tool", "-id", f"@loader_path/../Frameworks/{os.path.basename(src)}", dst])
    def rewrite(target):
        for lib in otool_libs(target):
            base = os.path.basename(lib)
            for src, dst in mapping.items():
                if os.path.basename(src) == base or os.path.realpath(lib) == os.path.realpath(src):
                    subprocess.check_call(
                        ["install_name_tool", "-change", lib, f"@loader_path/../Frameworks/{os.path.basename(src)}", target]
                    )
    rewrite(binary)
    # Drop Homebrew LC_RPATH leftovers from the link step.
    rpath_out = subprocess.check_output(["otool", "-l", binary], text=True)
    for line in rpath_out.splitlines():
        if "path " in line and ("/opt/homebrew" in line or "/usr/local" in line):
            path = line.split("path ", 1)[1].split(" ", 1)[0]
            subprocess.call(["install_name_tool", "-delete_rpath", path, binary])
    for dst in mapping.values():
        rewrite(dst)
        subprocess.check_call(["codesign", "--force", "--sign", "-", "--timestamp=none", dst])
    subprocess.check_call(["codesign", "--force", "--sign", "-", "--timestamp=none", binary])
    print(f"bundled {len(mapping)} dylibs into {fw}")

if __name__ == "__main__":
    main()
