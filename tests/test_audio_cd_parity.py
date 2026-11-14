"""Pure synthetic checks for the approved-CD audio parity tool (no game files)."""
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

MOD_PATH = Path(__file__).resolve().parents[1] / "tools" / "check_rayman2_cd_audio_parity.py"
SPEC = importlib.util.spec_from_file_location("audio_cd_parity", MOD_PATH)
MOD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MOD
SPEC.loader.exec_module(MOD)


def fake_pe(text_offset=0x200):
    # Tiny PE32 with equal virtual addresses, different physical offsets.
    rdata_offset = text_offset + 0x100
    data = bytearray(rdata_offset + 0x100)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHI", data, 0x84, 0x14C, 2, 0)
    struct.pack_into("<H", data, 0x94, 0xE0)
    struct.pack_into("<H", data, 0x98, 0x10B)
    struct.pack_into("<I", data, 0x98 + 0x1C, 0x400000)
    data[0x178:0x180] = b".text\0\0\0"
    struct.pack_into("<IIII", data, 0x180, 0x100, 0x1000, 0x100, text_offset)
    data[0x1A0:0x1A8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, 0x1A8, 0x100, 0x2000, 0x100, rdata_offset)
    data[text_offset:text_offset+0x100] = bytes(range(256))
    data[rdata_offset:rdata_offset+0x100] = b"A" * 0x100
    return data


class ParityTests(unittest.TestCase):
    def test_independent_pe_raw_offsets(self):
        a, b = MOD.PE32(fake_pe(0x200)), MOD.PE32(fake_pe(0x400))
        self.assertEqual(a.at(0x401020, 16), b.at(0x401020, 16))
        windows = MOD.compare_regions(a, b, (("test", 0x401020, 0x401040),))
        self.assertEqual([x["identical"] for x in windows], [True])

    def test_modified_byte_is_detected_without_printing_payload(self):
        a, b = fake_pe(0x200), fake_pe(0x400)
        b[0x400 + 0x28] ^= 0x01
        result = MOD.compare_regions(MOD.PE32(a), MOD.PE32(b),
                                     (("test", 0x401020, 0x401040),))
        self.assertFalse(result[0]["identical"])
        self.assertNotIn("data", result[0])

    def test_only_exact_pinned_pair_can_be_called_verified_cd(self):
        a, b = fake_pe(0x200), fake_pe(0x400)
        with self.assertRaisesRegex(ValueError, "fingerprints"):
            MOD.audit(a, b)
        result = MOD.audit(a, b, require_pins=False, check_anchors=False,
                           regions=(("test", 0x401020, 0x401040),))
        self.assertFalse(result["exact_pinned_pair"])
        self.assertFalse(result["original_cd_audio_path_confirmed"])
        self.assertTrue(result["selected_audio_code_equal"])

    def test_call_target_is_real_x86_relative_call(self):
        f = fake_pe()
        va = 0x401020
        f[0x220] = 0xE8
        struct.pack_into("<i", f, 0x221, 0x401050 - (va + 5))
        self.assertEqual(MOD.PE32(f).call_target(va), 0x401050)
        self.assertIsNone(MOD.PE32(f).call_target(va + 1))

    def test_unbacked_or_cross_section_reads_fail(self):
        a = MOD.PE32(fake_pe())
        with self.assertRaises(ValueError):
            a.at(0x4010FA, 12)
        with self.assertRaises(ValueError):
            a.at(0x4020F0, 32)
        with self.assertRaises(ValueError):
            MOD.compare_regions(a, a, (("bad", 0x401020, 0x401020),))

    def test_absolute_dword_xrefs_are_section_scoped_and_va_based(self):
        a = fake_pe(0x400)
        operand = struct.pack("<I", 0x4B9618)
        a[0x410:0x414] = operand
        a[0x490:0x494] = operand
        # Ignore the same literal in a different PE section.
        a[0x500:0x504] = operand
        pe = MOD.PE32(a)
        self.assertEqual(MOD.absolute_dword_xrefs(pe, 0x4B9618),
                         [0x401010, 0x401090])
        with self.assertRaisesRegex(ValueError, "not a 32-bit"):
            MOD.absolute_dword_xrefs(pe, 0x100000000)
        with self.assertRaisesRegex(ValueError, "Section not present"):
            MOD.absolute_dword_xrefs(pe, 0x4B9618, ".missing")

    def test_direct_rel32_scan_distinguishes_targets_and_sections(self):
        f = fake_pe(0x400)
        for offset in (0x10, 0x40):
            va = 0x401000 + offset
            f[0x400 + offset] = 0xE8
            struct.pack_into("<i", f, 0x400 + offset + 1,
                             0x401080 - (va + 5))
        # This looks like E8 but targets a DIFFERENT call destination.
        f[0x4A0] = 0xE8
        struct.pack_into("<i", f, 0x4A1, 0)
        pe = MOD.PE32(f)
        self.assertEqual(MOD.direct_e8_candidate_sites(pe, 0x401080),
                         [0x401010, 0x401040])
        self.assertEqual(MOD.direct_e8_candidate_sites(pe, 0x4010A5),
                         [0x4010A0])
        self.assertEqual(MOD.direct_e8_candidate_sites(pe, 0x401077), [])
        with self.assertRaisesRegex(ValueError, "Invalid x86"):
            MOD.direct_e8_candidate_sites(pe, -1)

    def test_script_opcode_dispatch_is_generic_not_menu_confirmation(self):
        class ScriptPeStub:
            def __init__(self):
                self.mem = {
                    va: expected for _, va, expected in MOD.SCRIPT_MACHINE_ANCHORS
                }
                self.calls = {
                    site: target for _, _op, site, target
                    in MOD.SCRIPT_OPCODE_BRANCHES
                }
                for opcode in MOD.SCRIPT_AUDIO_OPCODE_IDS:
                    va = MOD.SCRIPT_OPCODE_TABLE_VA + opcode * 8
                    self.mem[va] = struct.pack("<I", MOD.SCRIPT_DISPATCHER_VA)
                    self.mem[va+4] = struct.pack("<I", 0)

            def at(self, va, length):
                return self.mem.get(va, bytes(length))[:length]

            def call_target(self, va):
                return self.calls.get(va)

        pe = ScriptPeStub()
        result = MOD.verify_script_opcode_dispatch(pe)
        self.assertTrue(result["verified_native_dispatcher"])
        self.assertEqual(result["opcodes"], [0x1AA, 0x1AB, 0x1AC])
        self.assertFalse(result["script_origin_alone_proves_esc_menu"])
        self.assertFalse(result["script_origin_alone_proves_f1_menu"])
        self.assertTrue(result["script_caller_or_menu_ui_state_observation_needed"])
        pe.mem[MOD.SCRIPT_OPCODE_TABLE_VA + 0x1AB * 8] = struct.pack(
            "<I", 0x471E80)
        self.assertFalse(MOD.verify_script_opcode_dispatch(pe)[
            "verified_native_dispatcher"])
        pe.mem[MOD.SCRIPT_OPCODE_TABLE_VA + 0x1AB * 8] = struct.pack(
            "<I", MOD.SCRIPT_DISPATCHER_VA)
        pe.calls[0x471E70] = 0x453700
        self.assertFalse(MOD.verify_script_opcode_dispatch(pe)[
            "verified_native_dispatcher"])

    def test_script_opcode_dispatch_uses_actual_index_not_key_name(self):
        self.assertEqual(MOD.SCRIPT_OPCODE_TABLE_VA + 0x1AA * 8, 0x4A5130)
        self.assertEqual(MOD.SCRIPT_OPCODE_TABLE_VA + 0x1AB * 8, 0x4A5138)
        self.assertEqual(MOD.SCRIPT_OPCODE_TABLE_VA + 0x1AC * 8, 0x4A5140)
        self.assertEqual(MOD.SCRIPT_OPCODE_BRANCHES[0][2], 0x471E70)
        self.assertEqual(MOD.SCRIPT_OPCODE_BRANCHES[1][2], 0x471E67)

    def test_original_game_menu_flag_relationships_are_not_esc_proof(self):
        from unittest.mock import patch

        class NativeMenuSyntheticPE:
            def __init__(self):
                self.memory = {
                    va: b for _, va, b in MOD.INGAME_MENU_CODE_ANCHORS
                }
                self.memory[MOD.AI_MENU_VM_ENTRY_VA] = struct.pack(
                    "<II", MOD.AI_MENU_VM_HANDLER_VA, 0
                )
                self.memory[MOD.WINDOW_PAUSE_TITLE_VA] = (
                    b"Rayman2 - pause" + bytes([0])
                )

            def at(self, va, size):
                return self.memory.get(va, bytes(size))[:size]

            def call_target(self, va):
                return 0x480B90 if va == 0x4730BB else None

        a = NativeMenuSyntheticPE()
        def xrefs(pe, target):
            return {
                MOD.AI_INGAME_MENU_FLAG_VA: MOD.AI_INGAME_MENU_XREFS,
                MOD.ENGINE_PAUSE_FLAG_VA: MOD.ENGINE_PAUSE_XREFS,
                MOD.WINDOW_PAUSE_TITLE_VA: MOD.WINDOW_PAUSE_TITLE_XREFS,
            }[target]

        with patch.object(MOD, "absolute_dword_xrefs", side_effect=xrefs):
            result = MOD.inspect_native_menu_indicators(a)
            self.assertTrue(result["native_relationships_verified"])
            self.assertEqual(result["ai_ingame_menu_read_sites"], 8)
            self.assertEqual(result["ai_ingame_menu_direct_init_and_vm_writer_sites"], 2)
            self.assertTrue(result["engine_pause_is_separate_from_ai_ingame_menu"])
            self.assertTrue(result["ai_ingame_menu_does_not_prove_ESC_specific"])
            self.assertTrue(result["ai_ingame_menu_does_not_exclude_F1"])
            self.assertTrue(result["gameplay_menu_lifetime_requires_independent_validation"])
            # A changed VM script handler must invalidate the evidence.
            a.memory[MOD.AI_MENU_VM_ENTRY_VA] = struct.pack("<II", 0x4730B0, 0)
            self.assertFalse(
                MOD.inspect_native_menu_indicators(a)["native_relationships_verified"]
            )

    def test_menu_background_handle_is_not_menu_visibility_evidence(self):
        from unittest.mock import patch

        class PE:
            def __init__(self):
                self.chunks = {
                    0x40406A: bytes.fromhex(
                        "8B 48 14 8B 11 89 15 50 72 4A 00"),
                    0x4245C8: bytes.fromhex(
                        "8B 0D AC 36 50 00 8B 15 50 72 4A 00 "
                        "51 52 FF 15 54 48 50 00"),
                }

            def at(self, va, size):
                return self.chunks.get(va, bytes(size))[:size]

        pe = PE()
        with patch.object(MOD, "absolute_dword_xrefs", return_value=(
            0x404071, 0x4245D0
        )):
            result = MOD.inspect_menu_background_handle(pe)
            self.assertTrue(result["matched"])
            self.assertTrue(result["initialization_is_not_menu_open"])
            self.assertTrue(result["render_use_is_not_proof_of_ESC"])
            self.assertTrue(result["render_use_is_not_proof_of_F1"])
            pe.chunks[0x4245C8] = b"\x90" + pe.chunks[0x4245C8][1:]
            self.assertFalse(MOD.inspect_menu_background_handle(pe)["matched"])

    def test_menu_actor_registry_roots_are_pinned_to_original_cd(self):
        anchors = {
            name: (addr, op)
            for name, addr, op in MOD.ANCHOR_BYTES
            if name.startswith("menu_personal_")
               or name.startswith("menu_dynamic_world_")
               or name.startswith("menu_inactive_world_")
        }
        self.assertEqual(anchors, {
            "menu_personal_list_initializer":
                (0x403C90,bytes.fromhex("BA F8 13 50 00")),
            "menu_personal_list_indexer":
                (0x407B04,bytes.fromhex("A1 F8 13 50 00")),
            "menu_personal_next_pointer":
                (0x407B0D,bytes.fromhex("8B 00")),
            "menu_personal_name_pointer":
                (0x407B16,bytes.fromhex("8B 40 0C")),
            "menu_dynamic_world_global":
                (0x403B75,bytes.fromhex("A3 D0 0F 50 00")),
            "menu_inactive_world_global":
                (0x403B7F,bytes.fromhex("A3 C4 0F 50 00")),
        })

    def test_inverse_loaded_bank_lookup_has_original_cd_code_anchors(self):
        anchors = {
            name: (address, raw)
            for name, address, raw in MOD.ANCHOR_BYTES
            if name.startswith("loaded_bank_")
        }
        self.assertEqual(len(anchors), 7)
        self.assertEqual(anchors["loaded_bank_list_head"],
                         (0x4538C0, bytes.fromhex("A1 50 4D 51 00")))
        self.assertEqual(anchors["loaded_bank_id_field"],
                         (0x4538CD, bytes.fromhex("39 48 04")))
        self.assertEqual(anchors["loaded_bank_next_field"],
                         (0x4538D2, bytes.fromhex("8B 40 3C")))
        self.assertEqual(anchors["loaded_bank_descriptor_bytes"],
                         (0x4538E2, bytes.fromhex("6A 48")))
        self.assertEqual(anchors["loaded_bank_head_pointer"],
                         (0x453FB2, bytes.fromhex("8B 40 24")))
        self.assertEqual(anchors["loaded_bank_resource_count"],
                         (0x453FB5, bytes.fromhex("3B 70 10")))
        self.assertEqual(anchors["loaded_bank_resource_first"],
                         (0x453FBA, bytes.fromhex("8B 50 0C")))
        self.assertEqual(MOD.RESOURCE_RESOLVER_SIZE, 64)
        self.assertEqual(MOD.RESOURCE_RESOLVER_VA, 0x453F90)

    def test_native_menu_viewport_switch_is_code_verified_not_esc_verified(self):
        class PE:
            def __init__(self):
                self.render = MOD.MENU_RENDER_BRANCH_BYTES
                self.targets = {
                    0x40B014: 0x424440,  # in-game menu UI camera
                    0x40B029: 0x429F70,  # normal scene branch
                }

            def at(self, va, size):
                return self.render[:size] if va == 0x40B003 else bytes(size)

            def call_target(self, va):
                return self.targets.get(va)

        pe = PE()
        self.assertEqual(len(MOD.MENU_RENDER_BRANCH_BYTES), 47)
        self.assertTrue(MOD.verify_native_menu_viewport_branch(pe))
        pe.targets[0x40B029] = 0x424440
        self.assertFalse(MOD.verify_native_menu_viewport_branch(pe))
        pe.targets[0x40B029] = 0x429F70
        pe.render = b"\x90" + pe.render[1:]
        self.assertFalse(MOD.verify_native_menu_viewport_branch(pe))

    def test_menu_xrefs_and_opcode_assumptions_are_anchored(self):
        self.assertEqual(len(MOD.AI_INGAME_MENU_XREFS), 10)
        self.assertEqual(len(MOD.ENGINE_PAUSE_XREFS), 3)
        self.assertEqual(len(MOD.WINDOW_PAUSE_TITLE_XREFS), 2)
        self.assertEqual(MOD.AI_MENU_VM_ENTRY_VA, 0x4A53A0)
        self.assertEqual(MOD.AI_MENU_VM_OPCODE_ID, 0x1F8)
        self.assertNotEqual(MOD.AI_INGAME_MENU_FLAG_VA, MOD.ENGINE_PAUSE_FLAG_VA)

    def test_transaction_flag_requires_exact_known_code_locations(self):
        # Code and operand checks are separate: loose DWORD scanning alone
        # cannot prove that a hit was an actual machine-code instruction.
        self.assertEqual(len(MOD.THEME_TRANSACTION_OPS), 6)
        self.assertEqual(len(MOD.THEME_FLAG_OPERAND_VAS), 6)
        self.assertEqual(
            [op[1] for op in MOD.THEME_TRANSACTION_OPS],
            [0x459694, 0x4596E0, 0x45999D, 0x459B2C, 0x459BE5, 0x459C23]
        )
        self.assertEqual(MOD.THEME_TRANSACTION_FLAG_VA, 0x4B9618)
        for _, _va, op in MOD.THEME_TRANSACTION_OPS:
            self.assertIn(struct.pack("<I", 0x4B9618), op)

    def test_corrupted_headers_rejected(self):
        a = fake_pe()
        a[0:2] = b"NO"
        with self.assertRaises(ValueError):
            MOD.PE32(a)
        b = fake_pe()
        b[0x84:0x86] = b"\x64\x86"
        with self.assertRaises(ValueError):
            MOD.PE32(b)
        c = fake_pe()
        c[0x1A8+12:0x1A8+16] = b"\xff\xff\xff\x7f"
        with self.assertRaises(ValueError):
            MOD.PE32(c)


if __name__ == "__main__":
    unittest.main()
