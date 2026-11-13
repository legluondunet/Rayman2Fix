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
        self.assertIn("event>=budget",section)
        self.assertIn("(slot==0u || slot==1u) ? 64u : 16u",section)
        self.assertIn("arg_zero=",section)
        self.assertIn("esc_down=%u",section)
        self.assertNotIn("f1_down=",section)
        self.assertNotIn("VK_F1",self.runtime)
        self.assertIn("sound_slot=",section)
        self.assertIn("global_pause_depth=",section)
        self.assertIn("inspect_audio_sound_context(arg)",section)
        self.assertIn("ReadProcessMemory(process,address,bytes,size,&got)",self.runtime)
        self.assertIn("kSoundTableSlots=64u",self.runtime)
        self.assertIn("kSoundTableRva=0xB8C98",self.runtime)
        self.assertIn("kPauseDepthRva=0xA3CC8",self.runtime)
        self.assertNotIn("MusicInPauseMenu",self.config)

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
