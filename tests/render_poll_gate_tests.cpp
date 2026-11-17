#include "rayman2fix/render_poll_gate.hpp"
#include <cstdint>
#include <cstdlib>

#define CHECK(x) do { if(!(x)) std::abort(); } while(false)

int main() {
    using r2fix::RenderPollGate;
    using r2fix::RenderPollSource;
    RenderPollGate gate;
    constexpr std::uint32_t owner = 173u;
    // No callbacks are allowed to claim an arbitrary thread before a
    // validated native camera establishes its engine-thread identity.
    CHECK(!gate.take(RenderPollSource::Native2D,100,owner,0,0));
    CHECK(!gate.take(RenderPollSource::Viewport,100,174,owner,1));
    CHECK(gate.take(RenderPollSource::Camera,100,owner,owner,100));
    CHECK(!gate.take(RenderPollSource::Native2D,117,owner,owner,100));
    CHECK(!gate.take(RenderPollSource::Viewport,147,174,owner,100));
    CHECK(!gate.take(RenderPollSource::Native2D,147,owner,owner,100));
    // Camera has been absent for 48 ms; GLI viewport/2D callbacks
    // on the exact same thread can now service both the native menu
    // and the jail_20 input latch, without a timer or worker thread.
    CHECK(gate.take(RenderPollSource::Viewport,148,owner,owner,100));
    CHECK(!gate.take(RenderPollSource::Native2D,149,owner,owner,100));
    CHECK(!gate.take(RenderPollSource::Camera,158,owner,owner,158));
    CHECK(gate.take(RenderPollSource::Camera,160,owner,owner,160));
    CHECK(!gate.take(RenderPollSource::Native2D,180,owner,owner,175));
    CHECK(gate.take(RenderPollSource::Native2D,223,owner,owner,175));
    CHECK(!gate.take(RenderPollSource::Viewport,224,owner,owner,175));
    CHECK(gate.take(RenderPollSource::Viewport,240,owner,owner,175));

    // Clock wrapping is supported by unsigned TickCount arithmetic.
    RenderPollGate wrap;
    const std::uint32_t before = 0xfffffff0u;
    CHECK(wrap.take(RenderPollSource::Camera,before,owner,owner,before));
    CHECK(!wrap.take(RenderPollSource::Viewport,20,owner,owner,before));
    CHECK(wrap.take(RenderPollSource::Native2D,35,owner,owner,before));
    CHECK(!wrap.take(RenderPollSource::Viewport,36,owner,owner,before));
    CHECK(wrap.take(RenderPollSource::Viewport,51,owner,owner,before));

    // A cinematic with no 3D camera remains serviceable across long
    // gaps, if a verified same-thread GLI callback still arrives.
    RenderPollGate no_camera;
    CHECK(no_camera.take(RenderPollSource::Camera,1000,owner,owner,1000));
    CHECK(no_camera.take(RenderPollSource::Native2D,21000,owner,owner,1000));
    CHECK(no_camera.take(RenderPollSource::Viewport,21016,owner,owner,1000));
    CHECK(!no_camera.take(RenderPollSource::Native2D,21017,174,owner,1000));
    // Regression: the 4:3 jail_20 camera and native 2D/viewport callbacks
    // can all go quiet for ~75 s while the original game's GLI frame flip
    // continues to draw the cinematic. FrameFlip must sample the *same*
    // trusted game thread with a fresh key release + press.
    RenderPollGate long_intro;
    CHECK(long_intro.take(RenderPollSource::Camera,1000,owner,owner,1000));
    CHECK(!long_intro.take(RenderPollSource::FrameFlip,1030,owner,owner,1000));
    CHECK(!long_intro.take(RenderPollSource::FrameFlip,1048,174,owner,1000));
    CHECK(long_intro.take(RenderPollSource::FrameFlip,1048,owner,owner,1000));
    CHECK(!long_intro.take(RenderPollSource::FrameFlip,1054,owner,owner,1000));
    CHECK(long_intro.take(RenderPollSource::FrameFlip,1064,owner,owner,1000));
    CHECK(long_intro.take(RenderPollSource::FrameFlip,76000,owner,owner,1000));
    CHECK(!long_intro.take(RenderPollSource::Viewport,76001,owner,owner,1000));
    CHECK(long_intro.take(RenderPollSource::FrameFlip,76016,owner,owner,1000));
    CHECK(!long_intro.take(RenderPollSource::FrameFlip,76017,174,owner,1000));
    // Strict opt-in: keep frame-flip isolated to a running jail_20
    // scene or a source-scoped pending ESC-menu window. Merely
    // enabling MusicInPauseMenu is not an audio authorization.
    using r2fix::should_poll_game_frame_flip;
    CHECK(!should_poll_game_frame_flip(false,false,true,true));
    CHECK(should_poll_game_frame_flip(true,false,true,true));
    CHECK(should_poll_game_frame_flip(false,true,true,true));
    CHECK(should_poll_game_frame_flip(true,true,true,true));
    CHECK(!should_poll_game_frame_flip(false,true,false,true));
    CHECK(!should_poll_game_frame_flip(false,true,true,false));
    CHECK(!should_poll_game_frame_flip(true,false,false,false));
    CHECK(!should_poll_game_frame_flip(true,false,true,false));
    CHECK(!should_poll_game_frame_flip(false,false,false,false));

    // The native camera takes priority as soon as it resumes and may
    // suppress the flip callback even when that callback is same-thread.
    CHECK(long_intro.take(RenderPollSource::Camera,76100,owner,owner,76100));
    CHECK(!long_intro.take(RenderPollSource::FrameFlip,76120,owner,owner,76100));
    CHECK(long_intro.take(RenderPollSource::FrameFlip,76148,owner,owner,76100));

    // There is intentionally NO authorization to fake a synchronized
    // Theme/SXD identity: this gate only schedules existing checks.
}
