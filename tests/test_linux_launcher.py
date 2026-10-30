"""Check backend selection without launching a GUI or changing desktop settings."""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import unittest


class LinuxLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qtnorth-launcher-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "scripts").mkdir()
        self.launcher = self.root / "scripts/run-linux.sh"
        shutil.copyfile(Path(__file__).resolve().parents[1] / "scripts/run-linux.sh", self.launcher)
        self.launcher.chmod(0o755)
        self.plugins = self.root / "build/runtime/plugins/platforms"
        self.plugins.mkdir(parents=True)
        (self.root / "build/runtime/lib").mkdir()
        (self.plugins / "libqwayland-generic.so").touch()
        executable = self.root / "build/qtNorth"
        executable.write_text(
            '#!/usr/bin/env python3\nimport json, os, sys\n'
            'print(json.dumps({"backend": os.environ.get("QT_QPA_PLATFORM"), "args": sys.argv[1:]}))\n'
        )
        executable.chmod(0o755)
        runtime = self.root / "run"
        runtime.mkdir()
        self.socket_path = runtime / "wayland-0"
        self.server = socket.socket(socket.AF_UNIX)
        self.addCleanup(self.server.close)
        self.server.bind(str(self.socket_path))
        self.env = {"PATH": os.defpath, "DISPLAY": ":88", "WAYLAND_DISPLAY": "wayland-0", "XDG_RUNTIME_DIR": str(runtime)}

    def run_launcher(self):
        result = subprocess.run([str(self.launcher), "--version"], env=self.env, text=True, capture_output=True, check=True)
        return json.loads(result.stdout)

    def test_available_wayland_preserves_arguments(self):
        self.assertEqual(self.run_launcher(), {"backend": "wayland", "args": ["--version"]})

    def test_explicit_backend_is_preserved(self):
        self.env["QT_QPA_PLATFORM"] = "offscreen"
        self.assertEqual(self.run_launcher()["backend"], "offscreen")

    def test_absolute_wayland_socket(self):
        self.env["WAYLAND_DISPLAY"] = str(self.socket_path)
        self.assertEqual(self.run_launcher()["backend"], "wayland")

    def test_missing_wayland_plugin_falls_back_to_x11(self):
        (self.plugins / "libqwayland-generic.so").unlink()
        self.assertEqual(self.run_launcher()["backend"], "xcb")

    def test_missing_socket_falls_back_to_x11(self):
        self.env["WAYLAND_DISPLAY"] = "not-a-socket"
        self.assertEqual(self.run_launcher()["backend"], "xcb")

    def test_no_display_does_not_force_a_backend(self):
        self.env.pop("WAYLAND_DISPLAY")
        self.env.pop("DISPLAY")
        self.assertIsNone(self.run_launcher()["backend"])


class PackagedLinuxLauncherTests(LinuxLauncherTests):
    def setUp(self):
        super().setUp()
        self.launcher = self.root / "qtNorth"
        shutil.copyfile(Path(__file__).resolve().parents[1] / "packaging/linux/qtNorth", self.launcher)
        self.launcher.chmod(0o755)
        shutil.move(self.root / "build/runtime/plugins", self.root / "plugins")
        self.plugins = self.root / "plugins/platforms"
        shutil.move(self.root / "build/runtime/lib", self.root / "lib")
        (self.root / "bin").mkdir()
        shutil.move(self.root / "build/qtNorth", self.root / "bin/qtNorth")

    def test_other_qt_platform_plugin_path_is_cleared(self):
        self.env["QT_QPA_PLATFORM_PLUGIN_PATH"] = "/a/different/qt"
        (self.root / "bin/qtNorth").write_text(
            '#!/usr/bin/env python3\nimport os, json\n'
            'print(json.dumps({"path": os.environ.get("QT_QPA_PLATFORM_PLUGIN_PATH")}))\n'
        )
        self.assertIsNone(self.run_launcher()["path"])


if __name__ == "__main__":
    unittest.main()
