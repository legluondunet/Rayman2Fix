"""Opt-in GameFrame logo texture capture: strict source patch and no pixel edits."""
import pathlib
import tempfile
import unittest
from tools.gameframe.apply_intro_logo_capture import install, MODULE

class LogoCapturePatchTests(unittest.TestCase):
    def fake_tree(self,root):
        base=pathlib.Path(root)/"wrappers/glide/src"
        (base/"render").mkdir(parents=True)
        (base/"lib.rs").write_text(
            "#[cfg(windows)]\nmod loading_image_fix;\n",encoding="utf-8")
        (base/"render/textures.rs").write_text(
            "                    Ok(img) => unsafe {\n"
            "                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n"
            "                    },\n",encoding="utf-8")
        return base

    def test_idempotence_and_pixel_passthrough(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=self.fake_tree(tmp)
            before=(base/"render/textures.rs").read_text()
            install(pathlib.Path(tmp))
            after=(base/"render/textures.rs").read_text()
            self.assertIn("crate::intro_logo_capture::inspect(lw,lh,i.format,&img.pixels)",after)
            self.assertEqual(after.replace(
                "                        if k == 0 { crate::intro_logo_capture::inspect(lw,lh,i.format,&img.pixels); }\n",""),before)
            self.assertIn("mod intro_logo_capture", (base/"lib.rs").read_text())
            self.assertIn("RAYMAN2FIX_LOGO_CAPTURE",MODULE)
            self.assertIn("gold_pixels",MODULE)
            self.assertIn("MAX_CANDIDATES: usize = 96",MODULE)
            self.assertIn("MAX_CAPTURE_BYTES: usize = 48",MODULE)
            self.assertIn("Rayman2Fix_logo_capture",MODULE)
            self.assertIn("fn write_bmp",MODULE)
            self.assertIn("fnv64",MODULE)
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))

    def test_unknown_pinned_source_fails_closed_without_mutating(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=self.fake_tree(tmp)
            initial=(base/"lib.rs").read_text()
            (base/"render/textures.rs").write_text("pinned layout changed\n")
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))
            self.assertEqual((base/"lib.rs").read_text(),initial)
            self.assertFalse((base/"intro_logo_capture.rs").exists())

if __name__=="__main__": unittest.main()
