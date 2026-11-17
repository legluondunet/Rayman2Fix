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
    // There is intentionally NO authorization to fake a synchronized
    // Theme/SXD identity: this gate only schedules existing checks.
}
