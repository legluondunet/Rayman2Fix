"""The owner's optional audio-pause diagnosis must not alter normal builds."""
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]

class AudioTraceGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime=(ROOT/"src/runtime/runtime.cpp").read_text(encoding="utf-8")
        cls.cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        cls.config=(ROOT/"src/launcher/game_config.cpp").read_text(encoding="utf-8")

    def test_compilation_requires_explicit_audio_trace_build(self):
        self.assertRegex(self.cmake,r'option\(RAYMAN2FIX_AUDIO_TRACE\s+[^\n]+\s+OFF\)')
        self.assertIn("if(RAYMAN2FIX_AUDIO_TRACE)",self.cmake)
        self.assertIn("RAYMAN2FIX_AUDIO_TRACE=1",self.cmake)
        self.assertIn("#define RAYMAN2FIX_AUDIO_TRACE 0",self.runtime)

    def test_hook_is_compiled_out_of_normal_approved_camera_path(self):
        self.assertRegex(self.runtime,
            r'#if RAYMAN2FIX_AUDIO_TRACE\s+poll_audio_pause_trace\(\);\s+#endif')
        self.assertIn("#if RAYMAN2FIX_AUDIO_TRACE\nusing AudioPauseFn",self.runtime)
        self.assertIn('if(!g_log || InterlockedCompareExchange(',self.runtime)
        self.assertIn("if(n<=kAudioSlotCount)",self.runtime)
        self.assertIn("if(!current || current==hook)continue;",self.runtime)
        self.assertIn("!executable_page(current)",self.runtime)
        self.assertIn("InterlockedCompareExchangePointer(ptr,hook,current)==current",
                      self.runtime)

    def test_original_audio_function_always_called_unchanged(self):
        section=self.runtime.split("void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1]
        section=section.split("void __stdcall audio_pause_sxd(",1)[0]
        self.assertIn("if(original) original(arg);",section)
        self.assertIn("const bool sampled=g_log && event<budget;",section)
        self.assertIn("(slot==0u || slot==1u) ? 64u : 16u",section)
        self.assertIn("arg_zero=",section)
        self.assertIn("esc_down=%u",section)
        self.assertNotIn("f1_down=",section)
        self.assertNotIn("VK_F1",self.runtime)
        self.assertIn("sound_slot=",section)
        self.assertIn("sound_matches=",section)
        self.assertIn("audio_origin=%s",section)
        self.assertIn("audio_origin=inspect_audio_pause_origin(",section)
        self.assertIn("RUNTIME-AUDIO-CALLSTACK:",self.runtime)
        self.assertIn("kMaxStackWords=1024",self.runtime)
        self.assertIn("kWrapperPauseReturnRva=0x536F2",self.runtime)
        self.assertIn("kWrapperResumeReturnRva=0x53722",self.runtime)
        self.assertIn("caller==base+kScriptPauseReturnRva",self.runtime)
        self.assertIn("caller==base+kWindowsPauseReturnRva",self.runtime)
        self.assertIn("paired_callers=%u",self.runtime)
        self.assertIn("if(pairs!=1 || kind==5)",self.runtime)
        self.assertIn("kCodeStartRva=0x1000",self.runtime)
        self.assertIn("kCodeEndRva=0x9B1D0",self.runtime)
        self.assertIn("global_pause_depth=",section)
        self.assertIn("if(sampled) {",section)
        self.assertIn("source=inspect_audio_sound_context(arg);",section)
        self.assertLess(section.index("source=inspect_audio_sound_context(arg);"),
                        section.index("if(original) original(arg);"))
        self.assertLess(section.index("if(original) original(arg);"),
                        section.index("if(!sampled)return;"))
        self.assertIn("inspect_audio_sound_context(arg)",section)
        self.assertIn("ReadProcessMemory(process,address,bytes,size,&got)",self.runtime)
        self.assertIn("kSoundTableSlots=64u",self.runtime)
        self.assertIn("kSoundTableRva=0xB8C98",self.runtime)
        self.assertIn("kPauseDepthRva=0xA3CC8",self.runtime)
        self.assertIn("RUNTIME-AUDIO-STATE: t_ms=",self.runtime)
        self.assertIn("g_audio_last_depth.exchange(",self.runtime)
        self.assertIn("++out.match_count;",self.runtime)
        self.assertIn("kScriptPauseReturnRva=0x71E75",self.runtime)
        self.assertIn("kScriptResumeReturnRva=0x71E6C",self.runtime)
        self.assertIn("kWindowsPauseReturnRva=0x01CA1",self.runtime)
        self.assertIn("kWindowsResumeReturnRva=0x01CEE",self.runtime)
        self.assertIn("_AddressOfReturnAddress()",self.runtime)
        self.assertIn("case 1:return \"game-script-pause\"",self.runtime)
        self.assertIn("case 3:return \"windows-activation-pause\"",self.runtime)
        self.assertNotIn("MusicInPauseMenu",self.config)

    def test_native_sequence_slot_is_only_read_in_audio_research_build(self):
        # The original single-stream controller's sound-table index is
        # observed, not treated as an independently proven music identity.
        self.assertIn("struct AudioSequenceContext",self.runtime)
        self.assertIn("kSequenceActiveRva=0xB95B8",self.runtime)
        self.assertIn("kSequenceSlotRva=0xB95E8",self.runtime)
        self.assertIn("kSoundTableRva=0xB8C98",self.runtime)
        self.assertIn("decode_audio_nested_record_ref(nested_ptr,module)",self.runtime)
        self.assertIn("sequence_ref_class=%u",self.runtime)
        self.assertIn("sequence_same_handle=%u",self.runtime)
        self.assertIn("sequence=inspect_audio_sequence_context(arg);",self.runtime)
        section=self.runtime.split(
            "AudioSequenceContext inspect_audio_sequence_context(DWORD argument) {",1)[1]
        section=section.split("struct AudioSoundContext {",1)[0]
        self.assertIn("ReadProcessMemory(",section)
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertNotIn("InterlockedCompareExchangePointer(",section)
        self.assertIn("argument==handle",section)
        self.assertNotIn("MusicInPauseMenu",self.config)

    def test_sequence_state_follows_native_nonzero_guard(self):
        # The game's type-11 pause callback checks state != 0,
        # not state == 1. Earlier 'inactive' traces were ambiguous.
        self.assertIn("0x459C40 accepts any nonzero state", self.runtime)
        self.assertIn("sequence_state_class=%u", self.runtime)
        self.assertIn("out.state_class=active==0u ? 1u : (active==1u ? 2u : 3u)", self.runtime)
        self.assertIn("if(active==0u || !read_word(", self.runtime)
        self.assertNotIn("active!=1u ||", self.runtime)
        self.assertNotIn("MusicInPauseMenu", self.config)

    def test_theme_resource_and_nested_sxd_owner_is_bounded_and_read_only(self):
        # SND_E_TR_Theme controller handle comes from 0x459B3A and the
        # inner sound-table index from 0x459AF4. Never equate an SXD
        # handle with music unless the unique outer/inner link is proven.
        section=self.runtime.split(
            "AudioThemeOwnerContext inspect_audio_theme_owner(DWORD argument) {",1)[1]
        section=section.split("struct AudioSoundContext {",1)[0]
        self.assertIn("kControllerRva=0xB95B8",section)
        self.assertIn("kNestedSlotRva=0xB95E8",section)
        self.assertIn("kSoundTableRva=0xB8C98",section)
        self.assertIn("Record table[64]{}",section)
        self.assertIn("ReadProcessMemory(",section)
        self.assertIn("header[1]!=11u",section)
        self.assertIn("inner_header[1]!=1u",section)
        self.assertIn("out.outer_candidates!=1u",section)
        self.assertIn("out.inner_driver_matches==1u",section)
        self.assertIn("argument==inner_handle",section)
        self.assertIn("if(active==0u)return out;",section)
        self.assertIn("const auto ref=decode_audio_nested_record_ref(nested_ptr,module);",section)
        self.assertIn("const auto& inner=table[ref.table_index];",section)
        self.assertIn("table[i].words[5]==0u",section)
        self.assertIn("theme_owned_driver",section)
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertNotIn("VirtualProtect(",section)
        self.assertNotIn("original(",section)
        trace=self.runtime.split("void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1]
        trace=trace.split("void __stdcall audio_pause_sxd(",1)[0]
        self.assertIn("theme=inspect_audio_theme_owner(arg)",trace)
        self.assertIn("RUNTIME-AUDIO-THEME-OWNER:",trace)
        self.assertIn("(read-only; no music bypass)",trace)
        self.assertLess(trace.index("theme=inspect_audio_theme_owner(arg)"),
                        trace.index("if(original) original(arg);"))
        self.assertIn("if(original) original(arg);",trace)
        self.assertNotIn("MusicInPauseMenu",self.config)

    def test_theme_nested_ref_is_pointer_not_slot_number(self):
        # Native 0x4574E0 returns &sound_table[index], which 0x459AF4
        # stores at 0x4B95E8. The old probe compared the pointer to 64
        # and incorrectly reported inner_slot=-1 even for active Themes.
        section=self.runtime.split(
            "AudioNestedRecordRef decode_audio_nested_record_ref(",1)[1]
        section=section.split("struct AudioSequenceContext {",1)[0]
        self.assertIn("kSoundTableRva=0xB8C98",section)
        self.assertIn("kRecordSize=0x20",section)
        self.assertIn("kRecordCount=64",section)
        self.assertIn("address<base || address>=base+kRecordCount*kRecordSize",section)
        self.assertIn("(address-base)%kRecordSize!=0u",section)
        self.assertIn("(address-base)/kRecordSize",section)
        self.assertIn("value==0xFFFFFFFFu",section)
        self.assertNotIn("ReadProcessMemory(",section)
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertNotIn("slot>=64u",self.runtime)
        self.assertIn("inner_ref_class=%u",self.runtime)
        self.assertIn("sequence_ref_class=%u",self.runtime)
        self.assertNotIn("MusicInPauseMenu",self.config)

    def test_transition_play_to_pause_correlation_is_research_only(self):
        # _SND_fn_lPlayTransitionExSxd@20 has FIVE stdcall arguments.
        # Its driver handle may or may not be music: never suppress it.
        self.assertIn("kTransitionPlayExRva=0xB86F0",self.runtime)
        self.assertIn("using AudioPlayTransitionExFn=DWORD (__stdcall*)("
                      "DWORD,DWORD,DWORD,DWORD,DWORD);",self.runtime)
        self.assertIn("audio_play_transition_ex(DWORD a,DWORD b,DWORD c,DWORD d,DWORD e)",self.runtime)
        self.assertIn("original(a,b,c,d,e)",self.runtime)
        self.assertIn("return result;",self.runtime)
        self.assertIn("transition_last_play_match=%u",self.runtime)
        self.assertIn("arg==last_play",self.runtime)
        self.assertIn("event<16u",self.runtime)
        self.assertIn("InterlockedCompareExchangePointer(play_slot,hook,current)==current",self.runtime)
        self.assertIn("not music-identified",self.runtime)
        self.assertNotIn("MusicInPauseMenu",self.config)

    def test_transition_descriptor_identity_is_read_only_and_bounded(self):
        self.assertIn("g_last_transition_play_descriptor",self.runtime)
        self.assertIn("transition_desc_match=%u",self.runtime)
        self.assertIn("entries[i].words[1]==last_descriptor",self.runtime)
        self.assertIn("out.match_count!=1u",self.runtime)
        self.assertIn("descriptor_transport=%u",self.runtime)
        self.assertIn("descriptor_volume=%d",self.runtime)
        self.assertIn("event<16u",self.runtime)
        self.assertIn("ReadProcessMemory(",self.runtime)
        self.assertIn("0x4582A4",self.runtime)
        self.assertNotIn("MusicInPauseMenu",self.config)

    def test_transition_descriptor_content_comparison_is_read_only(self):
        # Snapshot four words at play time and emit equality bits only.
        # A matching transport or volume is not itself a music identity.
        self.assertIn("g_last_transition_desc_words[4]", self.runtime)
        self.assertIn("g_last_transition_desc_readable", self.runtime)
        self.assertIn("desc_words_readable=%u", self.runtime)
        self.assertIn("desc_words_equal_mask=%u", self.runtime)
        self.assertIn("current_words[k]==g_last_transition_desc_words[k].load(", self.runtime)
        self.assertIn("out.match_count!=1u", self.runtime)
        self.assertIn("safe_read(reinterpret_cast<const void*>(desc_addr)", self.runtime)
        self.assertIn("got==sizeof(words)", self.runtime)
        self.assertNotIn("MusicInPauseMenu", self.config)

    def test_descriptor_is_resource_id_not_pointer(self):
        # Source: ACP_Ray2 SND_tdstBlockResourceMem, matched by 0x453F90.
        # DWORD0=Id, DWORD1=eType, DWORD2=eStorage, DWORD3=volume byte.
        self.assertIn("The first DWORD is a PACKED RESOURCE ID", self.runtime)
        self.assertIn("desc_volume=%u", self.runtime)
        self.assertIn("play_volume=%u", self.runtime)
        self.assertIn("desc_resource_group=%d", self.runtime)
        self.assertIn("desc_resource_index=%d", self.runtime)
        self.assertIn("play_resource_group=%d", self.runtime)
        self.assertIn("play_resource_index=%d", self.runtime)
        self.assertIn("desc_resource_id_match=%u", self.runtime)
        self.assertIn("const DWORD now_id=current_words[0];", self.runtime)
        self.assertIn("const DWORD old_id=g_last_transition_desc_words[0].load(",self.runtime)
        self.assertIn("out.descriptor_bank_group=int(now_id>>16)", self.runtime)
        self.assertIn("out.descriptor_record_index=int(now_id&0xFFFFu)", self.runtime)
        self.assertIn("current_words[3]&0xFFu", self.runtime)
        self.assertIn("out.match_count!=1u", self.runtime)
        self.assertNotIn("BYTE old_bytes[16]{},new_bytes[16]{}", self.runtime)
        self.assertNotIn("desc_head_pair_readable=%u", self.runtime)
        self.assertNotIn("desc_head_contents_match=%u", self.runtime)
        self.assertNotIn("MusicInPauseMenu", self.config)

    def test_exact_owner_cd_slots_are_named_and_bounded(self):
        expected={
            "PauseSxd":"0xB8664", "ResumeSxd":"0xB8668",
            "PauseTransitionSxd":"0xB8708",
            "ResumeTransitionSxd":"0xB870C",
            "PauseMidi":"0xB87B8", "ResumeMidi":"0xB87BC",
            "PauseTransitionMidi":"0xB87F4",
            "ResumeTransitionMidi":"0xB87F0",
            "PauseCD":"0xB8834", "ResumeCD":"0xB883C",
            "StopSxd":"0xB8660", "StopTransitionSxd":"0xB8704",
            "SetSoundVolumeSxd":"0xB866C", "StopCD":"0xB8840",
        }
        for name,rva in expected.items():
            self.assertIn('{"'+name+'",'+rva+'}',self.runtime)
        self.assertIn("TimeDateStamp!=kOriginalTimestamp",self.runtime)
        self.assertIn("kAudioSlotCount=14",self.runtime)

if __name__=="__main__":
    unittest.main()
