#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <BinaryData.h>

// ── ISO Drums Design System ───────────────────────────────────────────────────
// Single source of truth for colours, typography and component styling.
// Include this wherever you need the design system (editor + all dialogs).

namespace ISOPalette
{
    static const juce::Colour Bg       { 0xff09090b };   // deepest background
    static const juce::Colour Darkest  { 0xff131316 };   // darkest surface
    static const juce::Colour Dark     { 0xff232334 };   // dark (waveform bg, row bg)
    static const juce::Colour Surface  { 0xff131316 };   // surface (toolbars, panels)
    static const juce::Colour RowBg    { 0xff232334 };   // row/track background
    static const juce::Colour Hover    { 0xff424264 };   // hover/selected state
    static const juce::Colour Border   { 0xff2a2a2f };   // border
    static const juce::Colour Accent   { 0xffc9a96e };   // Plait gold
    static const juce::Colour Muted    { 0xff5a5a5e };   // text-muted
    static const juce::Colour MutedLt  { 0xff707094 };   // lighter muted (processing text)
    static const juce::Colour Text     { 0xffe8e4df };   // warm off-white
    static const juce::Colour TextDim  { 0xff8a8a8e };   // text-secondary
}

// ── Custom LookAndFeel ─────────────────────────────────────────────────────────
class ISOLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ISOLookAndFeel();

    // Buttons
    void drawButtonBackground(juce::Graphics&, juce::Button&,
                               const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool down) override;

    // Progress
    void drawProgressBar(juce::Graphics&, juce::ProgressBar&,
                         int w, int h, double progress, const juce::String&) override;

    // Toggle buttons
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool highlighted, bool down) override;

    // Text editor
    void fillTextEditorBackground(juce::Graphics&, int w, int h,
                                  juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int w, int h,
                               juce::TextEditor&) override;

    // ComboBox
    void drawComboBox(juce::Graphics&, int w, int h, bool down,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;

    // Typography
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    // Helper: return Inter at a given size (bold flag optional)
    static juce::Font font(float size, bool bold = false);

private:
    juce::Typeface::Ptr regularFace_;
    juce::Typeface::Ptr boldFace_;
    juce::Typeface::Ptr mediumFace_;
};
