"""Synthetic, offline tests for the approved Rayman 2 interpreter preflight.

No retail code is bundled, executed, copied or modified.
"""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "learn10_interpreter_preflight.py"
SPEC = importlib.util.spec_from_file_location("learn10_pe_preflight", SCRIPT)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)

PE = 0x80
OPT = PE + 24
SECTION = OPT + 0xE0
RAW_OFFSET = 0x200
RAW_SIZE = 0x9B000
TEXT_RVA = 0x1000


def fake_image():
    """Generate a well-formed minimal PE32 with two known synthetic anchors."""
    raw = bytearray(RAW_OFFSET + RAW_SIZE)
    raw[:2] = b"MZ"
    struct.pack_into("<I", raw, 0x3C, PE)
    raw[PE:PE+4] = b"PE\x00\x00"
    struct.pack_into("<HHIIIHH", raw, PE+4,
                     0x14C, 1, module.APPROVED_PE_TIMESTAMP, 0, 0, 0xE0, 0x0102)
    struct.pack_into("<H", raw, OPT, 0x10B)
    struct.pack_into("<I", raw, OPT+28, module.IMAGE_BASE)
    struct.pack_into("<I", raw, OPT+56, 0x100000)
    raw[SECTION:SECTION+8] = b".text\x00\x00\x00"
    struct.pack_into("<IIII", raw, SECTION+8, RAW_SIZE,
                     TEXT_RVA, RAW_SIZE, RAW_OFFSET)
    struct.pack_into("<I", raw, SECTION+36, 0x60000020)
    for name, (va, signature) in module.KNOWN_ANCHORS.items():
        off = RAW_OFFSET + (va - module.IMAGE_BASE - TEXT_RVA)
        raw[off:off+len(signature)] = signature
    # Synthetic direct CALL bytes targeting the two nearby candidates.
    # This is intentionally only a raw byte-pattern fixture.
    for call_va, target_va in [(0x431000, 0x480800), (0x431020, 0x480830)]:
        call_off = RAW_OFFSET + (call_va - module.IMAGE_BASE - TEXT_RVA)
        struct.pack_into("<Bi", raw, call_off, 0xE8, target_va - (call_va + 5))
    for index, va in enumerate(module.REFERENCE_AI_FUNCTIONS.values()):
        off = RAW_OFFSET + (va - module.IMAGE_BASE - TEXT_RVA)
        raw[off:off+8] = bytes([0x40 + index] * 8)
    return raw


class ApprovedPe32InterpreterPreflightTests(unittest.TestCase):
    def test_fixture_remains_unverified_even_with_known_anchors(self):
        out = module.inspect_pe32(bytes(fake_image()))
        self.assertEqual(out["status"], "CANDIDATE_NOT_VERIFIED")
        self.assertEqual(out["known_control_anchors"],
                         {"screen_to_vertex": "matched", "superimposed_proc": "matched"})
        self.assertFalse(out["allows_hook"])
        self.assertFalse(out["writes_to_game"])
        self.assertFalse(out["approved_cd_static_disassembly"])
        self.assertFalse(out["If_IfNot_share_same_native_handler"])
        self.assertFalse(out["runtime_detour_abi_or_reentrancy_proven"])
        self.assertEqual(set(out["candidate_functions"]), set(module.REFERENCE_AI_FUNCTIONS))
        for c in out["candidate_functions"].values():
            self.assertEqual(len(c["sample_sha256"]), 64)
            self.assertEqual(c["sample_size"], 32)
            self.assertFalse(c["machine_code_reviewed"])
            self.assertFalse(c["call_abi_verified"])
            self.assertFalse(c["patterns_are_verified_calls"])
        self.assertEqual(out["candidate_functions"]["eval_keyword"]["raw_e8_rel32_pattern_count"], 1)
        self.assertEqual(out["candidate_functions"]["eval_keyword"]["raw_e8_rel32_site_rvas"], ["0x031000"])
        self.assertEqual(out["candidate_functions"]["eval_condition"]["raw_e8_rel32_pattern_count"], 1)
        self.assertEqual(out["candidate_functions"]["eval_condition"]["raw_e8_rel32_site_rvas"], ["0x031020"])
        self.assertEqual(out["candidate_functions"]["eval_tree"]["raw_e8_rel32_pattern_count"], 0)

    def test_machine_and_header_checks_are_fail_closed(self):
        for which, offset, replacement in [
            ("dos", 0, b"ZZ"), ("pe", PE, b"ZZ"),
            ("x64", PE+4, b"\x64\x86"), ("pe32plus", OPT, b"\x0b\x02"),
            ("timestamp", PE+8, b"\x00\x00\x00\x00"),
        ]:
            with self.subTest(which=which):
                raw = fake_image()
                raw[offset:offset+len(replacement)] = replacement
                with self.assertRaises(module.PreflightError):
                    module.inspect_pe32(bytes(raw))

    def test_rejects_bad_executable_section_and_anchors(self):
        raw = fake_image()
        struct.pack_into("<I", raw, SECTION+36, 0x40000040)  # no execute
        with self.assertRaises(module.PreflightError):
            module.inspect_pe32(bytes(raw))
        raw = fake_image()
        raw[RAW_OFFSET + (0x4710C0 - module.IMAGE_BASE - TEXT_RVA)] ^= 1
        with self.assertRaisesRegex(module.PreflightError, "approved-anchor-mismatch"):
            module.inspect_pe32(bytes(raw))

    def test_rejects_bad_section_spans_or_offsets(self):
        for offset, value in [(SECTION+12, 0xF0000),
                              (SECTION+16, 0xFFFFFFFF),
                              (SECTION+20, 0xFFFFFFFF)]:
            with self.subTest(offset=offset):
                raw = fake_image()
                struct.pack_into("<I", raw, offset, value)
                with self.assertRaises(module.PreflightError):
                    module.inspect_pe32(bytes(raw))

    def test_cli_requires_exact_full_file_hash(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "rayman2.exe"
            p.write_bytes(bytes(fake_image()))
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                status = module.main(["tool", str(p)])
            self.assertEqual(status, 2)
            self.assertIn("unapproved-file-length", stdout.getvalue())
            p.write_bytes(b"\x00" * module.APPROVED_SIZE)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                status = module.main(["tool", str(p)])
            self.assertEqual(status, 2)
            self.assertIn("unapproved-file-sha256", stdout.getvalue())

    def test_verified_metadata_never_authorizes_hook(self):
        self.assertEqual(module.APPROVED_SHARED_IF_HANDLER, 0x489250)
        self.assertEqual(module.APPROVED_KEYWORD_TABLE_FILE_OFFSET, 0xA5E80)
        self.assertEqual(module.APPROVED_IFNOT_INVERSION_VA, 0x4892DF)
        self.assertEqual(len(module.APPROVED_IFNOT_SHA256_16), 64)
        self.assertEqual(len(module.APPROVED_MACHINE_CODE), 6)
        for va, digest in module.APPROVED_MACHINE_CODE.values():
            self.assertGreaterEqual(va, 0x400000)
            self.assertEqual(len(digest), 64)
        self.assertFalse(module.inspect_pe32(bytes(fake_image()))["allows_hook"])

    def test_hash_and_anchor_constants_match_project_sources(self):
        launcher = (ROOT / "src/launcher/auto_flow.cpp").read_text(encoding="utf-8")
        runtime = (ROOT / "src/runtime/runtime.cpp").read_text(encoding="utf-8")
        self.assertIn(module.APPROVED_SHA256, launcher)
        self.assertIn(str(module.APPROVED_SIZE), launcher)
        self.assertIn("0x3807B68B", runtime)
        self.assertIn("0x710C0", runtime)
        self.assertIn("0x22940", runtime)


if __name__ == "__main__":
    unittest.main()
