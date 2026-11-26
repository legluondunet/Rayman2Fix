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
// Presentation strings authored for the original PC languages, including the Polish GOG edition.
// UTF-8 for interchange; a Windows renderer must convert to wide text
// rather than use the process ANSI code page. No native asset copying.
enum class PadInfoLocale : unsigned { en, fr, es, it, de, pl };
struct PadInfoStrings {
    std::string_view no_pad, generic_pad, back, pending;
    std::array<std::string_view,11> actions;
    std::array<std::string_view,11> generic_controls;
    std::string_view left_stick, right_stick, up, down, generic_back;
};
inline constexpr std::array<PadInfoStrings,6> kPadInfoStrings{{
    {"No controller connected","Generic controller","Back","To be tested",
     {"Move","Jump / surface","Shoot","Camera left","Camera right","Camera rotation",
      "Target / dive","Statistics","World knowledge","Call Murfy","Pause"},
     {"Left stick","Bottom button","Right button","Left button / left shoulder",
      "Top button / right shoulder","Right stick","Left trigger","Right trigger",
      "D-pad up","D-pad down","Start"},
     "Left stick","Right stick","D-pad up","D-pad down","Right button"},
    {u8"Aucune manette connect\u00e9e",u8"Manette g\u00e9n\u00e9rique","Retour",u8"\u00c0 tester",
     {u8"D\u00e9placement","Sauter / remonter","Tirer",u8"Cam\u00e9ra gauche",u8"Cam\u00e9ra droite",u8"Rotation cam\u00e9ra",
      "Cibler / plonger","Statistiques","Connaissance du monde","Appeler Murfy","Pause"},
     {"Stick gauche","Bouton du bas","Bouton de droite",u8"Bouton de gauche / g\u00e2chette haute gauche",
      u8"Bouton du haut / g\u00e2chette haute droite","Stick droit",u8"G\u00e2chette gauche",u8"G\u00e2chette droite",
      "Croix haut","Croix bas","Start"},
     "Stick gauche","Stick droit","Croix haut","Croix bas","Bouton de droite"},
    {"Mando no conectado",u8"Mando gen\u00e9rico","Volver","Por probar",
     {"Mover","Saltar / subir","Disparar",u8"C\u00e1mara izquierda",u8"C\u00e1mara derecha",u8"Girar c\u00e1mara",
      "Apuntar / bucear",u8"Estad\u00edsticas","Conocimiento del mundo","Llamar a Murfy","Pausa"},
     {"Stick izquierdo",u8"Bot\u00f3n inferior",u8"Bot\u00f3n derecho",u8"Bot\u00f3n izquierdo / superior izquierdo",
      u8"Bot\u00f3n superior / superior derecho","Stick derecho","Gatillo izquierdo","Gatillo derecho",
      "Cruceta arriba","Cruceta abajo","Start"},
     "Stick izquierdo","Stick derecho","Cruceta arriba","Cruceta abajo",u8"Bot\u00f3n derecho"},
    {"Nessun controller collegato","Controller generico","Indietro","Da provare",
     {"Movimento","Salta / risali","Spara","Telecamera a sinistra","Telecamera a destra","Ruota telecamera",
      "Mira / immergiti","Statistiche","Conoscenza del mondo","Chiama Murfy","Pausa"},
     {"Stick sinistro","Pulsante in basso","Pulsante destro","Pulsante sinistro / dorsale sinistro",
      "Pulsante in alto / dorsale destro","Stick destro","Grilletto sinistro","Grilletto destro",
      "Croce su",u8"Croce gi\u00f9","Start"},
     "Stick sinistro","Stick destro","Croce su",u8"Croce gi\u00f9","Pulsante destro"},
    {"Kein Controller verbunden","Generischer Controller",u8"Zur\u00fcck","Noch zu testen",
     {"Bewegen","Springen / auftauchen",u8"Schie\u00dfen","Kamera links","Kamera rechts","Kamera drehen",
      "Anvisieren / tauchen","Statistik","Weltwissen","Murfy rufen","Pause"},
     {"Linker Stick","Untere Taste","Rechte Taste","Linke Taste / linke Schultertaste",
      "Obere Taste / rechte Schultertaste","Rechter Stick","Linker Trigger","Rechter Trigger",
      "Steuerkreuz oben","Steuerkreuz unten","Start"},
     "Linker Stick","Rechter Stick","Steuerkreuz oben","Steuerkreuz unten","Rechte Taste"},
    {u8"Nie pod\u0142\u0105czono kontrolera","Kontroler uniwersalny",u8"Wr\u00f3\u0107","Do sprawdzenia",
     {"Ruch","Skok / wynurzanie","Strzelanie","Kamera w lewo","Kamera w prawo",u8"Obr\u00f3t kamery",
      "Celowanie / nurkowanie","Statystyki",u8"Wiedza o \u015bwiecie",u8"Wezwij Murfy\u2019ego","Pauza"},
     {u8"Lewy dr\u0105\u017cek","Dolny przycisk","Prawy przycisk","Lewy przycisk / lewy bumper",
      u8"G\u00f3rny przycisk / prawy bumper",u8"Prawy dr\u0105\u017cek","Lewy spust","Prawy spust",
      u8"Krzy\u017cak w g\u00f3r\u0119",u8"Krzy\u017cak w d\u00f3\u0142","Start"},
     u8"Lewy dr\u0105\u017cek",u8"Prawy dr\u0105\u017cek",u8"Krzy\u017cak w g\u00f3r\u0119",u8"Krzy\u017cak w d\u00f3\u0142","Prawy przycisk"}
}};
// Original game label, with explicit ASCII, CP1250 and UTF-8 Polish
// representations. Unknown native glyph encodings remain unrecognized.
constexpr PadInfoLocale pad_info_locale_from_label(std::string_view text) noexcept {
    if(text=="/C:LANGUE")return PadInfoLocale::fr;
    if(text=="/C:IDIOMA")return PadInfoLocale::es;
    if(text=="/C:LINGUA")return PadInfoLocale::it;
    if(text=="/C:SPRACHE")return PadInfoLocale::de;
    // Actual Polish GOG native label, owner log 2026-11-26:
    // 2F433A4AE45A594B. 0xE4 is the game's own glyph encoding.
    if(text=="/C:J\xe4ZYK" || text=="/C:JEZYK" || text=="/C:J\xcaZYK" ||
       text==u8"/C:J\u0118ZYK")return PadInfoLocale::pl;
    return PadInfoLocale::en;
}
// The Polish GOG release may use a proprietary glyph for the accented
// LANGUAGE label. Identify its unaccented native captions instead,
// within the active localization table only. Two distinct captions are
// required, so a single incidental string cannot switch the UI language.
constexpr unsigned pad_polish_caption_marker(std::string_view text) noexcept {
    if(text.size()>=3 && text.substr(0,3)=="/C:")text.remove_prefix(3);
    const auto equals_ascii=[&](std::string_view expected) {
        if(text.size()!=expected.size())return false;
        for(std::size_t i=0;i<text.size();++i) {
            char c=text[i];
            if(c>='a'&&c<='z')c=char(c-'a'+'A');
            if(c!=expected[i])return false;
        }
        return true;
    };
    return equals_ascii("STEROWANIE")?1u:
           equals_ascii("NOWA GRA")?2u:
           equals_ascii("OPCJE")?4u:0u;
}
constexpr bool pad_polish_caption_set(unsigned markers) noexcept {
    return (markers&1u)!=0 && (markers&6u)!=0;
}
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
        s.back,family==PadLabelFamily::generic?s.generic_back:
            family==PadLabelFamily::playstation&&locale==PadInfoLocale::fr?"Rond":
            family==PadLabelFamily::playstation&&locale==PadInfoLocale::pl?u8"K\u00f3\u0142ko":pad_back_label(family),
        s.pending,{},true};
    for(unsigned i=0;i<out.rows.size();++i) {
        auto control=family==PadLabelFamily::generic?s.generic_controls[i]:
            pad_control_label(kPadControlRows[i],family);
        if(family==PadLabelFamily::playstation && locale==PadInfoLocale::fr){
            if(i==1)control="Croix";
            if(i==2)control="Rond";
            if(i==3)control=u8"Carr\u00e9 / L1";
            if(i==4)control="Triangle / R1";
        }
        if(family==PadLabelFamily::playstation && locale==PadInfoLocale::pl){
            if(i==1)control=u8"Krzy\u017cyk";
            if(i==2)control=u8"K\u00f3\u0142ko";
            if(i==3)control="Kwadrat / L1";
            if(i==4)control=u8"Tr\u00f3jk\u0105t / R1";
        }
        if(i==0)control=s.left_stick;
        if(i==5)control=s.right_stick;
        if(i==8)control=s.up;
        if(i==9)control=s.down;
        // World-knowledge and the added swim routes are not hardware-tested.
        // Keep pending state for engineering validation; the approved
        // user-facing diagram no longer renders asterisks or a test notice.
        out.rows[i]={s.actions[i],control,i==8};
    }
    return out;
}
} // namespace r2fix
