"""Verify the pinned Glide format probe integrates without changing pixels."""
import pathlib
import tempfile
import unittest
from tools.gameframe.apply_render_probe import install, MODULE

class RenderProbePatchTests(unittest.TestCase):
    def fake_tree(self, root):
        base = pathlib.Path(root) / "wrappers/glide/src"
        (base / "api").mkdir(parents=True)
        (base / "lib.rs").write_text("#[cfg(windows)]\nmod fps_limit;\n")
        (base / "api/sst.rs").write_text("    let (ow, oh) = crate::config::get().output(w, h);\n" * 2)
        (base / "api/lfb.rs").write_text(
            '    shim::call!("grLfbWriteMode", mode);\n'
            '    shim::call!("grLfbWriteColorFormat", format);\n'
            '    shim::call!("grLfbGetWritePtr", buffer);\n'
            "    if let Some(img) = img {\n        // The pixels\n"
            '    shim::call!("guFbWriteRegion", x, y, w, h, crate::callargs::Region(src, w, h, stride, with(|l| if wide(l.mode) { 4 } else { 2 })), stride);\n    finish_write();\n')
        (base / "api/tex.rs").write_text(
            "    let Some(i) = (unsafe { info.as_ref() }) else { return };\n"
            "    if tmu != 0 || data.is_null() { return; }\n")
        return base

    def test_probe_is_bounded_and_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = self.fake_tree(tmp)
            install(pathlib.Path(tmp))
            self.assertIn("mod render_probe;", (src / "lib.rs").read_text())
            self.assertIn("render_probe::opened", (src / "api/sst.rs").read_text())
            self.assertIn("render_probe::region", (src / "api/lfb.rs").read_text())
            self.assertIn("render_probe::texture", (src / "api/tex.rs").read_text())
            self.assertIn("RAYMAN2FIX_GLIDE_PROBE", MODULE)
            self.assertIn('parent.join("Rayman2Fix.log")', MODULE)
            self.assertIn("WRITES.fetch_add", MODULE)
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))

    def test_unknown_source_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = self.fake_tree(tmp)
            (base / "api/lfb.rs").write_text("changed upstream\n")
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))
            self.assertFalse((base / "render_probe.rs").exists())

if __name__ == "__main__":
    unittest.main()
