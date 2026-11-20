"""Offline tests for Rayman2Fix's noninvasive Wine latency comparison tool."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "audio_wine_ab.py"


class AudioWineAbTests(unittest.TestCase):
    def test_latency_environment_is_local(self):
        spec = importlib.util.spec_from_file_location("audio_wine_ab", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        baseline = {"WINEPREFIX": "/some/test-prefix"}
        self.assertEqual(module.audio_environment("baseline", baseline), baseline)
        with_latency = module.audio_environment("latency60", baseline)
        self.assertEqual(with_latency["PULSE_LATENCY_MSEC"], "60")
        self.assertEqual(with_latency["WINEPREFIX"], baseline["WINEPREFIX"])
        self.assertNotIn("PULSE_LATENCY_MSEC", baseline)

    def test_runner_does_not_modify_ini_or_first_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            launcher = root / "Rayman2Fix.exe"
            launcher.write_bytes(b"fake test executable")
            ini = root / "Rayman2Fix.ini"
            ini.write_text("[Debug]\nLog=0\n", encoding="utf-8")
            installer = root / "Rayman2Fix_installer.log"
            runtime = root / "Rayman2Fix.log"
            installer.write_bytes(b"keep first installer log\n")
            runtime.write_bytes(b"keep first runtime log\n")
            runner = root / "fake-wine"
            runner.write_text(
                "#!/bin/sh\nprintf '%s' \"$PULSE_LATENCY_MSEC\" > \"$1.test-env\"\nexit 0\n",
                encoding="utf-8",
            )
            runner.chmod(0o755)
            env = os.environ.copy()
            env.pop("PULSE_LATENCY_MSEC", None)
            for mode, expected in (("baseline", ""), ("latency60", "60")):
                cp = subprocess.run(
                    [sys.executable, str(SCRIPT), mode, "--no-pw-top",
                     "--launcher", str(launcher), "--wine", str(runner)],
                    capture_output=True, text=True, env=env,
                )
                self.assertEqual(cp.returncode, 0, cp.stderr)
                self.assertEqual((root / "Rayman2Fix.exe.test-env").read_text(), expected)
                self.assertEqual(ini.read_text(), "[Debug]\nLog=0\n")
                self.assertEqual(installer.read_bytes(), b"keep first installer log\n")
                self.assertEqual(runtime.read_bytes(), b"keep first runtime log\n")
                metas = list((root / "audio-diagnostics").glob(f"*-{mode}.json"))
                self.assertEqual(len(metas), 1)
                self.assertEqual(json.loads(metas[0].read_text())["mode"], mode)
                self.assertFalse(list((root / "audio-diagnostics").glob("*.pw-top.txt")))

    def test_missing_launcher_fails_without_side_effects(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cp = subprocess.run(
                [sys.executable, str(SCRIPT), "latency60", "--no-pw-top",
                 "--launcher", str(root / "missing.exe")],
                capture_output=True, text=True,
            )
            self.assertEqual(cp.returncode, 2)
            self.assertFalse((root / "audio-diagnostics").exists())


if __name__ == "__main__":
    unittest.main()
