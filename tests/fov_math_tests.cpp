#include "rayman2fix/fov_math.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool b, const char* msg) {
    if (!b) throw std::runtime_error(msg);
}
int main() {
    try {
        // In Rayman 2, alphaY is 0.75 of the reference horizontal angle.
        const float y = 1.2f * 0.75f;
        const auto a43 = r2fix::hor_plus_fov(y, 4.0f / 3.0f);
        const auto a169 = r2fix::hor_plus_fov(y, 16.0f / 9.0f);
        const auto a219 = r2fix::hor_plus_fov(y, 21.0f / 9.0f);
        check(std::abs(a43-1.2f) < 0.0001f,
              "reference 4:3 horizontal FOV unchanged");
        check(a169 > a43 && a219 > a169,
              "Hor+ widens visible horizontal coverage at 16:9 and 21:9");
        check(std::abs(std::tan(a169/2.0f)/std::tan(a43/2.0f) - (4.0f/3.0f))
              < 0.0001f, "horizontal tangent scales with aspect ratio");
        check(r2fix::hor_plus_fov(0.0f, 16.0f/9.0f) == 0.0f,
              "uninitialized camera angle not altered");
        check(r2fix::hor_plus_fov(y, 0.0f) == 0.0f,
              "invalid output aspect not altered");
        // Matrix of supported-or-plausible explicit resolutions; each
        // horizontal tangent must grow in direct proportion to output
        // aspect while preserving the original vertical camera input.
        struct AspectCase { unsigned width, height; };
        constexpr AspectCase cases[] = {
            {640,480},    // 4:3 minimum supported config
            {1024,768},   // 4:3 reference
            {1920,1200},  // 16:10
            {1920,1080},  // 16:9
            {2560,1440},  // 16:9 at a different pixel density
            {3440,1440},  // ultrawide (not precisely 21:9)
            {5120,1440}   // 32:9, mathematical coverage only
        };
        const float original_horizontal = y / 0.75f;
        float last_fov = 0.0f;
        for (const auto& c : cases) {
            const float aspect = static_cast<float>(c.width) / c.height;
            const float horizontal = r2fix::hor_plus_fov(y, aspect);
            check(horizontal > 0.0f && horizontal >= last_fov,
                  "Hor+ angle must increase monotonically with aspect");
            const float actual_tangent = std::tan(horizontal * 0.5f);
            const float original_tangent = std::tan(original_horizontal * 0.5f);
            check(std::abs(actual_tangent / original_tangent -
                           aspect / (4.0f/3.0f)) < 0.0002f,
                  "Hor+ horizontal tangent must scale with aspect");
            last_fov = horizontal;
        }
        // 5:4 and excessively wide configurations are NOT covered by
        // the current Hor+ contract, even though the Resolution parser
        // accepts some such width/height values.
        check(r2fix::hor_plus_fov(y, 1280.0f/1024.0f) == 0.0f,
              "5:4 is outside the current Hor+ coverage");
        check(r2fix::hor_plus_fov(y, 8192.0f/480.0f) == 0.0f,
              "extreme width must not be represented as corrected Hor+");

        std::cout << "Rayman2Fix Hor+ mathematical tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
