"""Tests for the scoped GameFrame LFB loading art correction."""
import pathlib
import tempfile
import unittest
from tools.gameframe.apply_loading_image_fix import install, MODULE

class LoadingImageFixTests(unittest.TestCase):
    def fixtures(self, root):
        base = pathlib.Path(root)/"wrappers/glide/src"
        (base/"api").mkdir(parents=True)
        (base/"render").mkdir(parents=True)
        (base/"lib.rs").write_text("#[cfg(windows)]\nmod render_probe;\n")
        (base/"cmd.rs").write_text("    pub rgba: Vec<u8>,\n    /// Through the pixel pipeline\n")
        (base/"api/lfb.rs").write_text(
            "    let alpha = if l.bypass { 0xff } else { l.constant_alpha };\n"
            "    let mut rgba_out = Vec::with_capacity((x1 - x0) * (y1 - y0) * 4);\n"
            "                    let mut c = rgba(l.mode, l.format, v, alpha);\n"
            "    Some(LfbImage { x: x0 as u32, y: y0 as u32, w: (x1 - x0) as u32, h: (y1 - y0) as u32, rgba: rgba_out, pipeline: !l.bypass })\n")
        (base/"render/mod.rs").write_text(
            "            let (x, y) = (img.x as f32, img.y as f32);\n"
            '            (gl.Uniform4f)(p.loc("uRect"), x, y, x + img.w as f32, y + img.h as f32);\n'
            "            (gl.ColorMask)(1, 1, 1, 1);\n"
            "            (gl.DrawArrays)(0x0005, 0, 4);\n")
        return base

    def test_strict_scoping_color_order_and_centered_canvas(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = self.fixtures(tmp)
            install(pathlib.Path(tmp))
            self.assertIn("mod loading_image_fix;",(base/"lib.rs").read_text())
            lfb=(base/"api/lfb.rs").read_text()
            self.assertIn("if loading_art",lfb)
            self.assertIn("rgba_argb(l.mode, v, alpha)",lfb)
            self.assertIn("four_three_loading: loading_art",lfb)
            self.assertIn("!l.bypass",lfb)
            renderer=(base/"render/mod.rs").read_text()
            self.assertIn("loading_image_fix::rect",renderer)
            self.assertIn("if img.four_three_loading",renderer)
            self.assertIn("Clear)(COLOR_BUFFER_BIT)",renderer)
            self.assertIn("format == COLORFORMAT_ABGR",MODULE)
            self.assertIn("mode == LFB_565",MODULE)
            self.assertIn("x1 == w && y1 == h",MODULE)
            self.assertIn("w == 1024 && h == 768",MODULE)
            # 1920x1080: 4:3 canvas is 1440 px wide, 240 px margins.
            factor=(1024/768)/(1920/1080)
            self.assertAlmostEqual(factor, 0.75)
            self.assertEqual(1920*(1-factor)/2,240)
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))

    def test_upstream_drift_aborts_without_writing(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=self.fixtures(tmp)
            (base/"render/mod.rs").write_text("upstream changed\n")
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))
            self.assertNotIn("loading_image_fix",(base/"lib.rs").read_text())

if __name__=="__main__":
    unittest.main()
