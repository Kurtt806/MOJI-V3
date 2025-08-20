#include "text_fonts.h"

// Expanded list of fonts (pointers to u8g2 font symbols).
// Note: adding many fonts will increase the firmware binary size because
// the referenced font symbols must be pulled into the final binary.
const uint8_t *textFonts[] = {
    // very small / pixel fonts
    u8g2_font_4x6_tr,
    u8g2_font_5x8_tr,
    u8g2_font_6x10_tr,
    u8g2_font_6x12_tr,

    // common bitmap fonts
    u8g2_font_6x13B_tr,
    u8g2_font_7x13B_tr,
    u8g2_font_8x13B_tr,
    u8g2_font_9x15B_tr,
    u8g2_font_10x20_tr,

    // tiny / thumb fonts
    u8g2_font_tom_thumb_4x6_tr,

    // profont series
    u8g2_font_profont10_tr,
    u8g2_font_profont11_tr,
    u8g2_font_profont12_tr,
    u8g2_font_profont15_tr,
    u8g2_font_profont17_tr,
    u8g2_font_profont22_tr,
    u8g2_font_profont29_tr,

    // unifont (wide unicode coverage) and vietnamese subsets
    u8g2_font_unifont_tr,
    u8g2_font_unifont_t_latin,
    u8g2_font_unifont_t_vietnamese1,
    u8g2_font_unifont_t_vietnamese2,

    // terminal / pixel classics
    u8g2_font_pxclassic_tr,
    u8g2_font_Terminal_tr,
    u8g2_font_NokiaSmallBold_tr,
    u8g2_font_VCR_OSD_tr,

    // spleen (larger pixel fonts)
    u8g2_font_spleen8x16_mr,
    u8g2_font_spleen12x24_mr,
    u8g2_font_spleen16x32_mr};

const char *textFontNames[] = {
    "4x6", "5x8", "6x10", "6x12",
    "6x13B", "7x13B", "8x13B", "9x15B", "10x20",
    "tom_thumb",
    "profont10", "profont11", "profont12", "profont15", "profont17", "profont22", "profont29",
    "unifont", "unifont_latin", "unifont_vn1", "unifont_vn2",
    "pxclassic", "Terminal", "NokiaSmallBold", "VCR_OSD",
    "spleen8x16", "spleen12x24", "spleen16x32"};

const uint8_t textFontCount = sizeof(textFonts) / sizeof(textFonts[0]);
