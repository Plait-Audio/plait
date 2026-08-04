#include "ISODesign.h"

using namespace ISOPalette;

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool isPrimaryButton(const juce::Colour& bg)
{
    return bg.getSaturation() > 0.35f && bg.getBrightness() > 0.3f;
}

// ── Constructor ───────────────────────────────────────────────────────────────

ISOLookAndFeel::ISOLookAndFeel()
{
    // Load Inter from binary data (embedded by CMake)
    if (BinaryData::InterRegular_otf != nullptr)
        regularFace_ = juce::Typeface::createSystemTypefaceFor(
            BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
    if (BinaryData::InterBold_otf != nullptr)
        boldFace_ = juce::Typeface::createSystemTypefaceFor(
            BinaryData::InterBold_otf, BinaryData::InterBold_otfSize);
    if (BinaryData::InterMedium_otf != nullptr)
        mediumFace_ = juce::Typeface::createSystemTypefaceFor(
            BinaryData::InterMedium_otf, BinaryData::InterMedium_otfSize);

    // Default button colour semantics
    setColour(juce::TextButton::buttonColourId,  Surface);
    setColour(juce::TextButton::buttonOnColourId, Accent);
    setColour(juce::TextButton::textColourOffId,  Text);
    setColour(juce::TextButton::textColourOnId,   juce::Colours::white);

    // Toggle buttons
    setColour(juce::ToggleButton::textColourId,   Text);
    setColour(juce::ToggleButton::tickColourId,   Accent);

    // Text editors
    setColour(juce::TextEditor::backgroundColourId,     Surface);
    setColour(juce::TextEditor::textColourId,           Text);
    setColour(juce::TextEditor::outlineColourId,        Border);
    setColour(juce::TextEditor::focusedOutlineColourId, Accent);
    setColour(juce::TextEditor::highlightColourId,      Accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId,Text);

    // ComboBox
    setColour(juce::ComboBox::backgroundColourId, Surface);
    setColour(juce::ComboBox::textColourId,        Text);
    setColour(juce::ComboBox::outlineColourId,     Border);
    setColour(juce::ComboBox::arrowColourId,       Muted);
    setColour(juce::ComboBox::buttonColourId,      Surface);

    // Progress bar
    setColour(juce::ProgressBar::backgroundColourId, Surface);
    setColour(juce::ProgressBar::foregroundColourId,  Accent);

    // PopupMenu
    setColour(juce::PopupMenu::backgroundColourId,           Surface);
    setColour(juce::PopupMenu::textColourId,                 Text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Accent);
    setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    // Label
    setColour(juce::Label::textColourId, Text);

    // Window / dialog backgrounds
    setColour(juce::ResizableWindow::backgroundColourId, Dark);
    setColour(juce::DocumentWindow::textColourId, Text);
    setColour(juce::AlertWindow::backgroundColourId, Dark);
    setColour(juce::AlertWindow::textColourId, Text);
    setColour(juce::AlertWindow::outlineColourId, Border);
}

// ── Font static helper ────────────────────────────────────────────────────────

juce::Font ISOLookAndFeel::font(float size, bool bold)
{
    return bold ? juce::Font(size, juce::Font::bold)
                : juce::Font(size);
}

// ── Typeface routing ──────────────────────────────────────────────────────────

juce::Typeface::Ptr ISOLookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    if (f.isBold() && boldFace_ != nullptr)  return boldFace_;
    if (regularFace_ != nullptr)             return regularFace_;
    return LookAndFeel_V4::getTypefaceForFont(f);
}

// ── Button background ─────────────────────────────────────────────────────────

void ISOLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                                           const juce::Colour& bg,
                                           bool highlighted, bool down)
{
    if (bg.getAlpha() < 10)
        return;

    const auto bounds   = btn.getLocalBounds().toFloat().reduced(0.5f);
    const bool primary  = isPrimaryButton(bg) && btn.isEnabled();

    if (primary)
    {
        auto fill = down ? bg.darker(0.22f) : (highlighted ? bg.brighter(0.09f) : bg);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(juce::Colours::white.withAlpha(down ? 0.0f : 0.04f));
        g.fillRoundedRectangle(bounds.withHeight(bounds.getHeight() * 0.48f), 8.0f);
    }
    else
    {
        const auto fill = down ? Border : (highlighted ? Surface.brighter(0.25f) : Surface);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 8.0f);

        const auto border = !btn.isEnabled() ? Border.darker(0.3f)
                          : highlighted      ? Border.brighter(0.7f)
                                             : Border;
        g.setColour(border);
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    }
}

// ── Button text ───────────────────────────────────────────────────────────────

void ISOLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                                     bool /*highlighted*/, bool down)
{
    const auto bgCol = btn.findColour(juce::TextButton::buttonColourId);
    const bool prim  = isPrimaryButton(bgCol) && btn.isEnabled();

    juce::Colour col;
    if (!btn.isEnabled())
        col = Muted.withAlpha(0.45f);
    else if (prim)
        col = juce::Colour(0xff1a1a1f).withAlpha(down ? 0.7f : 1.0f);
    else
        col = Text.withAlpha(down ? 0.6f : 0.82f);

    g.setColour(col);
    g.setFont(font(prim ? 13.0f : 11.0f, true));
    g.drawText(btn.getButtonText(),
               btn.getLocalBounds(), juce::Justification::centred);
}

// ── Progress bar ──────────────────────────────────────────────────────────────

void ISOLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& /*bar*/,
                                      int w, int h, double progress, const juce::String&)
{
    const float fw = (float)w, fh = (float)h;
    const float p = (float)juce::jlimit(0.0, 1.0, progress);

    // Segmented dot-style progress bar
    const float dotW   = 4.f;
    const float dotH   = fh * 0.55f;
    const float gap    = 2.5f;
    const float startX = 0.f;
    const float dotY   = (fh - dotH) * 0.5f;
    const int   numDots = (int)((fw) / (dotW + gap));

    for (int i = 0; i < numDots; ++i)
    {
        float x = startX + (float)i * (dotW + gap);
        float frac = (float)i / (float)numDots;
        bool filled = frac < p;
        g.setColour(filled ? Accent.withAlpha(0.85f)
                           : juce::Colour(0xff424264).withAlpha(0.4f));
        g.fillRoundedRectangle(x, dotY, dotW, dotH, 1.f);
    }
}

// ── Toggle button ─────────────────────────────────────────────────────────────

void ISOLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                                       bool highlighted, bool /*down*/)
{
    const float boxW = 16.f, boxH = 16.f;
    const float bx   = 0.f;
    const float by   = (btn.getHeight() - boxH) * 0.5f;

    juce::Rectangle<float> box(bx, by, boxW, boxH);
    g.setColour(btn.getToggleState() ? Accent : Surface);
    g.fillRoundedRectangle(box, 3.f);
    g.setColour(highlighted ? Border.brighter(0.6f) : Border);
    g.drawRoundedRectangle(box.reduced(0.5f), 3.f, 1.f);

    if (btn.getToggleState())
    {
        // Checkmark
        juce::Path tick;
        tick.startNewSubPath(bx + 3.f, by + 8.f);
        tick.lineTo(bx + 6.5f, by + 12.f);
        tick.lineTo(bx + 13.f, by + 4.5f);
        g.setColour(juce::Colours::white);
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }

    g.setColour(btn.isEnabled() ? Text : Muted);
    g.setFont(font(11.5f));
    g.drawText(btn.getButtonText(),
               juce::Rectangle<int>((int)(boxW + 8.f), 0,
                                     btn.getWidth() - (int)(boxW + 8.f),
                                     btn.getHeight()),
               juce::Justification::centredLeft);
}

// ── Text editor ───────────────────────────────────────────────────────────────

void ISOLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h,
                                               juce::TextEditor&)
{
    g.setColour(Surface);
    g.fillRoundedRectangle(0.f, 0.f, (float)w, (float)h, 3.f);
}

void ISOLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h,
                                            juce::TextEditor& ed)
{
    const auto col = ed.hasKeyboardFocus(true) ? Accent : Border;
    g.setColour(col);
    g.drawRoundedRectangle(0.5f, 0.5f, (float)w - 1.f, (float)h - 1.f, 3.f, 1.f);
}

// ── ComboBox ──────────────────────────────────────────────────────────────────

void ISOLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool isDown,
                                   int /*bx*/, int /*by*/, int /*bw*/, int /*bh*/,
                                   juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.f, 0.f, (float)w, (float)h);
    g.setColour(isDown ? Surface.brighter(0.15f) : Surface);
    g.fillRoundedRectangle(bounds, 3.f);
    g.setColour(box.hasKeyboardFocus(true) ? Accent : Border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.f, 1.f);

    // Arrow
    const float arrowX = (float)w - 18.f;
    const float arrowY = (float)h * 0.5f - 3.f;
    juce::Path arrow;
    arrow.startNewSubPath(arrowX,        arrowY);
    arrow.lineTo(arrowX + 5.f,  arrowY + 5.f);
    arrow.lineTo(arrowX + 10.f, arrowY);
    g.setColour(Muted);
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
}

void ISOLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(Surface);
    g.setColour(Border);
    g.drawRect(0, 0, w, h, 1);
}

void ISOLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                        const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked,
                                        bool /*hasSubMenu*/,
                                        const juce::String& text,
                                        const juce::String& /*shortcutKeyText*/,
                                        const juce::Drawable* /*icon*/,
                                        const juce::Colour* /*textColour*/)
{
    if (isSeparator)
    {
        auto line = area.reduced(12, 0).withHeight(1).withY(area.getCentreY());
        g.setColour(Border);
        g.fillRect(line);
        return;
    }

    // Soft, inset rounded highlight instead of an edge-to-edge gold fill.
    if (isHighlighted && isActive)
    {
        g.setColour(Hover);
        g.fillRoundedRectangle(area.reduced(5, 2).toFloat(), 5.0f);
    }

    g.setColour(! isActive ? Muted : Text);
    g.setFont(font(12.f));
    g.drawText(text, area.reduced(17, 0).withTrimmedRight(22), juce::Justification::centredLeft);

    // Gold dot on the right when the item is ticked (e.g. the analytics toggle).
    if (isTicked)
    {
        const float d = 6.0f;
        g.setColour(Accent);
        g.fillEllipse((float) area.getRight() - 18.0f, area.getCentreY() - d * 0.5f, d, d);
    }
}

void ISOLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                               int /*standardHeight*/, int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 60;
        idealHeight = 11;
        return;
    }
    idealWidth  = (int) std::ceil (font(12.f).getStringWidthFloat(text)) + 56;
    idealHeight = 30;
}

int ISOLookAndFeel::getPopupMenuBorderSize()
{
    return 6;
}

// ── Tooltips ───────────────────────────────────────────────────────────────────
juce::Rectangle<int> ISOLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                      juce::Point<int> screenPos,
                                                      juce::Rectangle<int> parentArea)
{
    const auto f = font(11.5f);
    const int maxW = 300;

    juce::AttributedString s;
    s.append(tipText, f, ISOPalette::Text);
    juce::TextLayout tl;
    tl.createLayout(s, (float) maxW);

    const int w = juce::jmin(maxW, (int) std::ceil(tl.getWidth())) + 22;
    const int h = (int) std::ceil(tl.getHeight()) + 14;

    // Centered on the cursor, above the control (below only if there's no room).
    const int x = screenPos.x - w / 2;
    const int y = (screenPos.y - h - 14 >= parentArea.getY()) ? screenPos.y - h - 14
                                                              : screenPos.y + 22;

    return juce::Rectangle<int>(x, y, w, h).constrainedWithin(parentArea);
}

void ISOLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h);

    // The JUCE tooltip window is opaque, so every pixel MUST be painted or the
    // uncovered corners show through as a white box. Fill the whole area with the
    // app's deepest background (it disappears against the dark UI), then draw the
    // rounded chip on top. The window's own drop shadow provides the lift.
    g.fillAll(ISOPalette::Bg);

    g.setColour(ISOPalette::Darkest);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(ISOPalette::Border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    g.setColour(ISOPalette::Text);
    g.setFont(font(11.5f));
    g.drawFittedText(text, juce::Rectangle<int>(w, h).reduced(11, 6),
                     juce::Justification::centred, 3);
}
