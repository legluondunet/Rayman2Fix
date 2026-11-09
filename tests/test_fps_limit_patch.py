"""No game binaries: verify our pinned GameFrame source integration fails closed."""
import pathlib
import tempfile
import unittest
from tools.gameframe.apply_fps_limit import install, MODULE

class FrameLimiterPatchTests(unittest.TestCase):
    def test_pinned_layout_and_producer_pacing(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "wrappers" / "glide" / "src"
            (src / "api").mkdir(parents=True)
            (src / "lib.rs").write_text("#[cfg(windows)]\nmod config;\n")
            (src / "api" / "sst.rs").write_text(
                "    push(Cmd::Swap);\n    flush();\n    super::movie::frame();\n")
            install(pathlib.Path(tmp))
            self.assertIn("mod fps_limit;", (src / "lib.rs").read_text())
            swap = (src / "api" / "sst.rs").read_text()
            self.assertTrue(swap.index("flush();") <
                            swap.index("crate::fps_limit::pace();") <
                            swap.index("super::movie::frame();"))
            self.assertIn('std::env::var("RAYMAN2FIX_FPS_LIMIT")', MODULE)
            self.assertIn("std::thread::sleep", MODULE)
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))

    def test_unknown_upstream_layout_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / "wrappers" / "glide" / "src"
            (src / "api").mkdir(parents=True)
            (src / "lib.rs").write_text("mod config;\n")
            (src / "api" / "sst.rs").write_text("fn nope() {}\n")
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))

if __name__ == "__main__":
    unittest.main()
