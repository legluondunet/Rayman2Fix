#include "rayman2fix/graphics_menu_info.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok, const char* desc) {
    if (!ok) throw std::runtime_error(desc);
}
}

int main() {
    using r2fix::make_graphics_menu_info;

    const auto hd = make_graphics_menu_info(1920,1080,false);
    check(hd.output_resolution()=="1920 x 1080","full desktop output");
    check(hd.native_glide_mode()=="1024 x 768 x 16","fixed original Glide mode");
    check(hd.actual_aspect_ratio()=="16:9","exact 16:9 output");
    check(std::string(hd.aspect_policy())=="Auto","automatic ratio policy");
    check(hd.renderer=="GameFrame / Glide2","renderer is not a GPU driver");
    check(hd.adapter_reported_by_win32=="Indisponible" &&
          hd.driver_reported_by_win32=="Indisponible",
          "do not invent absent hardware/driver identities");

    const auto classic=make_graphics_menu_info(1440,1080,true);
    check(classic.actual_aspect_ratio()=="4:3","classic 4:3 output");
    check(std::string(classic.aspect_policy())=="4:3","forced ratio setting");
    check(classic.native_glide_mode()==hd.native_glide_mode(),
          "internal Glide remains 1024x768x16 in any output aspect");

    const auto wide=make_graphics_menu_info(3440,1440,false);
    check(wide.actual_aspect_ratio()=="43:18",
          "do not mislabel 3440x1440 as the mathematically different 21:9");
    check(make_graphics_menu_info(1920,1200,false).actual_aspect_ratio()=="8:5",
          "exact 16:10 also reduces to equivalent 8:5");
    check(make_graphics_menu_info(3840,2160,false).actual_aspect_ratio()=="16:9",
          "ratio is independent of resolution scale");

    auto source=make_graphics_menu_info(1920,1080,false);
    source.adapter_reported_by_win32="AMD Radeon test adapter";
    source.driver_reported_by_win32="d3d9.dll (Win32-reported)";
    check(source.adapter_reported_by_win32=="AMD Radeon test adapter" &&
          source.driver_reported_by_win32=="d3d9.dll (Win32-reported)",
          "hardware data retains its explicit Win32 provenance");
    check(source.renderer=="GameFrame / Glide2",
          "a D3D9 query must never replace the actual game renderer");

    try {
        (void)make_graphics_menu_info(0,1080,false);
        throw std::runtime_error("must reject zero width");
    } catch (const std::invalid_argument&) {}
    try {
        (void)make_graphics_menu_info(1920,0,false);
        throw std::runtime_error("must reject zero height");
    } catch (const std::invalid_argument&) {}

    std::cout << "Graphics resolution read-only info model: OK\n";
    return 0;
}
