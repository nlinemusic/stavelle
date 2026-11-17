#ifndef FONTPROVIDERSTUB_H
#define FONTPROVIDERSTUB_H

#include "draw/ifontprovider.h"

namespace muse::draw {
class FontProviderStub : public IFontProvider
{
public:
    //! Jubelle: off by default (every glyph and text has no size, as in
    //! MuseScore's own Qt-free check). When on, music symbols get their real
    //! sizes from the fonts' SMuFL metadata (glyphBBoxes) and text an estimated
    //! size (about half an em per character), so layout measurements are
    //! meaningful. Used by the template layout tests.
    static void setRealisticMetrics(bool on);
    static bool realisticMetrics();

    int addSymbolFont(const String& family, const io::path_t& path) override;

    double lineSpacing(const Font& f) const override;
    double xHeight(const Font& f) const override;
    double height(const Font& f) const override;
    double capHeight(const Font& f) const override;
    double ascent(const Font& f) const override;
    double descent(const Font& f) const override;

    bool inFont(const Font& f, char32_t ucs4) const override;

    // Text
    double horizontalAdvance(const Font& f, const String& string) const override;
    double horizontalAdvance(const Font& f, char32_t ucs4) const override;

    RectF boundingRect(const Font& f, const String& string) const override;
    RectF boundingRect(const Font& f, char32_t ucs4) const override;

    RectF tightBoundingRect(const Font& f, const String& string) const override;
};
}

#endif // FONTPROVIDERSTUB_H
