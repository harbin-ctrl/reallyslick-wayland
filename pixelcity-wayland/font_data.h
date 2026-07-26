#ifndef FONT_DATA_H
#define FONT_DATA_H

/*-----------------------------------------------------------------------------

  Baked 1-bpp glyph bitmaps for the signage / HUD fonts.

  GENERATED (the .c) by fontgen.c from the TTFs in fonts/. Compiling these in
  means the runtime needs neither FreeType nor fontconfig nor the .ttf files --
  the glyphs are byte-for-byte what FreeType produced at build time.

  Regenerate after changing the font list or FONT_SIZE:  make regen-fonts

-----------------------------------------------------------------------------*/

typedef struct {
  int                   width;    // glBitmap width  (pixels)
  int                   height;   // glBitmap height (pixels)
  float                 xorig;    // glBitmap x origin
  float                 yorig;    // glBitmap y origin
  float                 xmove;    // glBitmap pen advance x
  float                 ymove;    // glBitmap pen advance y
  const unsigned char*  bits;     // rows MSB-first, bottom-up; NULL if empty (e.g. space)
} Glyph;

// font_glyphs[font][char - font_first_char]
extern const Glyph* const font_glyphs[];
extern const int          font_count;
extern const int          font_first_char;
extern const int          font_glyph_count;

#endif
