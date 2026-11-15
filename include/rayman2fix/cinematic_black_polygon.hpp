#pragma once
// SHA-locked Rayman 2 retail x86 GLI_DRV_vAddBlackPolygon guard.
//
// Confirmed by comparing game's VA 0x420827,0x420841,0x420858,0x420872
// assembly with user's 19:18 live driver logs:
// arguments are (x0,y0,x1,y1), NOT (top,left,bottom,right).
// Return RVA 0x2082D draws top full-width bars (0,0,1024,1..);
// return RVA 0x20847 draws lower bars (0,763..,1024,768).
// Return RVAs 0x2085E and 0x20878 are left/right side masks.
// Never suppress those, regardless of apparent geometry.
//
// Suppressing a polygon cannot restore pixels which the scene did not
// render. Keep opt-in and require visual validation.
namespace r2fix {
enum class CinematicBlackPolygon : unsigned {
    Preserve = 0, Upper = 1, Lower = 2
};

constexpr CinematicBlackPolygon classify_cinematic_black_polygon(
    unsigned return_rva,
    int x0,int y0,int x1,int y1,
    int viewport_width,int viewport_height) {
    // Exact supported internal Glide viewport and actual full-width
    // horizontally thin region. This is not a generic geometry-only
    // test: the native callsite must match as well.
    if(viewport_width!=1024 || viewport_height!=768 ||
       x0!=0 || x1!=viewport_width ||
       y0<0 || y1>viewport_height || y0>=y1)
        return CinematicBlackPolygon::Preserve;
    if(return_rva==0x0002082Du && y0==0 &&
       y1<=viewport_height/3)
        return CinematicBlackPolygon::Upper;
    if(return_rva==0x00020847u &&
       y1==viewport_height &&
       y0>=viewport_height*2/3)
        return CinematicBlackPolygon::Lower;
    return CinematicBlackPolygon::Preserve;
}
} // namespace r2fix
