# SPDX-License-Identifier: GPL-3.0-only
"""Where the pi32v2-llvm toolchain and the AC79 SDK files are, and how build.py runs a tool (BUILDING.md).

The compiler is Julius Vitkauskas's pi32v2-llvm (LLVM with a pi32v2 target) as his drop-in: a directory with the
layout of the old vendor toolchain (pi32v2/bin/clang, ld, common/bin/objdump, ...) whose tools are native binaries
of the build host plus a Python 3.11 wrapper. It runs natively, never in a container or WSL:

  PI32V2_LLVM_TOOLCHAIN=<dir>   that drop-in
  otherwise                     <toolchains>/pi32v2-llvm-dropin-<version> (tools/repo_root.py: OPTIMIST_TOOLCHAINS, the
                                main checkout's build/toolchains, ...; `optimist.py setup` downloads it there)

Hosts with a drop-in: Linux x86-64 and macOS arm64 (LLVM_ASSETS). Others build one from the pinned commits
(BUILDING.md, "The compiler").

The SDK files: AC79_SDK, else <repo>/sdk (git-ignored; where setup fetches them; in a git worktree: the main
checkout's sdk/, tools/shared.py), else ~/fw-AC79_AIoT_SDK (legacy, read-only fallback), else build/deps/ac79
(SLOOP's Windows layout).
"""
import importlib.util
import os
import platform
import subprocess
from dataclasses import dataclass
from pathlib import Path

import repo_root
import shared

ROOT = Path(__file__).resolve().parents[1]
# pi32v2-llvm 4f631b31b, pi32v2-isa e575321, built from pinned commits (BUILDING.md "The compiler") and kept as release
# assets of this repository (Apache-2.0 with LLVM exceptions, MIT-0: the licences inside each archive)
LLVM_VERSION = "4f631b31b-e575321"
LLVM_RELEASE_URL = f"https://github.com/w0ts/optimist/releases/download/toolchain-pi32v2-llvm-{LLVM_VERSION}"
# (system, machine) -> (asset, SHA-256, size in MB); the archive's one top directory is pi32v2-llvm-dropin-<version>
LLVM_ASSETS = {
    ("linux", "x86_64"): (f"pi32v2-llvm-dropin-{LLVM_VERSION}.tar.xz",
                          "72519811be85041a3b1ba4ec4b71068897903955d73a2d1beb77559e3a6d8f12", 72),
    ("darwin", "arm64"): (f"pi32v2-llvm-dropin-{LLVM_VERSION}-macos-arm64.tar.xz",
                          "8ea37c365a8443c3ce7bd3bd2e232e3ee9f42ed03e326e50d97ff41310e95cd5", 38),
}
SDK_TAG = "AC79NN_SDK_V1.2.1_2023-12-13"
SDK_URL = f"https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/raw/{SDK_TAG}/cpu/wl82/tools"
# the SDK files the package uses, as AC79NN_SDK_V1.2.1 has them (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}
# a shell sets the core limit: build.py runs tools in threads, where preexec_fn is unsafe; a crashed tool
# must not leave a core file in the source tree
NO_CORE = 'ulimit -c 0 && exec "$0" "$@"'


class ToolchainError(Exception):
    pass


def host_key():
    """(system, machine) of this host as LLVM_ASSETS spells it: ("linux", "x86_64"), ("darwin", "arm64"), ..."""
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
    return platform.system().lower(), machine


def llvm_asset(key=None):
    """-> (asset name, SHA-256, MB) of the drop-in for this host, or None when there is none"""
    return LLVM_ASSETS.get(key or host_key())


def has_clang(path):
    return path is not None and (Path(path) / "pi32v2" / "bin" / "clang").is_file()


def _quiet(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return None


def llvm_dir(env=None, root=None):
    """the drop-in's directory: PI32V2_LLVM_TOOLCHAIN, else <toolchains>/pi32v2-llvm-dropin-<version>"""
    env = os.environ if env is None else env
    if env.get("PI32V2_LLVM_TOOLCHAIN"):
        return Path(env["PI32V2_LLVM_TOOLCHAIN"])
    return repo_root.toolchains_dir(root, env) / f"pi32v2-llvm-dropin-{LLVM_VERSION}"


def runs_here(d):
    """the drop-in's own tools run on this host: its wrapper (python3, 3.11 or newer) starts the real clang"""
    p = _quiet([str(Path(d) / "pi32v2" / "bin" / "clang"), "--version"])
    return p is not None and p.returncode == 0


@dataclass(frozen=True)
class Backend:
    path: str                       # the drop-in's directory

    def describe(self):
        return f"pi32v2-llvm {LLVM_VERSION}, native, {self.path}"

    def command(self, tool, args, src=ROOT):
        """the command line that runs the drop-in's binary `tool` (pi32v2/bin/clang...) with cwd src; args are
        relative POSIX paths and flags"""
        return ["sh", "-c", NO_CORE, str(Path(self.path) / tool), *args]


def _missing_hint(d):
    if llvm_asset() is None:
        system, machine = host_key()
        return (f"there is no pi32v2-llvm drop-in for {system} {machine}: build one from the pinned commits "
                "(BUILDING.md, 'The compiler') and point PI32V2_LLVM_TOOLCHAIN at it")
    return f"no pi32v2-llvm in {d}: python tools/optimist.py setup downloads it"


def resolve(env=None):
    """-> the Backend to build with; ToolchainError says what is missing and how to get it"""
    env = os.environ if env is None else env
    d = llvm_dir(env)
    if not has_clang(d):
        raise ToolchainError(_missing_hint(d))
    if not runs_here(d):
        raise ToolchainError(f"the pi32v2-llvm in {d} does not run on this host ({'-'.join(host_key())}): it is for "
                             "another system, or python3 is older than 3.11 (its wrapper needs 3.11)")
    return Backend(str(d.resolve()))


# ---- the SDK files

def sdk_has_files(d):
    return (Path(d) / "cpu" / "wl82" / "tools" / "uboot.boot").is_file()


def default_sdk_dir(root=None):
    """where the SDK files are fetched to: ./sdk, in a linked git worktree the main checkout's sdk/"""
    return shared.resolve("sdk", sdk_has_files, root or ROOT)[0]


def sdk_dir(env=None, root=None):
    env = os.environ if env is None else env
    root = Path(root or ROOT)
    if env.get("AC79_SDK"):
        return Path(env["AC79_SDK"]).expanduser()
    own, is_shared = shared.resolve("sdk", sdk_has_files, root)
    if sdk_has_files(own):
        if is_shared:
            shared.note("sdk", own)
        return own
    for d in (Path.home() / "fw-AC79_AIoT_SDK", root / "build" / "deps" / "ac79"):
        if sdk_has_files(d):
            return d
    return own


def sdk_missing(root):
    """-> the SDK files missing under root (cpu/wl82/tools/...)"""
    return [rel for rel in SDK_SHA256 if not (Path(root) / "cpu" / "wl82" / "tools" / rel).is_file()]


def have_module(name):
    """name importable (a dotted name: its package too; "PIL.Image", as an empty PIL folder a removed Pillow leaves
    behind imports as a namespace package)"""
    try:
        return importlib.util.find_spec(name) is not None
    except ModuleNotFoundError:
        return False


def preflight(env=None):
    """the checks build.py runs before it starts -> (Backend, SDK dir); SystemExit with what to do"""
    env = os.environ if env is None else env
    if not have_module("PIL.Image"):
        raise SystemExit("build: this Python has no Pillow: python tools/optimist.py setup "
                         "(or pip install -r tools/requirements.txt)")
    try:
        backend = resolve(env)
    except ToolchainError as e:
        raise SystemExit(f"build: {e}")
    sdk = sdk_dir(env)
    if sdk_missing(sdk):
        raise SystemExit(f"build: no JieLi AC79 SDK files in {sdk} (python tools/optimist.py setup fetches them)")
    return backend, sdk
