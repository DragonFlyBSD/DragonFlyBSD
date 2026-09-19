/*-
 * Extended Unicode glyph pools for syscons (beyond Spleen 0..255).
 */
#ifndef _SYSCONS_FONT_EXT_H_
#define _SYSCONS_FONT_EXT_H_

#include <sys/types.h>

/* Nerd symbol plane (narrow). */
#define SC_GLYPH_NERD_BASE		256
/* Spleen Unicode + letter fill (Greek/Cyrillic/Latin-Ext). */
#define SC_GLYPH_LETTERS_BASE		0x4000
/* Emoji double-width: even=left, odd=right. */
#define SC_GLYPH_EMOJI_BASE		0x8000

/* Back-compat alias used by older draw paths. */
#define SC_GLYPH_EXT_BASE		SC_GLYPH_NERD_BASE

static __inline int
sc_glyph_is_emoji(int glyph_id)
{
	return (glyph_id >= SC_GLYPH_EMOJI_BASE);
}

static __inline int
sc_glyph_is_emoji_left(int glyph_id)
{
	return (sc_glyph_is_emoji(glyph_id) && ((glyph_id & 1) == 0));
}

static __inline int
sc_glyph_is_letters(int glyph_id)
{
	return (glyph_id >= SC_GLYPH_LETTERS_BASE &&
	    glyph_id < SC_GLYPH_EMOJI_BASE);
}

static __inline int
sc_glyph_is_nerd(int glyph_id)
{
	return (glyph_id >= SC_GLYPH_NERD_BASE &&
	    glyph_id < SC_GLYPH_LETTERS_BASE);
}

int	sc_ext_lookup_glyph(uint32_t cp);
const u_char *sc_ext_glyph_bits(int glyph_id, int *out_width, int *out_height);
int	sc_ext_wide_right_glyph(int left_glyph_id);
int	sc_ext_glyph_count(void);
int	sc_ext_emoji_count(void);
int	sc_ext_letters_count(void);

#endif /* _SYSCONS_FONT_EXT_H_ */
