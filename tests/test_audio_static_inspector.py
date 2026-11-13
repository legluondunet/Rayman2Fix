"""Pure synthetic PE tests; no proprietary game binary needed in CI."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "inspect_rayman2_audio.py"
SPEC = importlib.util.spec_from_file_location("inspect_rayman2_audio", SCRIPT)
assert SPEC and SPEC.loader
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


def synthetic_pe32() -> bytearray:
    b = bytearray(0x500)
    b[:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x80)
    b[0x80:0x84] = b"PE\x00\x00"
    struct.pack_into("<HHI", b, 0x84, 0x14C, 1, 0x12345678)
    struct.pack_into("<H", b, 0x94, 0xE0)
    struct.pack_into("<H", b, 0x98, 0x10B)
    struct.pack_into("<I", b, 0x98 + 0x1C, 0x400000)
    b[0x178:0x180] = b".text\x00\x00\x00"
    struct.pack_into("<IIII", b, 0x180, 0x200, 0x1000, 0x200, 0x200)
    b[0x220] = 0xE8
    struct.pack_into("<i", b, 0x221, 0x401040 - 0x401025)
    struct.pack_into("<I", b, 0x230, 0xDEADBEEF)
    return b


class AudioStaticInspectorTests(unittest.TestCase):
    def test_call_resolution_and_data(self):
        pe = M.PE32(bytes(synthetic_pe32()))
        self.assertEqual(pe.call_target(0x401020), 0x401040)
        self.assertIsNone(pe.call_target(0x401021))
        self.assertEqual(pe.dword(0x401030), 0xDEADBEEF)
        self.assertEqual(pe.image_base, 0x400000)
        self.assertEqual(pe.timestamp, 0x12345678)

    def test_bad_input_is_rejected(self):
        with self.assertRaises(ValueError):
            M.PE32(b"not a pe")
        sample = synthetic_pe32()
        sample[0x84] = 0
        with self.assertRaises(ValueError):
            M.PE32(sample)

    def test_out_of_section_and_beyond_file_are_rejected(self):
        pe = M.PE32(bytes(synthetic_pe32()))
        with self.assertRaises(ValueError):
            pe.at(0x401400, 1)
        with self.assertRaises(ValueError):
            pe.read(len(pe.raw) - 1, 20)

    def test_packed_resource_handle_is_group_and_record_not_song_id(self):
        # Native 0x453F90: group=upper16, entry=lower16, stride=92.
        self.assertEqual(M.decode_resource_handle(0x00170002),
                         {"group_id": 23, "record_index": 2,
                          "record_stride": 92})
        self.assertEqual(M.decode_resource_handle(0x0000FFFF),
                         {"group_id": 0, "record_index": 65535,
                          "record_stride": 92})
        with self.assertRaises(ValueError):
            M.decode_resource_handle(-1)
        with self.assertRaises(ValueError):
            M.decode_resource_handle(0x1_0000_0000)

    def test_resource_resolver_keeps_byte_verified_bounds(self):
        src = SCRIPT.read_text(encoding="utf-8")
        for fragment in ("group_high16", "index_low16",
                         "sound_bank_registry_lookup", "bank_next_link",
                         "index_stride_92", "type11_nested_resolve_first",
                         "type11_nested_resolve_second",
                         "type11_nested_stride24"):
            self.assertIn(fragment, src)
        # An unrelated PE must not be treated as the owner's reference.
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "other.exe"
            path.write_bytes(synthetic_pe32())
            self.assertIn("address_map_failure", M.inspect(path))

    def test_event_conversion_is_not_confused_with_sxd_transport(self):
        src = SCRIPT.read_text(encoding="utf-8")
        self.assertIn('result["sound_event_conversion"]', src)
        self.assertIn("conversion_tags = list(pe.at(0x4585A0, 11))", src)
        self.assertIn("type11_is_passthrough", src)
        self.assertIn("type8_nested_conversion", src)
        self.assertIn("type10_nested_conversion", src)
        self.assertIn("transport_type_is_separate", src)
        self.assertIn("pe.call_target(0x45856F) == 0x45AA00", src)
        self.assertIn("pe.call_target(0x45855B) == 0x45A4A0", src)

    def test_group_registry_is_command_loaded_not_a_disc_number(self):
        src = SCRIPT.read_text(encoding="utf-8")
        self.assertIn('result["resource_group_registration"]', src)
        self.assertIn('pe.at(0x4507D5, 6)', src)
        self.assertIn("pe.call_target(0x4507E3)", src)
        self.assertIn("pe.call_target(0x450811)", src)
        self.assertIn('"archive_identity_still_unproven": True', src)

    def test_named_bank_and_theme_resource_layout(self):
        src = SCRIPT.read_text(encoding="utf-8")
        self.assertIn('result["resource_type11"]', src)
        self.assertIn('"resource_type": "SND_E_TR_Theme"', src)
        self.assertIn('"enum_value": 11', src)
        self.assertIn('result["sound_bank_and_resource_layout"]', src)
        self.assertIn('"bank_list_global": "0x00514D50"', src)
        self.assertIn('"bank_id_offset": 4', src)
        self.assertIn('"bank_filename_offset": 12', src)
        self.assertIn('"bank_data_pointer_offset": 36', src)
        self.assertIn('"bank_next_pointer_offset": 60', src)
        self.assertIn('"resource_record_bytes": 92', src)
        self.assertIn('"theme_disk_parts_count_offset": 36', src)
        self.assertIn('"theme_disk_outro_offset": 40', src)
        self.assertIn('"SND_E_TR_Theme": 11', src)
        self.assertIn("0x4538E2", src)
        self.assertIn("0x45985A", src)
        self.assertIn("0x45987F", src)

    def test_modified_reference_manifest_is_paths_not_audio(self):
        paths = (
            b"DATA\\WORLD\\SOUND\\MM01.APM\\0"
            b"DATA\\WORLD\\SOUND\\MM10.APM\\0"
            b"DATA\\WORLD\\SOUND\\JAILMUS.APM\\0"
            b"DATA\\WORLD\\SOUND\\MAIN TITLE.APM\\0"
            b"DATA\\WORLD\\SOUND\\BNK_0.BNM\\0"
            b"DATA\\WORLD\\SOUND\\MM01.APM\\0"
        )
        # Python bytes escapes are kept literal here intentionally:
        # replace the textual '\\0' markers with NUL for the fixture.
        fixture = paths.replace(b"\\0", bytes([0]))
        info = M.summarize_gog_sound_manifest(fixture)
        self.assertEqual(info["apm_unique"], 4)
        self.assertEqual(info["bnm_unique"], 1)
        self.assertFalse(info["bnk_0_to_404_complete"])
        self.assertFalse(info["main_menu_mm01_to_mm10_present"])
        self.assertTrue(info["intro_jailmus_present"])
        self.assertTrue(info["main_title_present"])
        self.assertTrue(info["contains_paths_not_sound_bytes"])
        self.assertFalse(info["applies_to_original_cd"])

    def test_esc_and_focus_share_native_pause_and_resume_wrappers(self):
        src = SCRIPT.read_text(encoding="utf-8")
        self.assertIn('result["native_audio_pause_invariants"]', src)
        self.assertIn("pe.call_target(0x401C9C) == 0x4536D0", src)
        self.assertIn("pe.call_target(0x471E70) == 0x4536D0", src)
        self.assertIn("pe.call_target(0x401CE9) == 0x453700", src)
        self.assertIn("pe.call_target(0x471E67) == 0x453700", src)
        self.assertIn("pe.call_target(0x4536ED) == 0x4580B0", src)
        self.assertIn("pe.call_target(0x45371D) == 0x458110", src)
        self.assertIn("mark_written_before_dispatch", src)
        self.assertIn("pause_record_mark_guard", src)
        self.assertIn("clear_mark_before_resume", src)
        self.assertIn("global_resume_depth_dec", src)
        self.assertIn("research_inference", src)
        self.assertIn('"implemented_audio_bypass": False', src)

    def test_reference_file_directory_is_structured_not_file_integrity(self):
        # Synthetic path directory: [declared size, length incl NUL, path]...
        names = [
            (1_000_000, "DATA/FAKE_BIG.DAT"),
            (48_592, "DATA\\\\WORLD\\\\SOUND\\\\MM01.APM"),
            (6_642_012, "DATA\\\\WORLD\\\\SOUND\\\\BNK_0.BNM"),
        ]
        def make_entry(declared, filename):
            raw = filename.encode("ascii")
            return struct.pack("<II", declared, len(raw) + 1) + raw + bytes([0])
        data = b"".join(make_entry(v, n) for v, n in names)
        data += struct.pack("<II", 0xFFFFFFFF, 0) + bytes(12)
        info = M.summarize_reference_file_directory(data)
        self.assertEqual(info["entries"], 3)
        self.assertEqual(info["unique_paths"], 3)
        self.assertTrue(info["sentinel_verified"])
        self.assertEqual(info["declared_bytes_total_not_integrity"], 7_690_604)
        self.assertEqual(info["sound_entries"], {"APM": 1, "BNM": 1})
        self.assertFalse(info["sizes_verified_against_files"])
        self.assertFalse(info["approved_cd_metadata"])

    def test_reference_file_directory_rejects_malformed_entries(self):
        entry = lambda size, name: (
            struct.pack("<II", size, len(name) + 1) + name + bytes([0])
        )
        valid = entry(123, b"README.TXT")
        terminator = struct.pack("<II", 0xFFFFFFFF, 0)
        for invalid in (
            valid,  # missing sentinel
            valid + terminator + bytes([0, 1]),  # dirty padding
            valid + valid + terminator,  # duplicate
            struct.pack("<II", 5, 9999),  # out-of-bounds length
            entry(1, b"A" + bytes([0]) + b"B.TXT") + terminator,  # NUL in name
        ):
            with self.subTest(invalid=invalid[:16]):
                with self.assertRaises(ValueError):
                    M.summarize_reference_file_directory(invalid)

    def test_theme_singleton_lifecycle_guard_is_verified(self):
        src = SCRIPT.read_text(encoding="utf-8")
        self.assertIn('result["theme_controller_lifecycle"]', src)
        self.assertIn("initializes_active_zero", src)
        self.assertIn("nested_slot_minus_one_guard", src)
        self.assertIn("sets_active_one_after_success", src)
        self.assertIn("stops_clear_active_zero", src)
        self.assertIn("pause_requires_active_nonzero", src)
        self.assertIn("resume_requires_active_nonzero", src)
        self.assertIn('"does_not_establish_esc_runtime_state": True', src)

    def test_bnm_uses_two_native_offsets_and_play_event_ref_resolver(self):
        src=SCRIPT.read_text(encoding="utf-8")
        self.assertIn('result["bnm_disk_table_relocation"]',src)
        self.assertIn('"event_table_offset_field": 4',src)
        self.assertIn('"resource_table_offset_field": 12',src)
        self.assertIn("pe.at(0x453BB8,3)",src)
        self.assertIn("pe.at(0x453BC3,3)",src)
        self.assertIn("pe.at(0x453C23,3)",src)
        self.assertIn("pe.at(0x453BD1,11)",src)
        self.assertIn("pe.dword(0x453E90)==0x453DC9",src)
        self.assertIn("pe.at(0x453DC9,3)",src)
        self.assertIn("pe.call_target(0x453DD6)",src)
        self.assertIn('"retail_cd_bank_verified": False',src)

    def test_reference_bank_id_is_formatted_as_bnm_filename(self):
        src=SCRIPT.read_text(encoding="utf-8")
        self.assertIn('result["bank_id_to_bnm_filename_reference"]',src)
        self.assertIn('pe.name_at(0x4A252C) == "bnk_%d.BNM"',src)
        self.assertIn("pe.at(0x453945,3)",src)
        self.assertIn("pe.call_target(0x453948)",src)
        self.assertIn("pe.at(0x45387F,5)",src)
        self.assertIn("pe.at(0x45392E,3)",src)
        self.assertIn('"reference_only_not_verified_on_original_cd": True',src)

    def test_unknown_binary_never_invents_structural_matches(self):
        with tempfile.TemporaryDirectory() as d:
            fn = Path(d) / "synthetic.exe"
            fn.write_bytes(synthetic_pe32())
            result = M.inspect(fn)
            self.assertFalse(result["known_reference_only"])
            self.assertTrue(result["not_approved_cd_proof"])
            self.assertIn("address_map_failure", result)
            self.assertNotIn("resource_type11", result)


if __name__ == "__main__":
    unittest.main()
