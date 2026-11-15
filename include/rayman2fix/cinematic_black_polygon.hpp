#pragma once
// Rayman 2 retail x86 GLI_DRV_vAddBlackPolygon boundary guard.
//
// The SHA-pinned original calls the GLI driver callback in this native
// source order: (top, left, bottom, right), with four callsites in
// GLI viewport update at VA 0x420827/841/858/872. Only the third and
// fourth sites draw FULL-WIDTH top/bottom masks. The first two draw
// left/right side masks and are never candidates for suppression.
//
// The viewport or contents behind the polygons are not modified.
// A skipped polygon can therefore reveal unpainted or cleared pixels.
// Treat as experimental; owner must confirm visually.
namespace r2fix {
enum class CinematicBlackPolygon : unsigned {
    Preserve = 0, Upper = 1, Lower = 2
};

constexpr CinematicBlackPolygon classify_cinematic_black_polygon(
    unsigned return_rva,
    int top, int left, int bottom, int right,
    int viewport_width, int viewport_height) {
    // The opt-in prototype is restricted to the project's pinned 1024×768
    // internal GLI mode. Do not guess at dimensions or use geometry alone.
    if (viewport_width != 1024 || viewport_height != 768 ||
        left != 0 || right != viewport_width)
        return CinematicBlackPolygon::Preserve;
    // Original caller VA 0x420858: push width, push top, push 0, push 0,
    // returns at 0x42085E. A narrow full-width upper mask.
    if (return_rva == 0x0002085Eu &&
        top == 0 && bottom > 0 &&
        bottom <= viewport_height / 3)
        return CinematicBlackPolygon::Upper;
    // Original caller VA 0x420872: push width, push height, push 0,
    // push bottom, returns at 0x420878. A narrow lower mask.
    if (return_rva == 0x00020878u &&
        bottom == viewport_height &&
        top >= viewport_height * 2 / 3 &&
        top < bottom)
        return CinematicBlackPolygon::Lower;
    return CinematicBlackPolygon::Preserve;
}
} // namespace r2fix
