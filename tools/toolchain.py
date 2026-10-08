# SPDX-License-Identifier: GPL-3.0-only
"""Where the JieLi toolchain and the AC79 SDK files are, and how build.py runs a tool (BUILDING.md).

The toolchain is a Linux x86-64 binary set. Four ways to run it, picked in this order:

  JIELI_BACKEND=native|docker|image|wsl   forces one
  JIELI_TOOLCHAIN=<dir>                   that copy: natively on Linux x86-64 (JIELI_DOCKER=1: in Docker), else
                                          mounted into a linux/amd64 container (JIELI_DOCKER_IMAGE, debian)
  Linux x86-64                            ~/.jieli/toolchain, natively
  elsewhere                               the image tools/docker/Dockerfile builds (optimist-toolchain:<version>,
                                          JIELI_TOOLCHAIN_IMAGE), else ~/.jieli/toolchain-docker or
                                          ~/.jieli/toolchain mounted, else (Windows) WSL: JIELI_WSL_DISTRO,
                                          JIELI_WSL_TOOLCHAIN (default $HOME/.jieli/toolchain inside WSL)

The SDK files: AC79_SDK, else <repo>/sdk (git-ignored; where setup fetches them; in a git worktree: the main
checkout's sdk/, tools/shared.py), else ~/fw-AC79_AIoT_SDK (legacy, read-only fallback), else build/deps/ac79
(SLOOP's Windows layout).
"""
import importlib.util
import os
import platform
import shutil
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

import shared

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_VERSION = "20250324.1"
TOOLCHAIN_URL = f"https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-{TOOLCHAIN_VERSION}.tar.xz"
TOOLCHAIN_SHA256 = "f686586bcfb45e0f0bb27fd2b39c7a7f313cb4f0e88a66a14da621ffa8225958"
IMAGE_DEFAULT = f"optimist-toolchain:{TOOLCHAIN_VERSION}"
DOCKERFILE_DIR = ROOT / "tools" / "docker"
SDK_TAG = "AC79NN_SDK_V1.2.1_2023-12-13"
SDK_URL = f"https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/raw/{SDK_TAG}/cpu/wl82/tools"
# the SDK files the package uses, as AC79NN_SDK_V1.2.1 has them (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}
JIELI_HOME = Path.home() / ".jieli"
BACKENDS = ("native", "docker", "image", "wsl")
# a shell sets the core limit: build.py runs tools in threads, where preexec_fn is unsafe; a crashed tool
# must not leave a core file in the source tree
NO_CORE = 'ulimit -c 0 && exec "$0" "$@"'
# WSL: $0 is the toolchain directory; a leading ~/ or $HOME/ becomes the WSL user's home, nothing else is
# expanded -> $t
WSL_HOME = r't=$0; case $t in "~/"*) t="$HOME/${t#\~/}";; "\$HOME/"*) t="$HOME/${t#\$HOME/}";; esac; '


class ToolchainError(Exception):
    pass


def is_native_host():
    return platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")


def has_clang(path):
    return path is not None and (Path(path) / "pi32v2" / "bin" / "clang").is_file()


def image_name(env=None):
    return (env or os.environ).get("JIELI_TOOLCHAIN_IMAGE") or IMAGE_DEFAULT


def _quiet(cmd, timeout=60):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return None


_DOCKER = {}


def docker_running():
    """the Docker daemon answers (asked once per process: a hung daemon costs the timeout once)"""
    if "up" not in _DOCKER:
        p = _quiet(["docker", "info", "--format", "{{.ServerVersion}}"]) if shutil.which("docker") else None
        _DOCKER["up"] = p is not None and p.returncode == 0
    return _DOCKER["up"]


def image_present(image):
    p = _quiet(["docker", "image", "inspect", "--format", "{{.Id}}", image]) if shutil.which("docker") else None
    return p is not None and p.returncode == 0


def wsl_toolchain(env):
    """Windows: (distro, toolchain dir inside WSL) when a WSL distribution has the toolchain, else None"""
    if platform.system() != "Windows" or not shutil.which("wsl"):
        return None
    distro = env.get("JIELI_WSL_DISTRO", "")
    tc = env.get("JIELI_WSL_TOOLCHAIN", "$HOME/.jieli/toolchain")
    probe = ["wsl", *(["-d", distro] if distro else []), "--exec", "sh", "-c",
             WSL_HOME + 'test -x "$t/pi32v2/bin/clang"', tc]
    p = _quiet(probe)
    return (distro, tc) if p is not None and p.returncode == 0 else None


def linux_user():
    """--user for docker run on a Linux host (root-owned build files otherwise); Docker Desktop maps it itself"""
    if platform.system() == "Linux" and hasattr(os, "getuid"):
        return ["--user", f"{os.getuid()}:{os.getgid()}"]
    return []


@dataclass(frozen=True)
class Backend:
    kind: str                       # native | docker | image | wsl
    path: str = ""                  # the toolchain: a host directory (native, docker), a WSL path (wsl)
    image: str = ""                 # docker: the base image; image: the toolchain image
    distro: str = ""                # wsl: the distribution ("" = the default one)

    def describe(self):
        return {"native": f"native, {self.path}",
                "docker": f"Docker ({self.image}), {self.path} mounted",
                "image": f"Docker image {self.image}",
                "wsl": f"WSL {self.distro or '(default distribution)'}, {self.path}"}[self.kind]

    def command(self, tool, args, src=ROOT):
        """the command line that runs toolchain binary `tool` (pi32v2/bin/clang...) with cwd src; args are
        relative POSIX paths and flags"""
        if self.kind == "native":
            return ["sh", "-c", NO_CORE, str(Path(self.path) / tool), *args]
        if self.kind in ("docker", "image"):
            mount = ["-v", f"{self.path}:/opt/jieli:ro"] if self.kind == "docker" else []
            return ["docker", "run", "--rm", "--platform", "linux/amd64", "--ulimit", "core=0", *linux_user(),
                    "-v", f"{src}:/work", *mount, "-w", "/work", self.image, f"/opt/jieli/{tool}", *args]
        return ["wsl", *(["-d", self.distro] if self.distro else []), "--cd", wsl_path(src, self.distro), "--exec",
                "sh", "-c", WSL_HOME + 'ulimit -c 0 && tool=$1 && shift && exec "$t/$tool" "$@"', self.path, tool,
                *args]


TOOL_TRIES = 4
# what a tool says when the container's view of the mounted tree lags the host's (seen with Rancher Desktop on
# macOS, 2026-10-06: an output folder made a moment before is "not there") or the emulated x86-64 binary crashes
FLAKY = ("No such file or directory", "core dumped", "Segmentation fault", "Bus error")


def retry_tool(backend, returncode, output, attempt):
    """-> True to run a failed tool again: only in a container (--in-docker too; a native failure is real), for the
    failures above, and not after the last try. Waits a little first (the mount settles)"""
    in_container = backend.kind in ("docker", "image") or os.environ.get("OPTIMIST_IN_CONTAINER") == "1"
    if returncode == 0 or not in_container or attempt + 1 >= TOOL_TRIES:
        return False
    if not any(f in output for f in FLAKY):
        return False
    time.sleep(0.5 + attempt)
    return True


_WSL_PATHS = {}


def wsl_path(p, distro):
    key = (str(p), distro)
    if key not in _WSL_PATHS:
        cmd = ["wsl", *(["-d", distro] if distro else []), "--exec", "wslpath", "-a", Path(p).resolve().as_posix()]
        _WSL_PATHS[key] = subprocess.check_output(cmd, text=True).strip()
    return _WSL_PATHS[key]


def _from_dir(path, env):
    if env.get("JIELI_BACKEND") == "native" or (is_native_host() and env.get("JIELI_DOCKER", "0") != "1"):
        return Backend("native", str(Path(path).resolve()))
    return Backend("docker", str(Path(path).resolve()), env.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim"))


def resolve(env=None):
    """-> the Backend to build with; ToolchainError says what is missing and how to get it"""
    env = os.environ if env is None else env
    forced = env.get("JIELI_BACKEND", "")
    if forced and forced not in BACKENDS:
        raise ToolchainError(f"JIELI_BACKEND={forced}: use one of {', '.join(BACKENDS)}")
    if forced == "native" and not is_native_host():
        raise ToolchainError("the native toolchain runs on Linux x86-64 only (use docker, image or wsl)")
    explicit = env.get("JIELI_TOOLCHAIN")
    if explicit and forced not in ("image", "wsl"):
        if not has_clang(explicit):
            raise ToolchainError(f"JIELI_TOOLCHAIN={explicit} has no pi32v2/bin/clang (the directory with pi32v2/ "
                                 "and common/; python tools/optimist.py setup downloads it)")
        b = _from_dir(explicit, env)
        if forced == "docker" and b.kind == "native":
            b = Backend("docker", b.path, env.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim"))
        return _checked(b)
    if forced in ("", "native") and is_native_host() and env.get("JIELI_DOCKER", "0") != "1":
        if has_clang(JIELI_HOME / "toolchain"):
            return Backend("native", str((JIELI_HOME / "toolchain").resolve()))
        if forced == "native":
            raise ToolchainError("no toolchain in ~/.jieli/toolchain: python tools/optimist.py setup")
    if forced in ("", "image") and docker_running() and image_present(image_name(env)):
        return Backend("image", image=image_name(env))
    if forced in ("", "docker"):
        for d in (JIELI_HOME / "toolchain-docker", JIELI_HOME / "toolchain"):
            if has_clang(d) and docker_running():
                return Backend("docker", str(d.resolve()), env.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim"))
    if forced in ("", "wsl"):
        w = wsl_toolchain(env)
        if w:
            return Backend("wsl", w[1], distro=w[0])
    raise ToolchainError(_missing_hint(forced, env))


def _checked(b):
    if b.kind == "docker" and not docker_running():
        raise ToolchainError("Docker is not running (the toolchain runs in a linux/amd64 container on this host)")
    return b


def _missing_hint(forced, env):
    if forced == "image":
        return (f"no Docker image {image_name(env)} (or Docker is not running): "
                "python tools/optimist.py setup builds it")
    if forced == "wsl":
        return "no WSL distribution with the toolchain (JIELI_WSL_DISTRO, JIELI_WSL_TOOLCHAIN; BUILDING.md)"
    if is_native_host():
        return "no JieLi toolchain: python tools/optimist.py setup (downloads it to ~/.jieli/toolchain)"
    if not docker_running():
        return ("the JieLi toolchain is Linux x86-64 only and Docker is not running here: start Docker Desktop "
                "(or Rancher Desktop), then python tools/optimist.py setup" +
                (" (or set up WSL: BUILDING.md, Windows)" if platform.system() == "Windows" else ""))
    return f"no toolchain image {image_name(env)}: python tools/optimist.py setup builds it"


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
    return importlib.util.find_spec(name) is not None


def preflight(env=None):
    """the checks build.py runs before it starts -> (Backend, SDK dir); SystemExit with what to do"""
    env = os.environ if env is None else env
    if not have_module("PIL"):
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
