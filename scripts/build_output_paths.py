#!/usr/bin/env python3
"""Build-wrapper paths; direct developer CLI/GUI calls are outside this guard.

The shared resolver owns mm4 disk identity/reserve policy. Validating an explicit
output uses it as a validation root only; no hierarchy is appended to that output.
No directories are created until every caller override has been checked.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import os
from pathlib import Path
import platform
import re
import runpy
import shlex

def account_home():
    try:
        import pwd
        return Path(pwd.getpwuid(os.getuid()).pw_dir)
    except ImportError:
        return Path.home()


PROJECT = "floatingball"
REPO = Path(__file__).resolve().parent.parent
PURPOSES = ("work", "cache", "tests", "logs", "dist", "evidence", "releases")


class BuildPaths:
    def __init__(self, task="dev", checkout=None, workspace=None, target=None):
        self.checkout = Path(checkout or REPO).resolve(strict=True)
        target = target or ("macos" if sys.platform == "darwin" else platform.system().lower()) + "-" + platform.machine()
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", task) or task in (".", ".."):
            raise ValueError("Task must be one safe directory component")
        self.mm4 = platform.node().split(".")[0].lower() == "mm4"
        canonical = account_home() / "mygit/tools/build-paths.py"
        candidates = [canonical] if self.mm4 else []
        if not self.mm4:
            if os.environ.get("MYGIT_BUILD_PATHS_RESOLVER"):
                candidates.append(Path(os.environ["MYGIT_BUILD_PATHS_RESOLVER"]))
            if workspace:
                candidates.append(Path(workspace) / "tools/build-paths.py")
            candidates.extend(parent / "tools/build-paths.py" for parent in self.checkout.parents)
            candidates.append(canonical)
        resolver = next((p for p in candidates if p.is_file()), None)
        self.shared = runpy.run_path(str(resolver)) if resolver else None
        self.args = argparse.Namespace(project=PROJECT, checkout=self.checkout, task=task,
                                       platform=target, build_root=None)
        if self.shared:
            if self.mm4 and (not all(callable(self.shared.get(k)) for k in ('host_policy', 'workspace_root', 'verify_external_root')) or not self.shared['host_policy'](self.shared['workspace_root']()).get('require_external_build_root')):
                raise ValueError('mm4 shared resolver lacks required external-volume policy; no fallback')
            self.paths = self.shared["paths"](self.args)
        elif self.mm4:
            raise ValueError("mm4 shared build resolver is missing; no internal fallback")
        else:
            # Portable checkout/CI fallback. mm4 can never reach this branch.
            import subprocess
            key = subprocess.check_output(["cksum"], input=str(self.checkout), text=True).split()[0]
            base = Path(os.environ.get("MYGIT_BUILD_ROOT", str(Path(workspace or account_home() / "mygit") / "build")))
            root = base / PROJECT
            target_dir = root / (self.checkout.name + "-" + key) / task / target
            self.paths = {p: target_dir / p for p in PURPOSES}
            self.paths.update(root=root, platform=target_dir)
        self.env = {"MYGIT_" + k.upper() + "_DIR": str(v) for k, v in self.paths.items()}
        self.env["MYGIT_BUILD_ROOT"] = str(self.paths["root"].parent)
        self.env["MYGIT_MM4_BUILD_GUARD"] = "1" if self.mm4 else "0"
        cache, work = self.paths["cache"], self.paths["work"]
        tmp = work / "tmp"
        self.env.update(TMPDIR=str(tmp), TMP=str(tmp), TEMP=str(tmp),
                        PYTHONPYCACHEPREFIX=str(cache / "pycache"),
                        XDG_CACHE_HOME=str(cache / "xdg"),
                        CLANG_MODULE_CACHE_PATH=str(cache / "clang"),
                        SWIFTPM_MODULECACHE_OVERRIDE=str(cache / "swift-modules"),
                        SWIFT_MODULECACHE_PATH=str(cache / "swift-modules"),
                        CLANG_CACHE_DIR=str(cache / "clang-cache"),
                        CCACHE_DIR=str(cache / "ccache"),
                        MAGICK_TEMPORARY_PATH=str(tmp),
                        XCODEGEN_CACHE_PATH=str(cache / "xcodegen/project.cache"))

    def validate(self, value, app=False):
        path = Path(value)
        if not path.is_absolute() or ".." in path.parts:
            raise ValueError("Output must be an absolute path without parent traversal: " + str(path))
        for parent in (path, *path.parents):
            if parent.is_symlink() or (parent / '.git').exists() or (parent / '.git').is_symlink() or ((parent / 'HEAD').is_file() and (parent / 'objects').is_dir()):
                raise ValueError("Output crosses a symlink or source repository: " + str(path))
        if path == self.checkout or path.is_relative_to(self.checkout):
            raise ValueError("Output must be outside the source checkout: " + str(path))
        if app and not any(p.endswith(".noindex") for p in path.parts):
            raise ValueError("Application output requires a .noindex ancestor: " + str(path))
        if self.shared:
            workspace = self.shared.get('workspace_root', lambda: account_home() / 'mygit')()
            policy = self.shared.get('host_policy', lambda _: {})(workspace)
            # The per-tool path is already a leaf beneath the resolved layout.
            # Validate its volume directly, rather than treating it as a NEW
            # shared root (which rejects valid other-host mygit/build children).
            verifier = self.shared.get('verify_external_root')
            if verifier is not None: verifier(path, policy)
            elif self.mm4: raise ValueError('mm4 shared external verifier is missing; no fallback')
            if path.is_relative_to(workspace) and not path.is_relative_to(workspace / 'build'):
                raise ValueError('Output within mygit must use its shared build tree: ' + str(path))
        if path in (Path('/'), Path.home()) or path.is_relative_to(Path('/nix/store')) or '.git' in path.parts:
            raise ValueError('Protected output path: ' + str(path))
        return path

    def prepare(self, *outputs):
        # Complete preflight before mkdir or the caller's tool/temp/cache use.
        directories = [Path(self.env[k]) for k in ("TMPDIR", "PYTHONPYCACHEPREFIX", "XDG_CACHE_HOME",
                       "CLANG_MODULE_CACHE_PATH", "SWIFTPM_MODULECACHE_OVERRIDE", "CLANG_CACHE_DIR", "CCACHE_DIR")]
        directories.append(Path(self.env['XCODEGEN_CACHE_PATH']).parent)
        tool_paths = [self.paths['cache'] / p for p in ('SourcePackages', 'compilation-cas',
                      'swiftpm/cache', 'swiftpm/config', 'swiftpm/security')]
        for path in [*directories, *tool_paths, Path(self.env["XCODEGEN_CACHE_PATH"]), *outputs]:
            self.validate(path)
        for path in directories:
            path.mkdir(parents=True, exist_ok=True)
        os.environ.update(self.env)
        sys.pycache_prefix = self.env["PYTHONPYCACHEPREFIX"]
        import tempfile
        tempfile.tempdir = self.env["TMPDIR"]
        return self

    def swift_flags(self):
        cache = self.paths["cache"]
        return ["--cache-path", str(cache / "swiftpm/cache"), "--config-path", str(cache / "swiftpm/config"),
                "--security-path", str(cache / "swiftpm/security")]

    def xcode_flags(self):
        cache = self.paths["cache"]
        return ["-clonedSourcePackagesDirPath", str(cache / "SourcePackages"),
                "-packageCachePath", str(cache / "swiftpm/cache"),
                "CLANG_MODULE_CACHE_PATH=" + self.env["CLANG_MODULE_CACHE_PATH"],
                "SWIFT_MODULECACHE_PATH=" + self.env["SWIFT_MODULECACHE_PATH"],
                "COMPILATION_CACHE_CAS_PATH=" + str(cache / "compilation-cas")]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolve-only", action="store_true")
    parser.add_argument("--task", default="dev")
    parser.add_argument("--platform")
    parser.add_argument("--validate", action="append", default=[])
    parser.add_argument("--app-output", action="append", default=[])
    args = parser.parse_args()
    try:
        paths = BuildPaths(args.task, target=args.platform)
        for value in args.validate: paths.validate(value)
        for value in args.app_output: paths.validate(value, app=True)
        if not args.resolve_only: paths.prepare()
        for key, value in paths.env.items(): print("export " + key + "=" + shlex.quote(value))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, str(error) + "\n")
