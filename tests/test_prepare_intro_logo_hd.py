"""RayWiki 403 fallback: local, user-downloaded logo is preferred."""
import pathlib
import tempfile
import unittest
from tools.prepare_intro_logo_hd import SOURCE_URL,find_local_logo,main

class DownloadFallbackTests(unittest.TestCase):
    def test_default_logo_destination(self):
        import inspect
        source=inspect.getsource(main)
        self.assertIn('rayman2/rayman2fix/textures/logo.rgba',source)
        self.assertNotIn('Rayman2Fix_logo_hd.rgba',source)

    def test_original_url_is_explicit(self):
        self.assertEqual(SOURCE_URL,"https://raymanpc.com/wiki/script-en/images/b/bb/Rayman_2_Logo.png")

    def test_local_png_is_found_without_network(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp)
            home=root/"home"
            cwd=root/"game"
            home.mkdir()
            cwd.mkdir()
            self.assertIsNone(find_local_logo(home,cwd))
            folder=home/"Téléchargements"
            folder.mkdir()
            saved=folder/"Rayman_2_Logo.png"
            saved.write_bytes(b"dummy")
            self.assertEqual(find_local_logo(home,cwd),saved)
            current=cwd/"Rayman_2_Logo.png"
            current.write_bytes(b"dummy")
            self.assertEqual(find_local_logo(home,cwd),current)

    def test_english_downloads(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp)
            home=root/"home"
            cwd=root/"game"
            home.mkdir()
            cwd.mkdir()
            folder=home/"Downloads"
            folder.mkdir()
            saved=folder/"Rayman 2 Logo.png"
            saved.write_bytes(b"dummy")
            self.assertEqual(find_local_logo(home,cwd),saved)

if __name__=="__main__":
    unittest.main()
