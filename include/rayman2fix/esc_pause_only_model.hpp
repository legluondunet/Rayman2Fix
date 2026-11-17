#pragma once
// Independent research model for the owner's original 1999 APMMX type-4
// pause flag. This file is NOT linked into Rayman2FixRuntime and must not
// gate, intercept, delay, replay, or synthesize real driver calls.
// The owner-original APMMX 0x1EF0 and 0x1F30 operations assign 1 and 0
// respectively to record+0x1c after handle validation and driver locking.
#include <cstdint>

namespace r2fix::esc_pause_only_research {

enum class Evidence : std::uint8_t {
    stable_single_voice,
    unknown_handle_or_epoch,
    focus_or_nested_pause,
    native_voice_changed,
    unexpected_callback_sequence
};

struct Outcome {
    // Valid only in the explicitly closed, single-record flag model.
    bool model_applicable=false;
    bool paused_while_esc=false;
    bool paused_after_native_resume=false;
    bool both_pauses_suppressed=false;
    // By design this isolated model never authorizes a real driver hook.
    bool runtime_interception_authorized=false;
};

// Simulates exactly two original type-4 Pause writes followed by two
// original Resume writes to the SAME live ADPCM record, initially playing.
// Calling this function never touches audio, time, process memory or OS.
// skip_* is a hypothetical per-call immediate omission, NOT a deferred
// Pause or a request to synthesize a Resume. The model provides no
// proof of real physical record epoch or driver callback source.
constexpr Outcome evaluate(Evidence evidence,
                           bool initially_paused,
                           bool skip_first,
                           bool skip_second,
                           unsigned original_resume_count=2u) noexcept {
    Outcome result{};
    if (evidence!=Evidence::stable_single_voice || initially_paused ||
        original_resume_count!=2u)
        return result;
    result.model_applicable=true;
    result.both_pauses_suppressed=skip_first && skip_second;
    bool flag=initially_paused;
    if (!skip_first) flag=true;
    if (!skip_second) flag=true;
    result.paused_while_esc=flag;
    // Original idempotent APMMX Resume writes 0 without reading the
    // previous paused flag. No obligation to repay a skipped bit-write
    // exists in this *isolated stable-record model*, not in live audio.
    for (unsigned i=0;i<original_resume_count;++i) flag=false;
    result.paused_after_native_resume=flag;
    return result;
}

}  // namespace r2fix::esc_pause_only_research
