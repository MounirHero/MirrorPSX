// Experimental ABI-only probe bridge for PacBrew libiconv.
// NOT approved for shipping or physical-console execution until validated.
// SPDX-License-Identifier: GPL-3.0-or-later
// PacBrew's libiconv references the FreeBSD libc exported data variable
// ___mb_cur_max. OpenProspero application-profile imports do not supply it
// as a linkable definition. UTF-8 can occupy at most four bytes, so an
// explicit constant is used only to confirm whether all other static
// FFmpeg symbols can link. Real locale integration belongs in OpenProspero.
int ___mb_cur_max = 4;
