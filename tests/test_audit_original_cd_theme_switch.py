"""Synthetic fixture tests for original retail-CD Theme state machine evidence.

The user's actual copyrighted binary is not included in this repo or CI.
"""
from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
import audit_original_cd_theme_switch as audit


class FakeOriginalPE:
    machine = 0x14C
    timestamp = audit.ORIGINAL_PE_TIMESTAMP
    image_base = audit.ORIGINAL_IMAGE_BASE
    image_size = audit.ORIGINAL_IMAGE_SIZE

    def __init__(self, data: bytes):
        self.opcodes = {
            rva: bytes.fromhex(raw)
            for _, rva, raw, _, _ in audit.SITES
        }

    def read_rva(self, rva: int, size: int) -> bytes:
        return self.opcodes[rva][:size]


class OriginalCDThemeStaticAuditTests(unittest.TestCase):
    def test_original_15_sites_are_explicit_and_never_audio_hook_commands(self):
        self.assertEqual(len(audit.SITES), 15)
        self.assertEqual(len(set(item[0] for item in audit.SITES)),15)
        self.assertEqual(len(set(item[1] for item in audit.SITES)),15)
        self.assertEqual(len(audit.ORIGINAL_SHA256),64)
        self.assertEqual(audit.ORIGINAL_IMAGE_BASE,0x400000)
        self.assertEqual(audit.ORIGINAL_PE_TIMESTAMP,0x3807B68B)
        self.assertEqual(audit.ORIGINAL_IMAGE_SIZE,0x117000)
        self.assertIn("end_of_part_requeues_flag",
                      [item[0] for item in audit.SITES])
        self.assertIn("end_of_part_rewrites_target",
                      [item[0] for item in audit.SITES])
        self.assertIn("progress_promotes_target",
                      [item[0] for item in audit.SITES])

    def test_different_version_fails_before_static_offsets_are_read(self):
        with patch.object(audit,"PE32") as constructor:
            with self.assertRaisesRegex(audit.PEFormatError,
                                        "SHA-256 does not match"):
                audit.audit(b"synthetic non-retail PE")
            constructor.assert_not_called()

    def test_synthetic_hash_override_exercises_full_static_contract(self):
        fake=b"synthetic only, no original executable bytes"
        digest=hashlib.sha256(fake).hexdigest()
        with patch.object(audit,"PE32",FakeOriginalPE):
            report=audit.audit(fake,expected_sha256=digest)
        self.assertEqual(report["sha256"],digest)
        self.assertEqual(report["site_count"],15)
        self.assertEqual(report["action_policy"],
                         "READ_ONLY_NO_NATIVE_AUDIO_INTERVENTION")
        labels=[r["site"] for r in report["sites"]]
        self.assertLess(labels.index("progress_clears_pending"),
                        labels.index("progress_promotes_target"))
        self.assertLess(labels.index("end_of_part_requeues_flag"),
                        labels.index("end_of_part_rewrites_target"))

    def test_wrong_opcode_in_original_layout_fails_closed(self):
        fake=b"another synthetic source"
        digest=hashlib.sha256(fake).hexdigest()

        class IncorrectPE(FakeOriginalPE):
            def __init__(self, data):
                super().__init__(data)
                self.opcodes[0x5953A]=b"\x90\x90\x90"

        with patch.object(audit,"PE32",IncorrectPE):
            with self.assertRaisesRegex(audit.PEFormatError,
                                        "opcode mismatch.*end_of_part_rewrites_target"):
                audit.audit(fake,expected_sha256=digest)

    def test_wrong_pe_version_fails_closed(self):
        fake=b"synthetic PE version"
        digest=hashlib.sha256(fake).hexdigest()

        class WrongPE(FakeOriginalPE):
            timestamp=0

        with patch.object(audit,"PE32",WrongPE):
            with self.assertRaisesRegex(audit.PEFormatError,
                                        "header fingerprint mismatch"):
                audit.audit(fake,expected_sha256=digest)

    def test_two_distinct_paths_can_set_pending_target(self):
        code={name:(rva, bytes.fromhex(raw),action)
              for name,rva,raw,action,_ in audit.SITES}
        self.assertEqual(code["explicit_pending_target"][1],
                         b"\x89\x51\x44")
        self.assertEqual(code["end_of_part_rewrites_target"][1],
                         b"\x89\x51\x44")
        self.assertNotEqual(code["explicit_pending_target"][0],
                            code["end_of_part_rewrites_target"][0])
        self.assertEqual(code["progress_clears_pending"][1],
                         b"\x89\x59\x0c")
        self.assertEqual(code["progress_promotes_target"][1],
                         b"\x89\x41\x14")

    def test_no_dynamic_import_or_native_exe_execution(self):
        source=(Path(__file__).resolve().parents[1] /
                "tools/audit_original_cd_theme_switch.py").read_text()
        for forbidden in ("ctypes", "subprocess", "os.system(",
                          "mmap(", "popen(", "WriteProcessMemory",
                          "VirtualProtect", "CreateRemoteThread",
                          "LoadLibrary", "GetProcAddress"):
            self.assertNotIn(forbidden,source)


if __name__=="__main__":
    unittest.main()
