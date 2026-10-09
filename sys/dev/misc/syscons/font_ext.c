/*-
 * Extended glyph lookup: letters (Spleen Unicode) + Nerd + mono emoji.
 */

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>

#include "font_ext.h"
#include "font_ext_nerd16x32.h"
#include "font_ext_emoji16x32.h"
#include "font_ext_letters16x32.h"

int
sc_ext_glyph_count(void)
{
	return (SC_EXT_GLYPH_COUNT);
}

int
sc_ext_emoji_count(void)
{
	return (SC_EMOJI_COUNT);
}

int
sc_ext_letters_count(void)
{
	return (SC_LETTERS_COUNT);
}

static int
bsearch_u32(const uint32_t *arr, int n, uint32_t cp)
{
	int lo, hi, mid;

	lo = 0;
	hi = n - 1;
	while (lo <= hi) {
		mid = lo + ((hi - lo) >> 1);
		if (arr[mid] == cp)
			return (mid);
		if (arr[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return (-1);
}

int
sc_ext_lookup_glyph(uint32_t cp)
{
	int idx;

	/* Prefer Spleen-native Unicode (style match) before Nerd PUA. */
	if (SC_LETTERS_COUNT > 0) {
		idx = bsearch_u32(sc_letters_cps, SC_LETTERS_COUNT, cp);
		if (idx >= 0)
			return (SC_GLYPH_LETTERS_BASE + idx);
	}
	if (SC_EXT_GLYPH_COUNT > 0) {
		idx = bsearch_u32(sc_ext_cps, SC_EXT_GLYPH_COUNT, cp);
		if (idx >= 0)
			return (SC_GLYPH_NERD_BASE + idx);
	}
	if (SC_EMOJI_COUNT > 0) {
		idx = bsearch_u32(sc_emoji_cps, SC_EMOJI_COUNT, cp);
		if (idx >= 0)
			return (SC_GLYPH_EMOJI_BASE + (idx << 1));
	}
	return (-1);
}

int
sc_ext_wide_right_glyph(int left_glyph_id)
{
	if (!sc_glyph_is_emoji_left(left_glyph_id))
		return (-1);
	return (left_glyph_id + 1);
}

const u_char *
sc_ext_glyph_bits(int glyph_id, int *out_width, int *out_height)
{
	int idx;
	int half;

	if (out_width != NULL)
		*out_width = 16;
	if (out_height != NULL)
		*out_height = 32;

	if (sc_glyph_is_emoji(glyph_id)) {
		idx = (glyph_id - SC_GLYPH_EMOJI_BASE) >> 1;
		half = glyph_id & 1;
		if (idx < 0 || idx >= SC_EMOJI_COUNT)
			return (NULL);
		return (&sc_emoji_bits[idx * SC_EMOJI_PAIR_BYTES +
		    half * SC_EMOJI_CELL_BYTES]);
	}
	if (sc_glyph_is_letters(glyph_id)) {
		idx = glyph_id - SC_GLYPH_LETTERS_BASE;
		if (idx < 0 || idx >= SC_LETTERS_COUNT)
			return (NULL);
		return (&sc_letters_bits[idx * SC_LETTERS_BYTES]);
	}
	if (sc_glyph_is_nerd(glyph_id)) {
		idx = glyph_id - SC_GLYPH_NERD_BASE;
		if (idx < 0 || idx >= SC_EXT_GLYPH_COUNT)
			return (NULL);
		return (&sc_ext_bits[idx * SC_EXT_GLYPH_BYTES]);
	}
	return (NULL);
}
