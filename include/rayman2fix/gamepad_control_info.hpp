#pragma once
// Presentation contract only: no SDL, Win32, rendering or native menu hooks.
#include "rayman2fix/gamepad_input_policy.hpp"
#include <array>
#include <string_view>

namespace r2fix {
enum class PadLabelFamily { generic, xbox, playstation };

struct PadControlRow {
    std::string_view action_key;
    std::string_view generic;
    std::string_view xbox;
    std::string_view playstation;
};

inline constexpr std::array<PadControlRow, 11> kPadControlRows{{
    {"movement", "Left stick", "Left stick", "Left stick"},
    {"jump", "South", "A", "Cross"},
    {"fire", "East", "B", "Circle"},
    {"camera_left", "West / left shoulder", "X / LB", "Square / L1"},
    {"camera_right", "North / right shoulder", "Y / RB", "Triangle / R1"},
    {"camera_rotation", "Right stick", "Right stick", "Right stick"},
    {"target", "Left trigger", "LT", "L2"},
    {"statistics", "Right trigger", "RT", "R2"},
    {"world_knowledge", "D-pad up", "D-pad up", "D-pad up"},
    {"murphy", "D-pad down", "D-pad down", "D-pad down"},
    {"pause", "Start", "Start", "Options"}
}};

constexpr std::string_view pad_control_label(const PadControlRow& row,
                                             PadLabelFamily family) noexcept {
    switch (family) {
        case PadLabelFamily::xbox: return row.xbox;
        case PadLabelFamily::playstation: return row.playstation;
        default: return row.generic;
    }
}

// The adapter supplies a confidently identified active family; never infer
// physical legends from an unverified device name. No controller means a
// generic reference layout, not a claim that a device is connected.
constexpr PadLabelFamily pad_display_family(bool connected,
                                            PadLabelFamily detected) noexcept {
    return connected ? detected : PadLabelFamily::generic;
}

constexpr std::string_view pad_back_label(PadLabelFamily family) noexcept {
    switch (family) {
        case PadLabelFamily::xbox: return "B";
        case PadLabelFamily::playstation: return "Circle";
        default: return "East";
    }
}
// Presentation strings authored for the five original PC languages.
// UTF-8 for interchange; a Windows renderer must convert to wide text
// rather than use the process ANSI code page. No native asset copying.
enum class PadInfoLocale : unsigned { en, fr, es, it, de };
struct PadInfoStrings {
    std::string_view no_pad, generic_pad, back, pending;
    std::array<std::string_view,11> actions;
    std::array<std::string_view,11> generic_controls;
    std::string_view left_stick, right_stick, up, down, generic_back;
};
inline constexpr std::array<PadInfoStrings,5> kPadInfoStrings{{
    {"No controller connected","Generic controller","Back","To be tested",
     {"Move","Jump / surface","Shoot","Camera left","Camera right","Camera rotation",
      "Target / dive","Statistics","World knowledge","Call Murfy","Pause"},
     {"Left stick","Bottom button","Right button","Left button / left shoulder",
      "Top button / right shoulder","Right stick","Left trigger","Right trigger",
      "D-pad up","D-pad down","Start"},
     "Left stick","Right stick","D-pad up","D-pad down","Right button"},
    {"Aucune manette connectée","Manette générique","Retour","À tester",
     {"Déplacement","Sauter / remonter","Tirer","Caméra gauche","Caméra droite","Rotation caméra",
      "Cibler / plonger","Statistiques","Connaissance du monde","Appeler Murfy","Pause"},
     {"Stick gauche","Bouton du bas","Bouton de droite","Bouton de gauche / gâchette haute gauche",
      "Bouton du haut / gâchette haute droite","Stick droit","Gâchette gauche","Gâchette droite",
      "Croix haut","Croix bas","Start"},
     "Stick gauche","Stick droit","Croix haut","Croix bas","Bouton de droite"},
    {"Mando no conectado","Mando genérico","Volver","Por probar",
     {"Mover","Saltar / subir","Disparar","Cámara izquierda","Cámara derecha","Girar cámara",
      "Apuntar / bucear","Estadísticas","Conocimiento del mundo","Llamar a Murfy","Pausa"},
     {"Stick izquierdo","Botón inferior","Botón derecho","Botón izquierdo / superior izquierdo",
      "Botón superior / superior derecho","Stick derecho","Gatillo izquierdo","Gatillo derecho",
      "Cruceta arriba","Cruceta abajo","Start"},
     "Stick izquierdo","Stick derecho","Cruceta arriba","Cruceta abajo","Botón derecho"},
    {"Nessun controller collegato","Controller generico","Indietro","Da provare",
     {"Movimento","Salta / risali","Spara","Telecamera a sinistra","Telecamera a destra","Ruota telecamera",
      "Mira / immergiti","Statistiche","Conoscenza del mondo","Chiama Murfy","Pausa"},
     {"Stick sinistro","Pulsante in basso","Pulsante destro","Pulsante sinistro / dorsale sinistro",
      "Pulsante in alto / dorsale destro","Stick destro","Grilletto sinistro","Grilletto destro",
      "Croce su","Croce giù","Start"},
     "Stick sinistro","Stick destro","Croce su","Croce giù","Pulsante destro"},
    {"Kein Controller verbunden","Generischer Controller","Zurück","Noch zu testen",
     {"Bewegen","Springen / auftauchen","Schießen","Kamera links","Kamera rechts","Kamera drehen",
      "Anvisieren / tauchen","Statistik","Weltwissen","Murfy rufen","Pause"},
     {"Linker Stick","Untere Taste","Rechte Taste","Linke Taste / linke Schultertaste",
      "Obere Taste / rechte Schultertaste","Rechter Stick","Linker Trigger","Rechter Trigger",
      "Steuerkreuz oben","Steuerkreuz unten","Start"},
     "Linker Stick","Rechter Stick","Steuerkreuz oben","Steuerkreuz unten","Rechte Taste"}
}};
constexpr const PadInfoStrings& pad_info_strings(PadInfoLocale locale) noexcept {
    const auto i=static_cast<unsigned>(locale);
    return kPadInfoStrings[i<kPadInfoStrings.size()?i:0];
}
struct PadInfoRowView {
    std::string_view action, control;
    bool pending;
};
struct PadInfoView {
    PadLabelFamily family;
    std::string_view status, back, back_control, pending_label;
    std::array<PadInfoRowView,11> rows;
    bool swim_pending;
};
constexpr PadInfoView make_pad_info_view(PadInfoLocale locale,bool connected,
                                        PadLabelFamily detected) noexcept {
    const auto& s=pad_info_strings(locale);
    const auto family=pad_display_family(connected,detected);
    PadInfoView out{family, !connected?s.no_pad:
        family==PadLabelFamily::xbox?"Xbox":
        family==PadLabelFamily::playstation?"PlayStation":s.generic_pad,
        s.back,family==PadLabelFamily::generic?s.generic_back:pad_back_label(family),
        s.pending,{},true};
    for(unsigned i=0;i<out.rows.size();++i) {
        auto control=family==PadLabelFamily::generic?s.generic_controls[i]:
            pad_control_label(kPadControlRows[i],family);
        if(i==0)control=s.left_stick;
        if(i==5)control=s.right_stick;
        if(i==8)control=s.up;
        if(i==9)control=s.down;
        // World-knowledge and the added swim routes are not hardware-tested.
        // A future page must show the pending notice, not imply validation.
        out.rows[i]={s.actions[i],control,i==8};
    }
    return out;
}
} // namespace r2fix
