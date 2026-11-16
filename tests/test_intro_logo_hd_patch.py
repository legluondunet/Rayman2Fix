"""Ensure optional user-supplied logo never changes unrecognized GameFrame textures."""
import pathlib
import tempfile
import unittest
from tools.gameframe.apply_intro_logo_hd import install,MODULE

class PinnedIntroLogoHDTests(unittest.TestCase):
    def fixture(self,root):
        base=pathlib.Path(root)/"wrappers/glide/src"
        (base/"render").mkdir(parents=True)
        (base/"lib.rs").write_text("#[cfg(windows)]\nmod loading_image_fix;\n")
        (base/"render/textures.rs").write_text(
            "            for (k, (lod, &(a, b))) in levels.zip(&t.ranges).enumerate() {\n"
            "                    Ok(img) => unsafe {\n"
            "                        (gl.TexImage2D)(TEXTURE_2D, k as i32, RGBA8 as i32, lw as i32, lh as i32, 0, RGBA, UNSIGNED_BYTE, img.pixels.as_ptr().cast());\n"
            "                    },\n")
        return base
    def test_unknown_textures_preserve_original_upload(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=self.fixture(tmp)
            install(pathlib.Path(tmp))
            contents=(base/"render/textures.rs").read_text()
            self.assertIn("crate::intro_logo_hd::replacement",contents)
            self.assertIn("let hd_chain_ok=",contents)
            self.assertIn("levels.clone().all",contents)
            self.assertIn("let mut hd_tile:Option<Vec<u8>>=None",contents)
            self.assertIn("crate::intro_logo_hd::mip_from_hd(base,lw,lh)",contents)
            self.assertIn("(lw*4) as i32,(lh*4) as i32",contents)
            self.assertNotIn("t.ranges.len()==1",contents)
            self.assertIn("crate::intro_logo_hd::observe_source",contents)
            self.assertIn("if let Some(pixels)",contents)
            self.assertIn("else",contents)
            self.assertIn("img.pixels.as_ptr().cast()",contents)
            self.assertIn("const TITLE_HASHES:[u64;18]",MODULE)
            self.assertIn("let n=17-index;",MODULE)
            self.assertIn("TILE-1-y",MODULE)
            self.assertIn("R2LOGOHD",MODULE)
            self.assertIn("pub fn compatible_mip",MODULE)
            self.assertIn("pub fn mip_from_hd",MODULE)
            self.assertIn("alpha_downsample_preserves_color_at_transparent_edges",MODULE)
            self.assertIn("renderer hook ACTIVE",MODULE)
            self.assertIn("recognized source logo tile but HD atlas",MODULE)
            self.assertIn("first 32x32 format-12 source is not",MODULE)
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))
    def test_unpinned_upstream_rejected_atomically(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=self.fixture(tmp)
            before=(base/"lib.rs").read_text()
            (base/"render/textures.rs").write_text("changed upstream")
            with self.assertRaises(SystemExit):
                install(pathlib.Path(tmp))
            self.assertEqual(before,(base/"lib.rs").read_text())
            self.assertFalse((base/"intro_logo_hd.rs").exists())

if __name__=="__main__":unittest.main()
