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
        std::cout << "Rayman2Fix Hor+ mathematical tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
