"""Pinned GameFrame logo tile replacement guard and no pixel asset redistribution."""
import pathlib
import tempfile
import unittest
from tools.gameframe.apply_intro_logo_upscale import install, MODULE

class IntroLogoUpscalePatchTests(unittest.TestCase):
    def make(self,root):
        base=pathlib.Path(root)/"wrappers/glide/src"
        (base/"render").mkdir(parents=True)
        (base/"lib.rs").write_text("#[cfg(windows)]\nmod loading_image_fix;\n")
        (base/"render/textures.rs").write_text(
            "            for (k, (lod, &(a, b))) in levels.zip(&t.ranges).enumerate() {\n"
            "                    Ok(img) => unsafe {\n"
            "                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n"
            "                    },\n")
        return base
    def test_exact_source_and_no_unrelated_replacements(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=self.make(tmp)
            install(pathlib.Path(tmp))
            content=(base/"render/textures.rs").read_text()
            self.assertIn("let mut title_tile=false;",content)
            self.assertIn("if k==0 {title_tile",content)
            self.assertIn("resample_rgba(lw,lh,&img.pixels)",content)
            self.assertIn("(lw*4) as i32,(lh*4) as i32",content)
            self.assertIn("} else {",content)
            self.assertIn("const TITLE_HASHES: [u64;18]",MODULE)
            self.assertIn("alpha+=weight*a",MODULE)
            self.assertIn("wrong_size_format_or_pixels_never_match",MODULE)
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))
    def test_changed_upstream_refused_without_partial_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=self.make(tmp)
            old=(base/"lib.rs").read_text()
            (base/"render/textures.rs").write_text("new upstream")
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))
            self.assertEqual(old,(base/"lib.rs").read_text())
            self.assertFalse((base/"intro_logo_upscale.rs").exists())

if __name__=="__main__":unittest.main()
