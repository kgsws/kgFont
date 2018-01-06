# kgFont
My custom bitmap font format.
16 bit Unicode support.
Currently only 8bit alpha channel is supported.

Format stores characters in ranges.
Each range contains consecutive characters until a gap is reached,
then next range starts.

Font convertor converts glyphs using FreeType to bitmaps.
Editor is a quick and simple SDL app that will let you touch-up created fonts.
