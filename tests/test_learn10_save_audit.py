"""Synthetic, read-only Rayman 2 save inventory tests."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SRC = Path(__file__).resolve().parent.parent / "tools" / "learn10_save_audit.py"
spec = importlib.util.spec_from_file_location("learn10_save_audit", SRC)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class Learn10SaveAuditTests(unittest.TestCase):
    def test_add_modify_remove_and_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "game" / "Data" / "SaveGame"
            folder.mkdir(parents=True)
            item = folder / "Slot0.sav"
            item.write_bytes(b"first")
            before = audit.inventory(root)
            self.assertFalse(before["scan_incomplete"])
            self.assertEqual(audit.compare(before, audit.inventory(root))["status"], "UNCHANGED")
            item.write_bytes(b"changed")
            delta = audit.compare(before, audit.inventory(root))
            self.assertEqual(delta["status"], "CHANGED")
            self.assertEqual(delta["changed"], ["game/Data/SaveGame/Slot0.sav"])
            item.unlink()
            delta = audit.compare(before, audit.inventory(root))
            self.assertEqual(delta["removed"], ["game/Data/SaveGame/Slot0.sav"])

    def test_both_known_save_folders_case_insensitive(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "data" / "savegame").mkdir(parents=True)
            (root / "Menu" / "SAVEGAME").mkdir(parents=True)
            (root / "Menu" / "SAVEGAME" / "raymanm.sav").write_bytes(b"menu")
            result = audit.inventory(root)
            self.assertFalse(result["scan_incomplete"])
            self.assertEqual(len(result["directories"]), 2)
            self.assertEqual(len(result["files"]), 1)

    def test_missing_save_dirs_is_incomplete(self):
        with tempfile.TemporaryDirectory() as temp:
            result = audit.inventory(Path(temp))
            self.assertTrue(result["scan_incomplete"])
            self.assertEqual(audit.compare(result, result)["status"], "INCOMPLETE")

    def test_report_cannot_write_into_save_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            save = root / "Data" / "SaveGame"
            save.mkdir(parents=True)
            with self.assertRaises(ValueError):
                audit.write_out(audit.inventory(root), save / "report.json", root)

    def test_refuse_symlinks(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            save = root / "Data" / "SaveGame"
            save.mkdir(parents=True)
            other = root / "external.sav"
            other.write_bytes(b"not to follow")
            (save / "link.sav").symlink_to(other)
            result = audit.inventory(root)
            self.assertTrue(result["scan_incomplete"])
            self.assertNotIn("Data/SaveGame/link.sav", result["files"])

    def test_bad_baseline_refused(self):
        with self.assertRaises(ValueError):
            audit.compare({"format": "wrong", "files": {}},
                          {"format": audit.FORMAT, "files": {}})


if __name__ == "__main__":
    unittest.main()
