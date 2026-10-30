#!/usr/bin/env python3
"""Build and verify a relocatable Linux desktop archive without installing it."""
import argparse
from functools import cache
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PLUGIN_GROUPS = {
    "platforms": ["libqxcb.so", "libqwayland-generic.so", "libqwayland-egl.so", "libqoffscreen.so", "libqminimal.so"],
    "imageformats": ["libqgif.so", "libqicns.so", "libqico.so", "libqjpeg.so", "libqsvg.so", "libqtga.so", "libqtiff.so", "libqwbmp.so", "libqwebp.so"],
    "iconengines": ["libqsvgicon.so"],
    "tls": ["libqcertonlybackend.so", "libqopensslbackend.so"],
    "platforminputcontexts": ["*.so"],
    "networkinformation": ["*.so"],
    "xcbglintegrations": ["*.so"],
    "wayland-decoration-client": ["*.so"],
    "wayland-shell-integration": ["*.so"],
    "wayland-graphics-integration-client": ["libqt-plugin-wayland-egl.so"],
}
# Keep the host's libc, display/graphics stack, OpenSSL, GLib and D-Bus.
# Bundle Qt and the additional ABI-specific libraries commonly absent without Qt.
BUNDLED = re.compile(r"^(libQt6|libqt6keychain|libicu|libb2\.|libdouble-conversion|libmd4c|"
                     r"libstdc\+\+|libgcc_s|libgomp|libxcb-(cursor|icccm|image|keysyms|render-util|util|xkb)|"
                     r"libxkbcommon-x11|libwebp|libsharpyuv|libtiff|libLerc|libjbig|libjpeg|libdeflate)")


def run(args, env=None, capture=False):
    if not capture:
        print("Running:", " ".join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), cwd=ROOT, env=env, check=True,
                          text=True, stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.PIPE if capture else None)


def dependencies(path, env):
    result = run(["ldd", path], env, capture=True)
    if "not found" in result.stdout:
        raise RuntimeError(f"Unresolved dependencies for {path}:\n{result.stdout}")
    return {match[1]: Path(match[2]) for match in
            re.finditer(r"^\s*(\S+) => (/.+) \(0x[0-9a-f]+\)", result.stdout, re.MULTILINE)}


def copy_file(source, target, executable=False):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(0o755 if executable else 0o644)


@cache
def package_owner(path):
    for candidate in (path, path.resolve()):
        result = subprocess.run(["dpkg-query", "-S", str(candidate)], text=True, capture_output=True)
        if result.returncode == 0:
            return result.stdout.split(": ", 1)[0].split(":", 1)[0]
    return None


def verify(stage, build, env):
    # Spaces in the extraction path and an unrelated working directory are deliberate.
    with tempfile.TemporaryDirectory(prefix="qtNorth extracted ") as directory:
        extracted = Path(directory) / "a folder with spaces"
        shutil.copytree(stage, extracted)
        copy_file(build / "linux_runtime_probe", extracted / "bin/runtime-probe", True)
        check_env = {**os.environ, "LD_LIBRARY_PATH": str(extracted / "lib"),
                     "QT_PLUGIN_PATH": str(extracted / "plugins"), "QT_QPA_PLATFORM": "offscreen"}
        check_env.pop("QT_QPA_PLATFORM_PLUGIN_PATH", None)
        for binary in [extracted / "bin/qtNorth", *extracted.glob("plugins/*/*.so")]:
            for name, library in dependencies(binary, check_env).items():
                if BUNDLED.match(name) and library.parent != extracted / "lib":
                    raise RuntimeError(f"Dependency escaped the archive: {name}: {library}")
        version = subprocess.run([str(extracted / "qtNorth"), "--version"], cwd="/",
                                 env=check_env, check=True, capture_output=True, text=True)
        print(version.stdout.strip(), flush=True)
        probe = run([extracted / "bin/runtime-probe"], check_env, capture=True)
        report = json.loads(probe.stdout)
        if Path(report["plugins"]) != extracted / "plugins":
            raise RuntimeError(f"Qt plugin search path escaped the archive: {report}")
        print("Extracted runtime verified:", json.dumps(report), flush=True)
        # A Wayland window exercises the real display backend, without account access.
        socket = os.environ.get("WAYLAND_DISPLAY", "")
        socket_path = Path(socket) if socket.startswith("/") else Path(os.environ.get("XDG_RUNTIME_DIR", "/")) / socket
        if socket and socket_path.is_socket():
            check_env.pop("QT_QPA_PLATFORM")
            # Use the actual launcher to exercise its selection, not just Qt directly.
            launch = subprocess.run([str(extracted / "qtNorth"), "--version"], cwd="/",
                                    env=check_env, check=True, capture_output=True, text=True)
            check_env["QT_QPA_PLATFORM"] = "wayland"
            report = json.loads(run([extracted / "bin/runtime-probe"], check_env, capture=True).stdout)
            if report["platform"] != "wayland":
                raise RuntimeError(f"Wayland verification failed: {report}")
            print("Wayland window, TLS and image plugins verified.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-linux-package")
    parser.add_argument("--runtime-dir", type=Path, default=ROOT / "build/runtime",
                        help="Qt runtime with lib/ and plugins/; otherwise use qtpaths6")
    parser.add_argument("--qt-prefix", type=Path, help="CMAKE_PREFIX_PATH for a non-system Qt")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--skip-tests", action="store_true", help="Reuse a build whose CTest suites have already passed")
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise RuntimeError("This package recipe currently targets Linux x86_64.")
    build = args.build_dir.resolve()
    runtime = args.runtime_dir.resolve()
    env = os.environ.copy()
    prefix = args.qt_prefix
    if prefix is None and (ROOT / "build/CMakeCache.txt").exists():
        match = re.search(r"^CMAKE_PREFIX_PATH:[^=]+=(.+)$", (ROOT / "build/CMakeCache.txt").read_text(), re.MULTILINE)
        if match and Path(match[1]).is_dir():
            prefix = Path(match[1])
    if (runtime / "lib").is_dir():
        libraries = runtime / "lib"
        plugins = runtime / "plugins"
        env["LD_LIBRARY_PATH"] = str(libraries) + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    else:
        libraries = Path(run(["qtpaths6", "--query", "QT_INSTALL_LIBS"], capture=True).stdout.strip())
        plugins = Path(run(["qtpaths6", "--query", "QT_INSTALL_PLUGINS"], capture=True).stdout.strip())
    env["QT_PLUGIN_PATH"] = str(plugins)
    configure = ["cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DBUILD_TESTING=ON", "-DCMAKE_SKIP_RPATH=ON"]
    if prefix:
        configure.append(f"-DCMAKE_PREFIX_PATH={prefix}")
    run(configure, env)
    run(["cmake", "--build", build, "--parallel", args.jobs], env)
    if not args.skip_tests:
        run(["ctest", "--test-dir", build, "--output-on-failure"], env)
    version = re.search(r"project\(qtNorth VERSION ([\d.]+)", (ROOT / "CMakeLists.txt").read_text())[1]
    name = f"qtNorth-{version}-linux-x86_64"
    output = ROOT / "out"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="qtnorth-linux-", dir=output) as work:
        stage = Path(work) / name
        stage.mkdir()
        copy_file(build / "qtNorth", stage / "bin/qtNorth", True)
        run(["strip", "--strip-unneeded", stage / "bin/qtNorth"])
        copy_file(ROOT / "packaging/linux/qtNorth", stage / "qtNorth", True)
        copy_file(ROOT / "packaging/linux/README.txt", stage / "README.txt")
        copy_file(ROOT / "README.md", stage / "README-source.md")
        (stage / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\n")
        for group, patterns in PLUGIN_GROUPS.items():
            for pattern in patterns:
                matches = list((plugins / group).glob(pattern))
                if "*" not in pattern and not matches:
                    raise RuntimeError(f"Missing plugin: {group}/{pattern}")
                for plugin in matches:
                    copy_file(plugin, stage / "plugins" / group / plugin.name, True)
        copied = {}
        required = {}
        for binary in [stage / "bin/qtNorth", *stage.glob("plugins/*/*.so")]:
            for soname, path in dependencies(binary, env).items():
                if BUNDLED.match(soname):
                    if soname not in copied:
                        copy_file(path, stage / "lib" / soname, True)
                        copied[soname] = path
                else:
                    required[soname] = package_owner(path)
        # Also require dynamically loaded HTTPS libraries, without freezing OS security updates.
        required.update({"libssl.so.3": "libssl3t64", "libcrypto.so.3": "libssl3t64"})
        licenses = stage / "licenses"
        licenses.mkdir()
        docs = [Path("/usr/share/doc")]
        if prefix:
            docs.insert(0, prefix / "share/doc")
            for notice in (prefix / "share/doc").glob("*/copyright"):
                copy_file(notice, licenses / (notice.parent.name + ".copyright"))
        for library in copied.values():
            owner = package_owner(library)
            if owner:
                for root in docs:
                    notice = root / owner / "copyright"
                    if notice.exists():
                        copy_file(notice, licenses / (owner + ".copyright"))
                        break
                else:
                    raise RuntimeError(f"Copyright notice missing for {library}")
        for license_name in ("LGPL-2.1", "LGPL-3", "GPL-2", "GPL-3", "Apache-2.0"):
            copy_file(Path("/usr/share/common-licenses") / license_name, licenses / license_name)
        (licenses / "README.txt").write_text(
            "These are unmodified shared libraries. Copyright notices include upstream source references.\n"
            "Qt deployment: https://doc.qt.io/qt-6/linux-deployment.html\n"
            "Ubuntu sources (see each component's copyright notice for its source):\n"
            "https://launchpad.net/ubuntu/+source/qt6-base\n"
            "https://launchpad.net/ubuntu/+source/qt6-svg\n"
            "https://launchpad.net/ubuntu/+source/qt6-wayland\n"
            "https://launchpad.net/ubuntu/+source/qt6-imageformats\n"
            "https://launchpad.net/ubuntu/+source/qtkeychain\n"
            "Libraries may be replaced with compatible builds. qtNorth source is in source/.\n")
        for directory in ("src", "assets", "scripts", "packaging", "tests", "docs"):
            shutil.copytree(ROOT / directory, stage / "source" / directory,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        copy_file(ROOT / "DISCLAIMER.txt", stage / "DISCLAIMER.txt")
        for filename in ("CMakeLists.txt", "README.md", "DISCLAIMER.txt", ".gitignore"):
            copy_file(ROOT / filename, stage / "source" / filename)
        maximum_glibc = (0, 0)
        for binary in [stage / "bin/qtNorth", *stage.glob("lib/*"), *stage.glob("plugins/*/*.so")]:
            versions = run(["readelf", "--version-info", binary], capture=True).stdout
            for major, minor in re.findall(r"GLIBC_(\d+)\.(\d+)", versions):
                maximum_glibc = max(maximum_glibc, (int(major), int(minor)))
        info = {"name": "qtNorth", "version": version, "buildType": "Release", "architecture": "x86_64",
                "testedOS": platform.freedesktop_os_release().get("PRETTY_NAME"),
                "minimumGlibcSymbols": ".".join(map(str, maximum_glibc)),
                "bundledLibraries": sorted(copied), "systemLibraries": dict(sorted(required.items()))}
        (stage / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
        verify(stage, build, env)
        archive = output / (name + ".tar.gz")
        epoch = int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))

        def attributes(entry):
            entry.uid = entry.gid = 0
            entry.uname = entry.gname = "root"
            entry.mode = 0o755 if entry.isdir() or (entry.mode & 0o111 and entry.name.endswith((".so", ".sh"))) else 0o644
            if entry.name in (name + "/qtNorth", name + "/bin/qtNorth"):
                entry.mode = 0o755
            entry.mtime = epoch
            return entry

        temporary_archive = Path(work) / archive.name
        with temporary_archive.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as zipped:
            with tarfile.open(fileobj=zipped, mode="w") as tar:
                tar.add(stage, arcname=name, filter=attributes)
        final_stage = output / name
        if final_stage.exists():
            shutil.rmtree(final_stage)
        shutil.move(stage, final_stage)
        temporary_archive.replace(archive)
        with archive.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        archive.with_suffix(archive.suffix + ".sha256").write_text(checksum + "  " + archive.name + "\n")
        print(f"Package ready: {archive} ({archive.stat().st_size / 1024**2:.1f} MiB)", flush=True)


if __name__ == "__main__":
    main()
