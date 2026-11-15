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
        cls.esc_music_header=(ROOT/"include/rayman2fix/esc_music_wine_prototype.hpp").read_text(encoding="utf-8")

    def test_menu_attempt_generation_is_not_original_adpcm_record_generation(self):
        # The owner-hash-verified APMMX DLL has two handle aliases:
        # a first-free slot after Stop, and in-place inactive record
        # reinitialization WITHOUT any original Stop call. Therefore
        # the current coordinator's generation is an ESC session token,
        # never proof of a physical music stream epoch.
        self.assertIn("Only an ESC script-attempt/session token",self.runtime)
        self.assertIn("NOT the original APMMX ADPCM",self.runtime)
        self.assertIn("record's creation/reinitialization generation",self.runtime)
        self.assertIn("without native Stop",self.runtime)
        self.assertIn("NOT an original ADPCM record epoch",self.esc_music_header)
        self.assertIn("IN PLACE without Stop",self.esc_music_header)
        self.assertIn("not a physical voice",self.esc_music_header)
        self.assertEqual(self.runtime.count("g_proto_generation.fetch_add("),1)
        original_pause=self.runtime.split("void __cdecl proto_script_pause() {",1)[1].split(
            "void __cdecl proto_script_resume() {",1)[0]
        self.assertIn("g_proto_generation.fetch_add(1",original_pause)
        self.assertNotIn("g_proto_generation.fetch_add(",self.runtime.split(
            "void proto_record_stop(",1)[1])
        # Never promote pending-target equivalence or the session
        # counter to a generation credential in the synthetic bridge.
        self.assertNotIn("native_pending_target_matches_outer",
                         self.esc_music_header)

    def test_compilation_requires_explicit_audio_trace_build(self):
        self.assertRegex(self.cmake,r'option\(RAYMAN2FIX_AUDIO_TRACE\s+[^\n]+\s+OFF\)')
        self.assertIn("if(RAYMAN2FIX_AUDIO_TRACE)",self.cmake)
        self.assertIn("RAYMAN2FIX_AUDIO_TRACE=1",self.cmake)
        self.assertIn("#define RAYMAN2FIX_AUDIO_TRACE 0",self.runtime)

    def test_research_trace_schema_is_identifiable(self):
        # A renamed, stale log must not be mistaken for a new Wine test.
        self.assertIn(
            "RUNTIME-AUDIO-TRACE: enabled=1 schema=2 inner_record_ref=checked-pointer",
            self.runtime)
        self.assertIn("sequence_ref_class=%u",self.runtime)
        self.assertIn("inner_ref_class=%u",self.runtime)
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_standard_audio_hooks_are_feature_gated_and_research_remains_separate(self):
        # The standard runtime has a real opt-in feature now. Research
        # stack/driver enumeration remains excluded without AUDIO_TRACE=ON.
        self.assertIn("#if RAYMAN2FIX_AUDIO_HOOKS\n    poll_audio_pause_trace();",self.runtime)
        self.assertIn("#if RAYMAN2FIX_ESC_MUSIC_ACTIVE\n    proto_menu_tick();",self.runtime)
        self.assertIn("#if RAYMAN2FIX_AUDIO_TRACE\n    //",self.runtime)
        self.assertIn("if(!g_proto_enabled.load(std::memory_order_acquire))return;",self.runtime)
        self.assertIn("if(i!=2u && i!=3u && i!=10u && i!=11u)continue;",self.runtime)
        self.assertIn("if(!current || current==hook)continue;",self.runtime)
        self.assertIn("!executable_page(current)",self.runtime)
        self.assertIn("InterlockedCompareExchangePointer(ptr,hook,current)==current",self.runtime)
        self.assertIn("RAYMAN2FIX_ESC_MUSIC_FEATURE=1",self.cmake)
        self.assertIn("RUNTIME-ESC-MUSIC: enabled=1",self.runtime)

    def test_predriver_feasibility_is_passive_and_logs_after_audio_dispatch(self):
        self.assertIn("struct ProtoPreDriverProbe",self.runtime)
        self.assertIn("RUNTIME-ESC-PREDRIVER-GATE:",self.runtime)
        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        self.assertIn("probe.active && g_proto_origin==proto::Origin::script_pause",callback)
        self.assertIn("if(original && forwarded)original(arg);",callback)
        self.assertIn("slot==2u",callback)
        # The preview is not an audio interception mode: never defer,
        # drop, replay, or synthesize driver calls based on its counters.
        wrapper=self.runtime.split("void __cdecl proto_script_pause() {",1)[1].split(
            "void __cdecl proto_script_resume() {",1)[0]
        self.assertIn("proto_call_original(0x536D0)",wrapper)
        self.assertIn("g_proto_predriver_report.ready=true",wrapper)
        self.assertNotIn('RUNTIME-ESC-PREDRIVER-GATE:',wrapper)
        tick=self.runtime.split("void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("RUNTIME-ESC-PREDRIVER-GATE:"))
        self.assertLess(tick.index("proto_deferred_safety();"),
                        tick.index("RUNTIME-ESC-PREDRIVER-GATE:"))
        self.assertIn("report=g_proto_predriver_report;",tick)
        self.assertIn("g_proto_predriver_report.ready=false;",tick)
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("esc_pre_driver_pause_tests",cmake)
        policy=(ROOT/"include/rayman2fix/esc_pre_driver_pause_policy.hpp").read_text(
            encoding="utf-8")
        self.assertIn("class Policy",policy)
        self.assertIn("kMenuConfirmationDeadlineMs=100u",policy)
        self.assertNotIn("windows.h",policy.lower())

    def test_predriver_resume_probe_does_not_change_native_driver_policy(self):
        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        # The probe records original callback order, including callbacks
        # that the independently tested music coordinator already suppresses.
        self.assertIn("resume_probe.active && g_proto_origin==proto::Origin::script_resume",callback)
        self.assertIn("resume_probe.first_slots[resume_probe.callback_count]=slot",callback)
        self.assertIn("if(slot==3u)",callback)
        self.assertIn("if(original && forwarded)original(arg);",callback)
        self.assertIn("++resume_probe.forwarded_count",callback)
        self.assertIn("++resume_probe.suppressed_count",callback)
        self.assertLess(callback.index("if(original && forwarded)original(arg);"),
                        callback.index("if(resume_probe.active && g_proto_origin==proto::Origin::script_resume &&"))
        wrapper=self.runtime.split(
            "void __cdecl proto_script_resume() {",1)[1].split(
            "void proto_flush_predriver_resume_report() {",1)[0]
        self.assertIn("proto_call_original(0x53700);",wrapper)
        self.assertIn("g_proto.end_script_resume();",wrapper)
        self.assertIn("g_proto_predriver_resume_report.ready=true;",wrapper)
        self.assertNotIn("log_line(",wrapper)
        self.assertNotIn("RUNTIME-ESC-PREDRIVER-RESUME:",wrapper)
        flush=self.runtime.split(
            "void proto_flush_predriver_resume_report() {",1)[1].split(
            "// Focus is a Win32",1)[0]
        self.assertIn("RUNTIME-ESC-PREDRIVER-RESUME:",flush)
        self.assertIn("g_proto_predriver_resume_report.ready=false;",flush)
        self.assertIn("native_forwarded=%u native_suppressed=%u",flush)
        # The normal render tick flushes after audio compensation even
        # if script Resume reset the music state machine to idle.
        self.assertIn("proto_menu_tick();",self.runtime)
        self.assertIn("proto_theme_transition_watch_tick();\n    proto_flush_predriver_resume_report();",self.runtime)
        self.assertIn("sample<16u",wrapper)
        self.assertNotIn("PauseTransitionSxd(",wrapper)
        self.assertNotIn("ResumeTransitionSxd(",wrapper)

    def test_native_play_stop_window_audit_is_read_only_and_never_authorizes_audio(self):
        # The owner observed live SXD generation changes with unchanged
        # driver handle while ESC was open. Counts alone cannot classify
        # a specific song, but can falsify a claimed quiet interval.
        wrapper=self.runtime.split(
            "void __cdecl proto_script_pause() {",1)[1].split(
            "void __cdecl proto_script_resume() {",1)[0]
        for k in ("native_stop_before=g_proto_native_stop_calls.load(",
                  "native_stop_after=g_proto_native_stop_calls.load(",
                  "native_play_before=g_transition_play_calls.load(",
                  "native_play_after=g_transition_play_calls.load("):
            self.assertIn(k,wrapper)
        self.assertIn("g_proto_predriver_report.native_stop_before=native_stop_before;",wrapper)
        self.assertIn("g_proto_predriver_report.native_play_after=native_play_after;",wrapper)
        self.assertIn("proto_call_original(0x536D0);",wrapper)
        self.assertNotIn("RUNTIME-ESC-PREDRIVER-WINDOW:",wrapper)

        stop=self.runtime.split(
            "void proto_record_stop(DWORD handle) {",1)[1].split(
            "// The original CD's four independent",1)[0]
        self.assertIn("g_proto_native_stop_calls.fetch_add(",stop)
        self.assertIn("g_proto.native_stop(handle);",stop)
        self.assertNotIn("return;",stop)
        self.assertNotIn("log_line(",stop)

        tick=self.runtime.split(
            "void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        self.assertIn("RUNTIME-ESC-PREDRIVER-WINDOW:",tick)
        self.assertIn("stop_during_wrapper=%u stop_after_wrapper=%u",tick)
        self.assertIn("play_during_wrapper=%u play_after_wrapper=%u",tick)
        self.assertIn("esc_actor_confirmed=%u",tick)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("RUNTIME-ESC-PREDRIVER-WINDOW:"))
        self.assertLess(tick.index("proto_deferred_safety();"),
                        tick.index("RUNTIME-ESC-PREDRIVER-WINDOW:"))
        self.assertIn("report.native_stop_after-report.native_stop_before",tick)
        self.assertIn("stop_tick-report.native_stop_after",tick)
        self.assertIn("report.native_play_after-report.native_play_before",tick)
        self.assertIn("play_tick-report.native_play_after",tick)

        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        self.assertIn("if(original && forwarded)original(arg);",callback)
        self.assertNotIn("g_proto_native_stop_calls.fetch_add(",callback)
        # The research-only policy must remain detached from the game.
        self.assertNotIn("pre_driver_pause::Policy",self.runtime)
        self.assertNotIn("RUNTIME-ESC-PREDRIVER-WINDOW:",wrapper)

    def test_adpcm_pause_snapshot_rechecks_active_and_pause_state(self):
        # The original active flag at record+0x28 is a better diagnostic
        # than source-only Play/Stop counts, but it is NOT an epoch token.
        sampler=self.runtime.split(
            "ProtoAdpcmVoiceSnapshot proto_sample_adpcm_voice_after_dispatch(DWORD ref) {",
            1)[1].split("void __cdecl proto_script_pause() {",1)[0]
        self.assertIn("int active=-1;",self.runtime)
        self.assertIn("record_address+0x28u",sampler)
        self.assertIn("&active,sizeof(active)",sampler)
        self.assertIn("&active_after,sizeof(active_after)",sampler)
        self.assertIn("active_after!=active",sampler)
        self.assertIn("pause_after!=pause",sampler)
        self.assertIn("record_after!=record || count_after!=count",sampler)
        self.assertIn("out.active=active==0u ? 0 : (active==1u ? 1 : 2);",sampler)
        self.assertIn("ABA",sampler)
        self.assertNotIn("TestIsPlayingSxd(",sampler)
        self.assertNotIn("ResumeTransitionSxd(",sampler)
        self.assertNotIn("PauseTransitionSxd(",sampler)
        self.assertNotIn("WriteProcessMemory(",sampler)

    def test_adpcm_resume_snapshot_is_after_original_audio_and_on_render_thread(self):
        resume=self.runtime.split("void __cdecl proto_script_resume() {",1)[1].split(
            "void proto_flush_predriver_resume_report() {",1)[0]
        flush=self.runtime.split("void proto_flush_predriver_resume_report() {",1)[1].split(
            "// Focus is a Win32",1)[0]
        self.assertIn("proto_call_original(0x53700);",resume)
        self.assertNotIn("proto_sample_adpcm_voice_after_dispatch(",resume)
        self.assertNotIn("proto_read_transition_type_post_wrapper(",resume)
        self.assertNotIn("RUNTIME-ESC-ADPCM-RESUME:",resume)
        self.assertIn("g_proto_predriver_resume_report.ready=false;",flush)
        self.assertIn("report.lifetime_paired &&",flush)
        self.assertIn("probe.resume_count==2u",flush)
        self.assertIn("probe.unrelated_slots==0u",flush)
        self.assertIn("probe.other_handle_count==0u",flush)
        self.assertIn("transition_type==4",flush)
        self.assertIn("proto_read_transition_type_post_wrapper(probe.first_handle)",flush)
        self.assertIn("proto_sample_adpcm_voice_after_dispatch(probe.first_handle)",flush)
        self.assertIn("RUNTIME-ESC-ADPCM-RESUME:",flush)
        self.assertIn("snapshot=post-native-resume",flush)
        self.assertIn("ABA not excluded",flush)
        self.assertNotIn("ReadProcessMemory(",flush)
        self.assertNotIn("TestIsPlayingSxd(",flush)
        self.assertNotIn("proto_dispatch(",flush)
        self.assertNotIn("VirtualProtect(",flush)
        self.assertNotIn("WriteProcessMemory(",flush)
        self.assertLess(flush.index("log_line(resource_line);"),
                        flush.index("RUNTIME-ESC-ADPCM-RESUME:"))

    def test_adpcm_open_and_close_samples_never_authorize_audio_actions(self):
        tick=self.runtime.split("void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        self.assertIn("RUNTIME-ESC-ADPCM-VOICE:",tick)
        self.assertIn("paused=%d active=%d other_gate=%d",tick)
        self.assertIn("ABA not excluded",tick)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("RUNTIME-ESC-ADPCM-VOICE:"))
        self.assertIn("proto_flush_predriver_resume_report();",self.runtime)
        # Neither source samples nor the model are credentials for
        # synthesizing audio on a recycled native handle.
        self.assertNotIn("if(adpcm_voice.sampled)",tick)
        self.assertNotIn("if(adpcm_voice.active)",tick)
        self.assertNotIn("if(adpcm_voice.pause)",tick)

    def test_predriver_lifetime_is_only_observational_and_requires_pairing(self):
        # Observe native Play/Stop across the whole ESC menu, in addition to
        # the wrapper-to-first-tick window. No same-thread/generation/overlap
        # proof means that a zero event count is NOT a valid measurement.
        self.assertIn("struct ProtoEscLifetimeStart",self.runtime)
        self.assertIn("thread_id=0;",self.runtime)
        self.assertIn("g_proto_esc_lifetime_start={",self.runtime)
        pause=self.runtime.split(
            "void __cdecl proto_script_pause() {",1)[1].split(
            "void __cdecl proto_script_resume() {",1)[0]
        self.assertIn("g_proto_esc_lifetime_start.valid",pause)
        self.assertIn("g_proto_esc_lifetime_start.overlap=true;",pause)
        self.assertIn("native_play_before,native_stop_before",pause)
        self.assertIn("proto_call_original(0x536D0)",pause)
        self.assertNotIn("RUNTIME-ESC-PREDRIVER-LIFETIME:",pause)

        resume=self.runtime.split(
            "void __cdecl proto_script_resume() {",1)[1].split(
            "void proto_flush_predriver_resume_report() {",1)[0]
        self.assertIn("lifetime=g_proto_esc_lifetime_start;",resume)
        self.assertIn("lifetime.thread_id==GetCurrentThreadId()",resume)
        self.assertIn("lifetime.generation==",resume)
        self.assertIn("!lifetime.overlap",resume)
        self.assertIn("g_proto.end_script_resume();",resume)
        self.assertIn("proto_call_original(0x53700)",resume)
        self.assertIn("play_pre-lifetime.play_before",resume)
        self.assertIn("stop_pre-lifetime.stop_before",resume)
        self.assertIn("play_post-play_pre",resume)
        self.assertIn("stop_post-stop_pre",resume)
        self.assertIn("if(paired)",resume)
        self.assertNotIn("log_line(",resume)

        flush=self.runtime.split(
            "void proto_flush_predriver_resume_report() {",1)[1].split(
            "// Focus is a Win32",1)[0]
        self.assertIn("RUNTIME-ESC-PREDRIVER-LIFETIME:",flush)
        self.assertIn("paired=%u overlap=%u",flush)
        self.assertIn("play_between=%u stop_between=%u",flush)
        self.assertIn("play_during_resume=%u stop_during_resume=%u",flush)
        self.assertNotIn("proto_dispatch(",flush)
        self.assertNotIn("ReadProcessMemory(",flush)
        self.assertNotIn("VirtualProtect(",flush)
        self.assertIn("proto_flush_predriver_resume_report();",self.runtime)
        # The separate policy model is never wired into production hooks.
        self.assertNotIn("pre_driver_pause::Policy",self.runtime)

    def test_sxd_lifetime_provenance_is_sampled_and_never_authorizes_audio(self):
        self.assertIn("struct ProtoEscLifetimeStart",self.runtime)
        self.assertIn("sxd_baseline_valid=false;",self.runtime)
        self.assertIn("unsigned sxd_changes=0;",self.runtime)
        self.assertIn("RUNTIME-ESC-SXD-LIFETIME:",self.runtime)
        observer=self.runtime.split(
            "void proto_note_sampled_sxd(const AudioThemeOwnerContext& owner,",1)[1].split(
            "void proto_theme_transition_watch_tick() {",1)[0]
        for token in (
            "if(!menu_visible || !g_log)return;",
            "owner.inner_bank_provenance.list_complete==1u",
            "owner.inner_bank_provenance.candidates==1u",
            "if(!sample.valid || sample.overlap)return;",
            "++sample.sxd_unknown;",
            "sample.sxd_first_bank=bank;",
            "sample.sxd_first_index=index;",
            "++sample.sxd_changes;",
            "++sample.sxd_samples;",
        ):
            self.assertIn(token,observer)
        for forbidden in (
            "proto_dispatch(", "original(", "VirtualProtect(", "ReadProcessMemory(",
            "proto::Command", "g_audio_original[", "log_line("
        ):
            self.assertNotIn(forbidden,observer)

        watcher=self.runtime.split(
            "void proto_theme_transition_watch_tick() {",1)[1].split(
            "void proto_menu_tick() {",1)[0]
        self.assertIn("proto_note_sampled_sxd(owner,menu!=0u);",watcher)
        self.assertLess(watcher.index("if(!watch.observe(snapshot,DWORD(now-origin)))return;"),
                        watcher.index("proto_note_sampled_sxd(owner,menu!=0u);"))
        self.assertIn("inspect_audio_theme_owner(0,true)",watcher)

        tick=self.runtime.split(
            "void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        self.assertIn("proto_note_sampled_sxd(owner,known && menu!=0 && actor_ok);",tick)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("proto_note_sampled_sxd(owner,known && menu!=0 && actor_ok);"))
        self.assertEqual(tick.count("inspect_audio_theme_owner(probe.first_handle,true)"),1)

        resume=self.runtime.split(
            "void __cdecl proto_script_resume() {",1)[1].split(
            "void proto_flush_predriver_resume_report() {",1)[0]
        self.assertIn("g_proto_predriver_resume_report.sxd_changes=",resume)
        self.assertIn("g_proto_predriver_resume_report.sxd_first_bank=",resume)
        self.assertIn("g_proto_predriver_resume_report.sxd_unknown=",resume)
        self.assertIn("if(paired)",resume)
        flush=self.runtime.split(
            "void proto_flush_predriver_resume_report() {",1)[1].split(
            "// Focus is a Win32",1)[0]
        self.assertIn("paired=%u sampled=%u",flush)
        self.assertIn("samples=%u changes=%u unknown=%u",flush)
        self.assertIn("report.play_between,report.stop_between",flush)
        self.assertNotIn("proto_dispatch(",flush)
        self.assertNotIn("ReadProcessMemory(",flush)
        self.assertNotIn("VirtualProtect(",flush)
        self.assertNotIn("pre_driver_pause::Policy",self.runtime)

    def test_exact_owner_adpcm_voice_probe_is_post_dispatch_readonly_and_bounded(self):
        # Original WAVMW transition type=4 -> APMMX ADPCM handle ->
        # 0xE4-stride validated voice. Sampling is NOT pause authorization.
        helper=self.runtime.split(
            "ProtoAdpcmVoiceSnapshot proto_sample_adpcm_voice_after_dispatch(DWORD ref) {",
            1)[1].split("void __cdecl proto_script_pause() {",1)[0]
        for fragment in (
            'GetModuleHandleW(L"WAVMWbvr.dll")',
            'GetModuleHandleW(L"APMMXbvr.dll")',
            "0x38076FB4u,0x87000u",
            "0x38076F98u,0xEE000u",
            "0xC7E0u", "0x54D0u", "0x26A9Cu",
            "std::memcmp(", "ReadProcessMemory(", "transition[0]!=4u",
            "transition[1]", "0x24EC4u", "count>30u",
            "pool_rva=0xE67D8u", "record_stride=0xE4u",
            "record_address+0xD4u", "recorded_handle!=handle",
            "record_address+0x1Cu", "record_address+0xDCu",
            "record_after!=record || count_after!=count",
            "out.sampled=1", "out.pause=pause==0u",
        ):
            self.assertIn(fragment,helper)
        for forbidden in (
            "LoadLibrary", "WriteProcessMemory(", "VirtualProtect(",
            "log_line(", "proto_dispatch(", "original(",
            "g_proto.quarantine(", "g_proto.native_resume(", "Sleep(",
        ):
            self.assertNotIn(forbidden,helper)
        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        self.assertNotIn("proto_sample_adpcm_voice_after_dispatch(",callback)
        # The *separate research-only* RAYMAN2FIX_AUDIO_TRACE branch
        # already reads process memory in legacy callback diagnostics.
        # Standard opt-in production hooks must NOT add any such read.
        standard_callback=callback.split("#if RAYMAN2FIX_AUDIO_TRACE",1)[0]
        self.assertNotIn("ReadProcessMemory(",standard_callback)
        pause=self.runtime.split(
            "void __cdecl proto_script_pause() {",1)[1].split(
            "void __cdecl proto_script_resume() {",1)[0]
        self.assertIn("proto_call_original(0x536D0);",pause)
        self.assertIn("if(predriver_trace)",pause)  # first 16 eligible sources
        self.assertNotIn("proto_sample_adpcm_voice_after_dispatch(",pause)
        tick=self.runtime.split(
            "void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        self.assertIn("RUNTIME-ESC-ADPCM-VOICE:",tick)
        self.assertIn("report.transition_type_post_pause==4",tick)
        self.assertIn("proto_sample_adpcm_voice_after_dispatch(probe.first_handle)",tick)
        self.assertIn("snapshot=post-dispatch",tick)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("proto_sample_adpcm_voice_after_dispatch("))
        self.assertLess(tick.index("proto_deferred_safety();"),
                        tick.index("proto_sample_adpcm_voice_after_dispatch("))
        self.assertNotIn("g_proto_predriver_report.adpcm_voice_sampled",self.runtime)

    def test_transition_object_type_probe_is_post_native_bounded_and_passive(self):
        # The owner's SHA-pinned WAVMWbvr PE has a tagged transition
        # object, not a proven physical waveOut voice handle. Test the
        # 1/2/4 categorization without *any* memory read in audio callbacks.
        helper=self.runtime.split(
            "int proto_read_transition_type_post_wrapper(DWORD object_ref) {",1)[1].split(
            "void __cdecl proto_script_pause() {",1)[0]
        for token in (
            "if(object_ref<0x10000u || object_ref==0xffffffffu)return -1;",
            "ReadProcessMemory(", "GetCurrentProcess()",
            "read==sizeof(type)", "type==1u || type==2u || type==4u",
            "return -1", ": 0;",
        ):
            self.assertIn(token,helper)
        self.assertNotIn("log_line(",helper)
        self.assertNotIn("VirtualProtect(",helper)
        self.assertNotIn("WriteProcessMemory(",helper)
        self.assertNotIn("proto_dispatch(",helper)
        self.assertNotIn("fn(",helper)

        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        self.assertNotIn("proto_read_transition_type_post_wrapper(",callback)

        pause=self.runtime.split(
            "void __cdecl proto_script_pause() {",1)[1].split(
            "void __cdecl proto_script_resume() {",1)[0]
        self.assertIn("transition_type_post_pause=-1;",pause)
        self.assertIn("proto_call_original(0x536D0);",pause)
        self.assertIn("g_proto_predriver_probe.active=false;",pause)
        self.assertIn("g_proto_predriver_probe.pause_count==2u",pause)
        self.assertIn("g_proto_predriver_probe.other_handle_count==0u",pause)
        self.assertIn("transition_type_post_pause=proto_read_transition_type_post_wrapper(",pause)
        self.assertLess(pause.index("proto_call_original(0x536D0);"),
                        pause.index("transition_type_post_pause=proto_read_transition_type_post_wrapper("))
        self.assertNotIn("log_line(",pause)
        self.assertNotIn("VirtualProtect(",pause)
        self.assertIn("if(predriver_trace)",pause)  # max 16 sampled sources

        tick=self.runtime.split(
            "void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        self.assertIn("transition_type_post_pause=%d",tick)
        self.assertIn("report.transition_type_post_pause",tick)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("transition_type_post_pause=%d"))
        self.assertNotIn("proto_read_transition_type_post_wrapper(",tick)

    def test_hash_guided_adpcm_delegate_resolution_is_passive_and_post_dispatch(self):
        # The owner-observed Transition type=4 is ADPCM, whose original
        # exported Pause/Resume/Stop thunks point to separately loaded DLL
        # callbacks. The diagnostic reads the original indirection table
        # only with the exact 1999 PE version and opcode fingerprint.
        bridge=self.runtime.split(
            "void log_adpcm_delegate_origins_readonly() {",1)[1].split(
            "// Owner's exact CD executable",1)[0]
        for token in (
            "g_audio_original[2u].load(",
            "GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS",
            "GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT",
            'L"WAVMWbvr.dll"', "CompareStringOrdinal(",
            "origin!=reinterpret_cast<std::uintptr_t>(base)+0xC7E0u",
            "nt.FileHeader.TimeDateStamp!=0x38076FB4u",
            "nt.OptionalHeader.SizeOfImage!=0x87000u",
            "expected_pause[7]", "std::memcmp(",
            "0x26A98u,0x26A9Cu,0x26AA0u,0x26A00u,0x26A14u",
            "0x269FCu,0x26A88u",
            "0x3410u,0x3B60u",
            '"set-param","set-param-transition"',
            "0x3C50u,0x3C80u,0x3CB0u,0x3440u,0x3530u",
            '"stop","pause","resume","test-is-playing","get-pos"',
            "status=unresolved", "status=fallback",
            "status=resolved", "callback_rva=0x%08X",
            "ReadProcessMemory(", "executable_page(",
        ):
            self.assertIn(token,bridge)
        for banned in ("LoadLibrary", "GetProcAddress(", "VirtualProtect(",
                       "WriteProcessMemory(", "InterlockedCompareExchangePointer(",
                       "original(", "fn(", "Sleep(", "proto_dispatch("):
            self.assertNotIn(banned,bridge)
        # Nothing in native callbacks reads these indirect pointers.
        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        self.assertNotIn("log_adpcm_delegate_origins_readonly()",callback)
        self.assertNotIn("0x26A9C",callback)

        tick=self.runtime.split(
            "void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        self.assertIn("report.transition_type_post_pause==4",tick)
        self.assertIn("static bool adpcm_targets_logged_pending=false",tick)
        self.assertIn("static bool adpcm_targets_logged_synced=false",tick)
        self.assertIn("report.transition_type_post_pause==4",tick)
        self.assertIn("cls==3 && !adpcm_targets_logged_pending",tick)
        self.assertIn("cls==1 && !adpcm_targets_logged_synced",tick)
        self.assertIn("RUNTIME-AUDIO-ADPCM-DELEGATE-CONTEXT:",tick)
        self.assertIn("stage=%s",tick)
        self.assertEqual(tick.count("log_adpcm_delegate_origins_readonly();"),1)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("log_adpcm_delegate_origins_readonly();"))
        self.assertLess(tick.index("proto_deferred_safety();"),
                        tick.index("log_adpcm_delegate_origins_readonly();"))

    def test_five_adpcm_export_entry_fingerprints_are_read_only_and_fail_closed(self):
        body=self.runtime.split(
            "void log_adpcm_delegate_origins_readonly() {",1)[1].split(
            "// Owner's exact CD executable",1)[0]
        for token in (
            "constexpr unsigned owner_rvas[7]",
            "0x54C0u,0x54D0u,0x54E0u,0x5150u,0x51A0u",
            "constexpr BYTE owner_entry[7][16]",
            'L"APMMXbvr.dll"',
            "target_rva==owner_rvas[i]",
            "original_nt.FileHeader.TimeDateStamp==0x38076F98u",
            "original_nt.OptionalHeader.SizeOfImage==0xEE000u",
            "original_dos.e_lfanew<0x1000",
            "read(owner_base+owner_rvas[i],actual_entry,sizeof(actual_entry))",
            "std::memcmp(actual_entry,owner_entry[i],sizeof(actual_entry))==0",
            "original_entry_fingerprint=%u",
            "NOT full SHA",
            "pointer is not invocation",
        ):
            self.assertIn(token,body)
        self.assertIn("bool owner_fingerprint=false;",body)
        self.assertIn("owner_fingerprint=true;",body)
        self.assertLess(body.index("target_rva==owner_rvas[i]"),
                        body.index("owner_fingerprint=true;"))
        self.assertNotIn("GetProcAddress(",body)
        self.assertNotIn("LoadLibrary",body)
        self.assertNotIn("VirtualProtect(",body)
        self.assertNotIn("WriteProcessMemory(",body)
        self.assertNotIn("proto_dispatch(",body)
        self.assertNotIn("original(",body)

    def test_original_adpcm_five_entry_call_targets_are_consistent(self):
        # Validate exact original 16-byte opcode signatures embedded in
        # the read-only diagnostic against their original E8 rel32 targets.
        # The game or DLL is never loaded by this synthetic source check.
        code=self.runtime.split("constexpr BYTE owner_entry[7][16]{",1)[1].split(
            "};",1)[0]
        entries=re.findall(r"[{]([^}]*)[}]",code)
        self.assertEqual(len(entries),7)
        original_rvas=(0x54C0,0x54D0,0x54E0,0x5150,0x51A0)
        targets=(0x3340,0x3380,0x33C0,0x2900,0x2A00)
        for rva, destination, row in zip(original_rvas,targets,entries[:5]):
            with self.subTest(rva=hex(rva)):
                raw=bytes(int(byte,16) for byte in
                          re.findall(r"0x([0-9A-Fa-f]{2})",row))
                self.assertEqual(len(raw),16)
                self.assertEqual(raw[:6],
                                 bytes((0x8B,0x44,0x24,0x04,0x50,0xE8)))
                relative=int.from_bytes(raw[6:10],'little',signed=True)
                self.assertEqual(rva+10+relative,destination)
                self.assertEqual(raw[10:],bytes((0x83,0xC4,0x04,0xC2,0x04,0x00)))

    def test_original_setparam_export_stubs_use_guarded_double_arg_signature(self):
        body=self.runtime.split(
            "void log_adpcm_delegate_origins_readonly() {",1)[1].split(
            "// Owner's exact CD executable",1)[0]
        code=body.split("constexpr BYTE owner_entry[7][16]{",1)[1].split("};",1)[0]
        entries=re.findall(r"[{]([^}]*)[}]",code)
        self.assertEqual(len(entries),7)
        for idx,rva,rel_lo,rel_hi in ((5,0x5120,0x8E,0xE9),
                                      (6,0x5430,0x7E,0xE6)):
            with self.subTest(rva=hex(rva)):
                row=bytes(int(b,16) for b in
                          re.findall(r"0x([0-9A-Fa-f]{2})",entries[idx]))
                self.assertEqual(len(row),16)
                self.assertEqual(row[:13],bytes((0x8B,0x4C,0x24,0x08,
                    0x83,0xEC,0x08,0x8D,0x44,0x24,0x00,0x50,0x51)))
                self.assertEqual(row[13:],bytes((0xE8,rel_lo,rel_hi)))
        self.assertIn("0x5120u,0x5430u",body)
        self.assertIn("0x269FCu,0x26A88u",body)
        self.assertIn("0x3410u,0x3B60u",body)
        self.assertIn('"set-param","set-param-transition"',body)
        self.assertIn("for(unsigned i=0;i<7u;++i)",body)
        for banned in ("GetProcAddress(", "LoadLibrary", "WriteProcessMemory(",
                       "VirtualProtect(", "proto_dispatch(", "original("):
            self.assertNotIn(banned,body)

    def test_adpcm_query_delegate_readout_has_no_new_native_side_effects(self):
        body=self.runtime.split("void log_adpcm_delegate_origins_readonly() {",1)[1].split(
            "// Owner's exact CD executable",1)[0]
        # The two NEW read-only query pointers (not functions invoked).
        self.assertIn("0x26A00u,0x26A14u",body)
        self.assertIn("0x3440u,0x3530u",body)
        self.assertIn('"test-is-playing","get-pos"',body)
        self.assertIn("for(unsigned i=0;i<7u;++i)",body)
        self.assertIn("ReadProcessMemory(",body)
        self.assertIn("status=fallback",body)
        self.assertIn("status=unresolved",body)
        self.assertIn("status=resolved",body)
        self.assertIn("callback_rva=0x%08X",body)
        # Exact original driver native queries have side effects:
        # never call them or acquire any extra driver hooks.
        for prohibited in (
            "GetProcAddress(", "LoadLibrary", "TestIsPlaying(", "GetPos(",
            "VirtualProtect(", "WriteProcessMemory(", "InterlockedExchange",
            "InterlockedCompareExchange", "proto_dispatch(",
        ):
            self.assertNotIn(prohibited,body)

    def test_delegate_pre_post_pair_is_source_qualified_and_bounded(self):
        tick=self.runtime.split("void proto_menu_tick() {",1)[1].split(
            "#endif // RAYMAN2FIX_ESC_MUSIC_ACTIVE",1)[0]
        section=tick.split("// Compare native delegate provenance at most twice:",1)[1].split(
            "    }\n    if(phase==proto::Phase::pending_menu",1)[0]
        self.assertIn("owner.native_theme_player_ref_class",section)
        self.assertIn("report.transition_type_post_pause==4",section)
        self.assertIn("adpcm_targets_logged_pending=true",section)
        self.assertIn("adpcm_targets_logged_synced=true",section)
        self.assertIn('stage=%s',section)
        self.assertIn("resolution is NOT invocation",section)
        self.assertEqual(section.count("log_adpcm_delegate_origins_readonly();"),1)
        self.assertNotIn("TestIsPlaying(",section)
        self.assertNotIn("GetPos(",section)
        self.assertNotIn("audio_trace_forward(",section)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index("log_adpcm_delegate_origins_readonly();"))
        self.assertLess(tick.index("proto_deferred_safety();"),
                        tick.index("log_adpcm_delegate_origins_readonly();"))

    def test_intro_theme_controller_watcher_is_change_triggered_and_bounded(self):
        self.assertIn("void proto_theme_transition_watch_tick()",self.runtime)
        start=self.runtime.index("void proto_theme_transition_watch_tick()")
        end=self.runtime.index("void proto_menu_tick()",start)
        watcher=self.runtime[start:end]
        self.assertIn("if(last_sample && DWORD(now-last_sample)<40u)return;",watcher)
        self.assertIn("proto::ThemeRefWatchPolicy::kWindowMs",watcher)
        self.assertIn("proto::ThemeRefWatchInput snapshot",watcher)
        self.assertIn("read_native(0xB8C98,records,sizeof(records))",watcher)
        self.assertIn("decode_audio_nested_record_ref(nested,module)",watcher)
        self.assertIn("record.words[1]",watcher)
        self.assertIn("controller_words[3],controller_words[5]",watcher)
        self.assertIn("nested,outer_descriptor,inner_descriptor,menu",watcher)
        self.assertIn("if(!watch.observe(snapshot,DWORD(now-origin)))return;",watcher)
        self.assertIn("outer_descriptor_ref==other.outer_descriptor_ref",self.esc_music_header)
        self.assertIn("inner_descriptor_ref==other.inner_descriptor_ref",self.esc_music_header)
        self.assertIn("controller_field0c==other.controller_field0c",self.esc_music_header)
        # The menu byte is useful to display, but it must not exhaust
        # the 24-event Theme monitor budget on repeat ESC toggles.
        header_body=self.esc_music_header.split(
            "bool same(const ThemeRefWatchInput& other) const noexcept {",1)[1].split("}",1)[0]
        self.assertNotIn("menu_flag==other.menu_flag",header_body)
        self.assertIn("inspect_audio_theme_owner(0,true)",watcher)
        self.assertIn("RUNTIME-ESC-THEME-WATCH:",watcher)
        self.assertIn("alt_same_bank=%d",watcher)
        self.assertIn("inner_index=%d",watcher)
        self.assertNotIn("fn(action.handle)",watcher)
        self.assertNotIn("proto_dispatch(",watcher)
        self.assertNotIn("VirtualProtect(",watcher)
        self.assertIn("proto_menu_tick();\n    // Never defer native ESC compensation behind diagnostic disk I/O.\n    proto_theme_transition_watch_tick();",self.runtime)
        self.assertIn("static constexpr unsigned kMaxEvents=48u",
                      self.esc_music_header)
        self.assertIn("static constexpr std::uint32_t kWindowMs=120000u",
                      self.esc_music_header)

    def test_esc_transition_latency_and_first_rejection_are_observable_without_bypass(self):
        # The approved paired driver calls and strict current-player check
        # remain unchanged. A menu opening should not wait behind an
        # unnecessary 75ms poll or a synchronous verdict file write.
        start=self.runtime.index("void proto_menu_tick() {")
        end=self.runtime.index("void audio_trace_forward(",start)
        tick=self.runtime[start:end]
        self.assertIn("proto_game_read(0xB9BC8,menu_quick)",tick)
        self.assertIn("menu_quick==0u",tick)
        self.assertIn("proto_identity(handle,generation)",tick)
        self.assertIn("g_proto.owner_same(live)",tick)
        self.assertIn("current=actor_ok && g_proto.owner_same(live)",tick)
        self.assertIn("proto_esc_menu_actor_open()",tick)
        self.assertIn("RUNTIME-ESC-PERF:",tick)
        self.assertIn('"RUNTIME-ESC-PERF: t_ms=%lu pending_ms=%lu',tick)
        self.assertIn("RUNTIME-ESC-OWNER-DIAG:",self.runtime)
        self.assertIn("native_player_ref_match=%d",self.runtime)
        self.assertIn("native_player_ref_class=%d",self.runtime)
        self.assertIn("alt_ref_bank=%d",self.runtime)
        self.assertIn("alt_ref_index=%d",self.runtime)
        self.assertIn("alt_ref_same_bank=%d",self.runtime)
        self.assertIn("alt_ref_complete=%u",self.runtime)
        self.assertIn("alt_ref_candidates=%u",self.runtime)
        self.assertIn("const auto alternative=inspect_audio_native_bank_origin(",self.runtime)
        self.assertIn("alternative.candidates==1u",self.runtime)
        self.assertIn("alternative.list_complete==1u",self.runtime)
        self.assertIn("alternative.bank_id==",self.runtime)
        self.assertIn("if(classify_native_ref)",self.runtime)

        self.assertIn("native_theme_player_ref_class=0",self.runtime)
        self.assertIn("native_theme_player_ref_class=1",self.runtime)
        self.assertIn("native_theme_player_ref_class=2",self.runtime)
        self.assertIn("native_theme_player_ref_class=3",self.runtime)
        self.assertIn("native_theme_player_ref_class=4",self.runtime)
        self.assertIn("native_theme_player_ref_class=5",self.runtime)
        self.assertIn("classify_native_ref=false",self.runtime)
        self.assertIn('proto_log_owner_rejection(handle,DWORD(now-started),"accepted")',self.runtime)

        self.assertIn("outer_bank_complete=%u",self.runtime)
        self.assertIn("inner_bank_complete=%u",self.runtime)
        self.assertLess(tick.index("proto_dispatch(action);"),
                        tick.index('proto_log("menu-verdict"'))
        self.assertIn("proto_deferred_safety();",tick)
        self.assertIn("first_owner_rejection_logged",tick)
        self.assertNotIn("await-live-theme-ref",tick)
        # No guessing from the keyboard alone, no new synthetic calls:
        self.assertIn("paired-calls=2",self.runtime)

    def test_menu_and_engine_pause_probe_is_read_only_and_research_only(self):
        # A future optional observation must distinguish the native menu
        # boolean from the engine pause flag. Neither proves ESC vs F1.
        section=self.runtime.split("void poll_audio_pause_trace() {",1)[1]
        section=section.split("#endif // RAYMAN2FIX_AUDIO_TRACE",1)[0]
        self.assertIn("module+0xB9BC8", section)
        self.assertIn("module+0x100640", section)
        self.assertIn("SizeOfImage>0x100641u", section)
        self.assertIn("ReadProcessMemory(", section)
        self.assertIn("menu_read==sizeof(in_game_menu)",section)
        self.assertIn("engine_read==sizeof(engine_paused)",section)
        self.assertIn("in_game_menu<=1u && engine_paused<=1u",section)
        self.assertIn("g_audio_last_ingame_menu.exchange(",section)
        self.assertIn("g_audio_last_engine_pause.exchange(",section)
        self.assertIn("RUNTIME-AUDIO-UI-STATE: t_ms=",section)
        self.assertIn("not ESC visibility proof",section)
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_original_audio_function_always_called_unchanged(self):
        section=self.runtime.split("void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1]
        section=section.split("void __stdcall audio_pause_sxd(",1)[0]
        self.assertIn("if(original) original(arg);",section)
        self.assertIn("const bool sampled=g_log && event<budget;",section)
        self.assertIn("(slot==0u || slot==1u) ? 64u : 16u",section)
        self.assertIn("arg_zero=",section)
        self.assertIn("esc_down=%u",section)
        self.assertNotIn("f1_down=",section)
        self.assertNotIn("VK_F1",section)  # F1 is observed only by the separate research UI probe
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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_call_time_menu_flags_are_only_read_before_native_forward(self):
        section=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1]
        section=section.split("void __stdcall audio_pause_sxd(",1)[0]
        self.assertIn("ui_in_game_menu_at_call=%d",section)
        self.assertIn("ui_engine_paused_at_call=%d",section)
        self.assertIn("in_game_menu_at_call=-1,engine_paused_at_call=-1",section)
        self.assertIn("game_module+0xB9BC8",section)
        self.assertIn("game_module+0x100640",section)
        self.assertIn("menu_read==sizeof(menu_byte)",section)
        self.assertIn("pause_read==sizeof(paused_byte)",section)
        self.assertIn("menu_byte<=1u",section)
        self.assertIn("paused_byte<=1u",section)
        self.assertIn("if(sampled) {",section)
        self.assertLess(section.index("game_module+0xB9BC8"),
                        section.index("if(original) original(arg);"))
        self.assertLess(section.index("game_module+0x100640"),
                        section.index("if(original) original(arg);"))
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertNotIn("VK_F1",section)
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_sequence_state_follows_native_nonzero_guard(self):
        # The game's type-11 pause callback checks state != 0,
        # not state == 1. Earlier 'inactive' traces were ambiguous.
        self.assertIn("0x459C40 accepts any nonzero state", self.runtime)
        self.assertIn("sequence_state_class=%u", self.runtime)
        self.assertIn("out.state_class=active==0u ? 1u : (active==1u ? 2u : 3u)", self.runtime)
        self.assertIn("if(active==0u || !read_word(", self.runtime)
        self.assertNotIn("active!=1u ||", self.runtime)
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_theme_resource_and_nested_sxd_owner_is_bounded_and_read_only(self):
        # SND_E_TR_Theme controller handle comes from 0x459B3A and the
        # inner sound-table index from 0x459AF4. Never equate an SXD
        # handle with music unless the unique outer/inner link is proven.
        section=self.runtime.split(
            "AudioThemeOwnerContext inspect_audio_theme_owner(\n    DWORD argument,bool classify_native_ref=false) {",1)[1]
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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_menu_actor_read_only_probe_is_bounded_and_fail_closed(self):
        section=self.runtime.split(
            "AudioMenuActorSnapshot inspect_audio_menu_actor_readonly()",1)[1]
        section=section.split("void poll_audio_pause_trace() {",1)[0]
        self.assertIn("YAM_GenMenuMap_I1", section)
        self.assertIn("kPersonalTypesRva=0x1013F8", section)
        self.assertIn("kDynamicWorldRva=0x100FD0", section)
        self.assertIn("kInactiveWorldRva=0x100FC4", section)
        self.assertIn("kMaxTypeNodes=4096", section)
        self.assertIn("kMaxActorNodes=4096", section)
        self.assertIn("elem[2]!=list_va", section)
        self.assertIn("if(type_node!=0u)return out;", section)
        self.assertIn("if(found>1u)return out;", section)
        self.assertIn("std::memcmp(name,kMenuPersonalType,sizeof(name))", section)
        self.assertIn("if(seen_type[j]==type_node)return out;", section)
        self.assertIn("if(seen[j]==child)return -1;", section)
        self.assertIn("so[6]!=previous || so[7]!=root", section)
        self.assertIn("if(so[0]==2u)", section)
        self.assertIn("std::uintptr_t(std_game)+8u", section)
        self.assertIn("if(out.dynamic_actor<0 || out.inactive_actor<0)", section)
        self.assertIn("ReadProcessMemory(", section)
        self.assertNotIn("WriteProcessMemory(", section)
        self.assertNotIn("VirtualProtect(", section)
        self.assertNotIn("InterlockedCompareExchangePointer(", section)
        self.assertNotIn("MusicInPauseMenu",section)

    def test_actor_sample_only_in_audio_research_on_native_ui_changes(self):
        poll=self.runtime.split("void poll_audio_pause_trace() {",1)[1]
        poll=poll.split("using AudioHook=",1)[0] if "using AudioHook=" in poll else poll
        self.assertIn("old_menu!=static_cast<int>(in_game_menu)",poll)
        self.assertIn("old_engine!=static_cast<int>(engine_paused)",poll)
        self.assertIn("inspect_audio_menu_actor_readonly()",poll)
        self.assertIn("RUNTIME-AUDIO-MENU-ACTOR:",poll)
        self.assertIn("actor candidate not proof of visible ESC menu",poll)
        self.assertIn("esc_down=%u",poll)
        self.assertLess(poll.index("old_menu!=static_cast<int>(in_game_menu)"),
                        poll.index("inspect_audio_menu_actor_readonly()"))
        self.assertNotIn("VK_F1",self.runtime)
        self.assertNotIn("f1_down=",self.runtime)
        # Scoped by #if RAYMAN2FIX_AUDIO_TRACE and follows existing checks.
        before=self.runtime.split(
            "AudioMenuActorSnapshot inspect_audio_menu_actor_readonly()",1)[0]
        self.assertIn("#if RAYMAN2FIX_AUDIO_TRACE",before)
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_current_native_theme_player_identity_is_a_readonly_gate(self):
        section=self.runtime.split(
            "AudioThemeOwnerContext inspect_audio_theme_owner(\n    DWORD argument,bool classify_native_ref=false) {",1)[1]
        section=section.split("struct AudioSoundContext {",1)[0]
        self.assertIn("kThemePlayerCurrentKeyRva=0xB95CC",section)
        self.assertIn("out.outer_bank_provenance=inspect_audio_native_bank_origin(",section)
        self.assertIn("out.outer_bank_provenance.bank_id>=0",section)
        self.assertIn("out.outer_bank_provenance.resource_index>=0",section)
        self.assertIn("native_theme_player_key_matches_outer",section)
        self.assertIn("current_theme_key==expected_key",section)
        self.assertIn("safe_read(module+kThemePlayerCurrentKeyRva",section)
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertNotIn("VirtualProtect(",section)
        self.assertLess(
            section.index("out.outer_bank_provenance=inspect_audio_native_bank_origin("),
            section.index("current_theme_key==expected_key"))
        self.assertIn("native_theme_player_key_matches_outer=%d", self.runtime)
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_audio_driver_module_probe_logs_only_basename_and_rva(self):
        section=self.runtime.split(
            "void log_audio_driver_module(unsigned slot,const void* current) {",1)[1]
        section=section.split("// Owner's exact CD executable",1)[0]
        self.assertIn("GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS",section)
        self.assertIn("GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT",section)
        self.assertIn("GetModuleFileNameW(",section)
        self.assertIn("beginning=wide+i+1",section)
        self.assertIn("callback_rva=0x%08X",section)
        self.assertIn("module basename only",section)
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertNotIn("VirtualProtect(",section)
        self.assertNotIn("LoadLibrary",section)
        self.assertNotIn("GetProcAddress",section.replace(
            "// DLL loading, no GetProcAddress, no game writes.", ""))
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)
        hook=self.runtime.split("void poll_audio_pause_trace() {",1)[1]
        self.assertIn("log_audio_driver_module(i,current);",hook)
        self.assertLess(hook.index("log_audio_driver_module(i,current);"),
                        hook.index("// Hook the five-argument transition"))

    def test_standard_native_sxd_backend_is_logged_from_actual_callback_module(self):
        # A configured Snd_Cpa.ini default is not necessarily the
        # loaded driver: CPA can choose tested fallbacks. Standard mode
        # must report the module which actually owns the original
        # native SXD callbacks (with no LoadLibrary or other side effects).
        hook=self.runtime.split("void poll_audio_pause_trace() {",1)[1].split(
            "#endif // RAYMAN2FIX_AUDIO_HOOKS",1)[0]
        research,standard=hook.split(
            "#if RAYMAN2FIX_AUDIO_TRACE\n                char line[140]{}",1)[1].split(
            "#else\n",1)
        standard=standard.split("#endif",1)[0]
        self.assertIn('proto_log("driver-hook-installed",kAudioSlots[i].name);',standard)
        self.assertIn('log_audio_driver_module(i,current);',standard)
        self.assertLess(standard.index('proto_log("driver-hook-installed"'),
                        standard.index('log_audio_driver_module(i,current);'))
        self.assertNotIn("LoadLibrary",standard)
        self.assertNotIn("GetProcAddress",standard)
        self.assertNotIn("Sleep(",standard)
        self.assertNotIn("VirtualProtect",standard)

        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        self.assertNotIn("log_audio_driver_module(",callback)
        probe=self.runtime.split(
            "void log_audio_driver_module(unsigned slot,const void* current) {",1)[1].split(
            "// Owner's exact CD executable",1)[0]
        self.assertIn("GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS",probe)
        self.assertIn("GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT",probe)
        self.assertIn("GetModuleFileNameW(h,wide,MAX_PATH)",probe)
        self.assertIn("basename,rva",probe)
        self.assertNotIn("GetProcAddress(",probe)
        self.assertNotIn("LoadLibrary",probe)
        self.assertNotIn("VirtualProtect(",probe)
        self.assertNotIn("std::filesystem",probe)

    def test_cd_native_pending_theme_target_is_read_only_and_not_audio_authorization(self):
        # The exact hash-approved CD EXE sets controller+0x0C=1 and
        # stores the target descriptor in +0x44, then later promotes
        # it into +0x14. Observe descriptor equality, never assume
        # the active ADPCM voice belongs to a safe generation.
        self.assertIn("int native_theme_pending_state=-1;",self.runtime)
        self.assertIn("int native_pending_target_matches_outer=-1;",self.runtime)
        self.assertIn("int native_pending_target_differs_current=-1;",self.runtime)
        observer=self.runtime.split(
            "AudioThemeOwnerContext inspect_audio_theme_owner(",1)[1].split(
            "struct AudioSoundContext {",1)[0]
        for token in (
            "out.outer_candidates!=1u",
            "out.outer_bank_provenance=inspect_audio_native_bank_origin(",
            "if(classify_native_ref)",
            "safe_read(module+kControllerRva+0x0Cu,",
            "safe_read(module+kControllerRva+0x44u,",
            "switch_flag<=1u",
            "if(switch_flag==1u && pending_target>=0x10000u)",
            "out.native_theme_pending_state=int(switch_flag)",
            "out.native_pending_target_matches_outer=int(",
            "std::uintptr_t(pending_target)==",
            "outer_descriptor_address",
            "out.native_pending_target_differs_current=int(",
            "pending_target!=current_theme_key",
        ):
            self.assertIn(token,observer)
        for forbidden in (
            "WriteProcessMemory(", "VirtualProtect(", "LoadLibrary",
            "proto_dispatch(", "proto_allow_resume(", "g_proto.resume(",
        ):
            self.assertNotIn(forbidden,observer)
        identity=self.runtime.split(
            "proto::Identity proto_identity(DWORD handle,",1)[1].split(
            "// Called on the original game thread",1)[0]
        self.assertIn("owner.native_theme_player_ref_matches_outer!=1",identity)
        self.assertNotIn("native_pending_target_matches_outer",identity)
        self.assertNotIn("native_theme_pending_state",identity)
        self.assertIn("pending_target_matches_outer=%d",self.runtime)
        self.assertIn("pending_target_differs_current=%d",self.runtime)
        # Redundant startup .text scan is removed now that we have the
        # owner's exact SHA-matched original executable to disassemble.
        self.assertNotIn("proto_audit_controller_code_references()",self.runtime)
        self.assertNotIn("RUNTIME-ESC-CONTROLLER-XREF:",self.runtime)

    def test_cpa_ini_audit_reads_only_original_game_config_at_initialization(self):
        audit=self.runtime.split("void log_cpa_sound_configuration() {",1)[1].split(
            "bool game_window_is_foreground() {",1)[0]
        self.assertIn('L"dll\\\\snd_cpa.ini"',audit)
        self.assertIn("GetModuleFileNameW(nullptr,path,MAX_PATH)",audit)
        self.assertIn("GetFileAttributesW(path)",audit)
        self.assertIn("FILE_ATTRIBUTE_DIRECTORY",audit)
        self.assertIn("FILE_ATTRIBUTE_REPARSE_POINT",audit)
        self.assertIn("status=unavailable",audit)
        self.assertIn("GetPrivateProfileStringW(",audit)
        self.assertIn('L"DLL_Sxd",L"Unconditionnal"',audit)
        self.assertIn('L"DLL_Sxd",L"Default"',audit)
        self.assertIn('L"DLL_Sxd",L"File0"',audit)
        self.assertIn('L"DLL_Sxd",L"File1"',audit)
        self.assertIn('L"DLL_Adpcm",L"Default"',audit)
        self.assertIn("RUNTIME-AUDIO-CONFIG:",audit)
        self.assertIn("configuration candidates only; original DLL unchanged",audit)
        self.assertIn("end+1-path",audit)  # include '\\0' in bound
        for forbidden in (
            "WritePrivateProfileString", "LoadLibrary", "GetProcAddress",
            "CreateFile", "WriteFile", "VirtualProtect", "RegSetValue",
        ):
            self.assertNotIn(forbidden,audit)
        init=self.runtime.split(
            "DWORD WINAPI Rayman2Fix_Initialize(void* input) {",1)[1].split(
            "BOOL APIENTRY DllMain(",1)[0]
        self.assertIn("log_cpa_sound_configuration();",init)
        self.assertLess(init.index("g_audio_origin.store(GetTickCount()"),
                        init.index("log_cpa_sound_configuration();"))
        self.assertLess(init.index("log_cpa_sound_configuration();"),
                        init.index("const DWORD camera_status = install_camera_hook();"))
        callback=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1].split(
            "void __stdcall audio_pause_sxd(",1)[0]
        self.assertNotIn("log_cpa_sound_configuration();",callback)
        self.assertNotIn("GetPrivateProfileStringW(",callback)

    def test_native_bank_provenance_scan_is_bounded_and_fail_closed(self):
        # The registry is a source of actual bank ID and table index,
        # unlike arbitrary high/low halves of the record's +0 Id field.
        section=self.runtime.split(
            "AudioNativeBankOrigin inspect_audio_native_bank_origin(",1)[1]
        section=section.split("// Read-only, source-backed link:",1)[0]
        self.assertIn("kBankListRva=0x114D50",section)
        self.assertIn("kMaxBankNodes=512",section)
        self.assertIn("kNativeResourceStride=92",section)
        self.assertIn("ReadProcessMemory(",section)
        self.assertIn("node=descriptor[15]",section)
        self.assertIn("const DWORD state=descriptor[8]",section)
        self.assertIn("const DWORD head_ptr=descriptor[9]",section)
        self.assertIn("const std::uintptr_t first=head[3]",section)
        self.assertIn("const DWORD count=head[4]",section)
        self.assertIn("difference%kNativeResourceStride==0u",section)
        self.assertIn("difference/kNativeResourceStride<count",section)
        self.assertIn("if(visited[k]==node)return fail_closed();",section)
        self.assertIn("out.list_complete=1u",section)
        self.assertIn("if(out.list_complete==0u || out.candidates!=1u)",section)
        self.assertIn("const auto fail_closed=[&]()",section)
        self.assertIn("return fail_closed();",section)
        # Only the fail_closed lambda and the final post-validation
        # return may publish "out"; every other return must scrub it.
        self.assertEqual(section.count("return out;"),2)
        self.assertNotIn("WriteProcessMemory(",section)
        self.assertNotIn("VirtualProtect(",section)
        self.assertNotIn("if(original)",section)
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_native_bank_provenance_attached_only_to_unique_theme_and_inner(self):
        owner=self.runtime.split(
            "AudioThemeOwnerContext inspect_audio_theme_owner(",1)[1]
        owner=owner.split("struct AudioSoundContext {",1)[0]
        self.assertIn("outer_descriptor_address=descriptor;",owner)
        self.assertIn("out.outer_bank_provenance=inspect_audio_native_bank_origin(",owner)
        self.assertIn("out.inner_bank_provenance=inspect_audio_native_bank_origin(inner_desc)",owner)
        self.assertLess(owner.index("out.outer_candidates!=1u"),
                        owner.index("out.outer_bank_provenance=inspect_audio_native_bank_origin("))
        self.assertLess(owner.index("inner_header[1]!=1u"),
                        owner.index("out.inner_bank_provenance=inspect_audio_native_bank_origin(inner_desc)"))
        trace=self.runtime.split(
            "void audio_trace_forward(unsigned slot,DWORD arg) {",1)[1]
        trace=trace.split("void __stdcall audio_pause_sxd(",1)[0]
        for field in ("outer_native_bank=%d", "outer_native_index=%d",
                      "outer_bank_candidates=%u",
                      "outer_bank_list_complete=%u",
                      "inner_native_bank=%d", "inner_native_index=%d",
                      "inner_bank_candidates=%u",
                      "inner_bank_list_complete=%u"):
            self.assertIn(field,trace)
        self.assertIn("Id halves NOT bank provenance",trace)
        self.assertLess(trace.index("theme=inspect_audio_theme_owner(arg)"),
                        trace.index("if(original) original(arg);"))
        self.assertIn("if(original) original(arg);",trace)

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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

    def test_descriptor_is_resource_id_not_pointer(self):
        # Source: ACP_Ray2 SND_tdstBlockResourceMem, matched by 0x453F90.
        # DWORD0=Id, DWORD1=eType, DWORD2=eStorage, DWORD3=volume byte.
        self.assertIn("The first DWORD is a native resource record Id", self.runtime)
        self.assertIn("its two halves need not equal the owning bank and table index", self.runtime)
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
        self.assertIn('return read_ini_flag(ini,"Features","MusicInPauseMenu")',self.config)

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


    def test_native_current_theme_reference_does_not_use_serialized_bank_key(self):
        # Old packed bank:index comparison is only a diagnostic. The
        # original 0x45997B compares a descriptor pointer instead.
        self.assertIn("native_theme_player_ref_matches_outer",self.runtime)
        self.assertIn("static_cast<DWORD>(outer_descriptor_address)",self.runtime)
        self.assertIn("owner.native_theme_player_ref_matches_outer!=1",self.runtime)
        self.assertIn("native_theme_player_ref_matches_outer=%d",self.runtime)
        self.assertIn("proto_esc_menu_actor_open()",self.runtime)
        self.assertIn("snapshot.dynamic_actor==1",self.runtime)
        self.assertIn("snapshot.inactive_actor==0",self.runtime)
        self.assertIn("DWORD(now-last_probe)<75u",self.runtime)


    def test_wine_esc_probe_allows_nested_depth_but_never_unverified_resume(self):
        # The first 2026-11-14 menu was entered at depth 1; later real
        # menu entries happened at depth 2. The original strict ==1 gate
        # rejected them, independent of whether their Theme was valid.
        self.assertIn("depth>=1u && depth<=8u",self.runtime)
        self.assertNotIn("proto_read_depth(depth) && depth==1",self.runtime)
        # A candidate may have a unique Theme owner while the native player
        # pointer is still transitional, but only exact player pointer
        # matches may cause an actual physical driver Resume.
        self.assertIn("proto_identity(handle,generation,false)",self.runtime)
        self.assertIn("bool require_current_theme=true",self.runtime)
        self.assertIn("owner.native_theme_player_ref_matches_outer!=1",self.runtime)
        self.assertIn("proto_identity(handle,generation)",self.runtime)
        self.assertIn("g_proto.owner_same(live)",self.runtime)
        self.assertIn("live.same_dispatch(action)",self.runtime)
        self.assertIn('proto_log("driver-command-rejected",',self.runtime)
        self.assertIn("!action.native_theme_key || !action.native_inner_key",self.runtime)
        # The scoped prototype source replaces broken return-stack inference
        # only when all four experimental source detours are installed.
        self.assertIn('audio_origin="game-script-pause"',self.runtime)
        self.assertIn('audio_origin="game-script-resume"',self.runtime)
        self.assertIn('audio_origin="windows-activation-pause"',self.runtime)
        self.assertIn('audio_origin="windows-activation-resume"',self.runtime)

    def test_focus_loss_diagnostic_uses_real_win32_source_callers(self):
        # Camera/UI callbacks can stop running while Wine is Alt+Tabbed,
        # and GetForegroundWindow() can be stale or map the Wine desktop.
        # Record verified native Win32 activation source events instead.
        self.assertIn("void proto_focus_trace(",self.runtime)
        self.assertIn("RUNTIME-ESC-FOCUS: t_ms=",self.runtime)
        self.assertIn('proto_focus_trace("native-focus-pause",d)',self.runtime)
        self.assertIn('proto_focus_trace("native-focus-resume",d)',self.runtime)
        self.assertIn("GetActiveWindow()!=nullptr",self.runtime)
        # Diagnostic must not become a new driver authorization source.
        self.assertIn("proto_dispatch(d);",self.runtime)

if __name__=="__main__":
    unittest.main()
