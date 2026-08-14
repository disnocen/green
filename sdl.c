/* green - the PDF reader
 * Copyright (C) 2009 Florian Tobias Schandinat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <SDL.h>
#include "green.h"


#define FLAG_QUIT	0x0001
#define FLAG_RENDER	0x0002


typedef enum
{
	NORMAL, GOTO, SEARCH, FIT, ROTATE, MIRROR, TOC
	
}	RState;

typedef struct
{
	char	buff[64];
	unsigned char	used, cur;
	
}	IBuffer;


const Uint32	live_interval = 40;

static RState	g_state = NORMAL;
static IBuffer	g_input = {.buff = {0}, .used = 0, .cur = 0};

typedef struct TOCNode
{
	char	*title;
	int	page;
		// 0-based page index, -1 if none
	bool	open;
	int	n_children;
	struct TOCNode	**children;
	
}	TOCNode;

typedef struct
{
	TOCNode	*node;
	int	depth;
	
}	TOCEntry;

static TOCNode	*g_toc = NULL;
static TOCEntry	*g_toc_entries = NULL;
static int	g_toc_count = 0, g_toc_sel = 0, g_toc_doc = -1;
static bool	g_toc_active = false;

// Simple 5x7 bitmap font for basic ASCII characters (32-126)
// Each character is represented by 7 bytes, one per row
static const unsigned char bitmap_font[][7] = {
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32: space
	{0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // 33: !
	{0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // 34: "
	{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // 35: #
	{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // 36: $
	{0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // 37: %
	{0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // 38: &
	{0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // 39: '
	{0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // 40: (
	{0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // 41: )
	{0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // 42: *
	{0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // 43: +
	{0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // 44: ,
	{0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // 45: -
	{0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // 46: .
	{0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // 47: /
	{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 48: 0
	{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 49: 1
	{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 50: 2
	{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 51: 3
	{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 52: 4
	{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 53: 5
	{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 54: 6
	{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 55: 7
	{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 56: 8
	{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 57: 9
	{0x00,0x04,0x00,0x00,0x04,0x00,0x00}, // 58: :
	{0x00,0x04,0x00,0x00,0x04,0x04,0x08}, // 59: ;
	{0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // 60: <
	{0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // 61: =
	{0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // 62: >
	{0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // 63: ?
	{0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // 64: @
	{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 65: A
	{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 66: B
	{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 67: C
	{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // 68: D
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 69: E
	{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 70: F
	{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 71: G
	{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 72: H
	{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 73: I
	{0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 74: J
	{0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 75: K
	{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 76: L
	{0x11,0x1B,0x15,0x11,0x11,0x11,0x11}, // 77: M
	{0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 78: N
	{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 79: O
	{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 80: P
	{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 81: Q
	{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 82: R
	{0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, // 83: S
	{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 84: T
	{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 85: U
	{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 86: V
	{0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 87: W
	{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 88: X
	{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 89: Y
	{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 90: Z
	{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // 91: [
	{0x10,0x08,0x04,0x02,0x01,0x00,0x00}, // 92: backslash
	{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // 93: ]
	{0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // 94: ^
	{0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // 95: _
	{0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // 96: `
	{0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 97: a
	{0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, // 98: b
	{0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, // 99: c
	{0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, // 100: d
	{0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 101: e
	{0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 102: f
	{0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 103: g
	{0x10,0x10,0x16,0x19,0x11,0x11,0x11}, // 104: h
	{0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 105: i
	{0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 106: j
	{0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 107: k
	{0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 108: l
	{0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 109: m
	{0x00,0x00,0x16,0x19,0x11,0x11,0x11}, // 110: n
	{0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 111: o
	{0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // 112: p
	{0x00,0x00,0x0D,0x13,0x0F,0x01,0x01}, // 113: q
	{0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 114: r
	{0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 115: s
	{0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 116: t
	{0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 117: u
	{0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 118: v
	{0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 119: w
	{0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 120: x
	{0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // 121: y
	{0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 122: z
	{0x06,0x08,0x08,0x10,0x08,0x08,0x06}, // 123: {
	{0x04,0x04,0x04,0x00,0x04,0x04,0x04}, // 124: |
	{0x0C,0x02,0x02,0x01,0x02,0x02,0x0C}, // 125: }
	{0x00,0x08,0x15,0x02,0x00,0x00,0x00}  // 126: ~
};


static Uint32	MapDisplayPixel( Uint32 src, SDL_PixelFormat *fmt, const Green_Theme *theme, bool dark )
{
	long	r = (src >> 16) & 0xFF,
		g = (src >> 8) & 0xFF,
		b = src & 0xFF;
	
	if (dark && theme)
	{
		long	lum = (r * 299 + g * 587 + b * 114) / 1000,
			inv = 255 - lum,
			tr = theme->text.r, tg = theme->text.g, tb = theme->text.b,
			br = theme->background.r, bg2 = theme->background.g, bb = theme->background.b;
		
		r = (tr * inv + br * lum) / 255 + (r - lum);
		g = (tg * inv + bg2 * lum) / 255 + (g - lum);
		b = (tb * inv + bb * lum) / 255 + (b - lum);
		if (r < 0) r = 0; else if (r > 255) r = 255;
		if (g < 0) g = 0; else if (g > 255) g = 255;
		if (b < 0) b = 0; else if (b > 255) b = 255;
	}
	
	return ((((Uint32)r >> fmt->Rloss) << fmt->Rshift)
		| (((Uint32)g >> fmt->Gloss) << fmt->Gshift)
		| (((Uint32)b >> fmt->Bloss) << fmt->Bshift));
}

static Uint32	MapBlendPixel( Uint32 src, SDL_PixelFormat *fmt,
	Uint32 ia, Uint32 ar, Uint32 ag, Uint32 ab, const Green_Theme *theme, bool dark )
{
	Uint32	r = (((src >> 16) & 0xFF) * ia + ar) / 256,
		g = (((src >> 8) & 0xFF) * ia + ag) / 256,
		b = ((src & 0xFF) * ia + ab) / 256;
	
	return MapDisplayPixel( (r << 16) | (g << 8) | b, fmt, theme, dark );
}

void	GetInput( IBuffer *input, SDL_Event *event )
{
	char	c;
	int i;
	
	switch (event->key.keysym.sym)
	{
		case SDLK_LEFT:
			if (input->cur)
				input->cur--;
			
			break;
		case SDLK_RIGHT:
			if (input->cur < input->used)
				input->cur++;
			
			break;
		case SDLK_BACKSPACE:
			if (!input->cur)
				break;
			
			input->cur--;
		case SDLK_DELETE:
			if (input->cur == input->used)
				break;
			
			for (i = input->cur; i < input->used-1; i++)
				input->buff[i] = input->buff[i+1];
			
			input->used--;
			break;
		case 'a'...'z':
			if (event->key.keysym.mod & KMOD_SHIFT)
				event->key.keysym.sym += 'A' - 'a';
		case '0'...'9':
		case '+':
		case '-':
		case '*':
		case '/':
		case '=':
		case '_':
		case '.':
		case ':':
		case ',':
		case ';':
		case '!':
		case '?':
		case '(':
		case ')':
		case '@':
		case ' ':
		case '$':
		case '&':
		case '#':
		case '\\':
			if (input->used == sizeof( input->buff ) - 1)
				break;
			
			for (i = input->cur; i < input->used; i++)
				input->buff[i+1] = input->buff[i];
			
			c = event->key.keysym.sym;
			input->buff[input->cur] = c;
			input->cur++;
			input->used++;
			break;
		default:
			break;
	}
	
	return;
}

void	RenderPage( Green_RTD *rtd, SDL_Rect dest, int xoff, int yoff, PopplerPage *page, double tscale, int page_num )
{
	PopplerRectangle	*rect;
	Green_Document	*doc = rtd->docs[rtd->doc_cur];
	const Green_Theme	*theme = Green_GetTheme( rtd );
	SDL_Surface	*display = SDL_GetVideoSurface();
	SDL_PixelFormat	fmt = *display->format;
	cairo_surface_t	*surface;
	cairo_t		*context;
	void	*pixels;
	unsigned short	ar, ag, ab, ia;
	gdouble	tmp_d;
	double	pwidth, pheight;
	guint	i, n;
	GList	*list = NULL;
	Uint32	*src, *dst;
	int	x, y, rowstride, w, h, dir_x, dir_y;

	if (doc->search_str)
		list = poppler_page_find_text( page, doc->search_str );

	if (doc->cache.surface && doc->cache.page == page_num && doc->cache.tscale == tscale)
		surface = doc->cache.surface;
	else
	{
		Green_GetDimension( page, &w, &h, tscale, false );
		surface = cairo_image_surface_create( CAIRO_FORMAT_ARGB32, w, h );
		context = cairo_create( surface );
		cairo_save( context );
		cairo_scale( context, tscale, tscale );
		poppler_page_render( page, context );
		cairo_restore( context );
		cairo_set_operator( context, CAIRO_OPERATOR_DEST_OVER );
		cairo_set_source_rgb( context, 1., 1., 1. );
		cairo_paint( context );
		cairo_destroy( context );
		if (doc->cache.surface)
			cairo_surface_destroy( doc->cache.surface );

		doc->cache.surface = surface;
		doc->cache.page = page_num;
		doc->cache.tscale = tscale;
	}

	if (doc->rotation == 1)
	{
		dir_x = -1;
		dir_y = 1;
	}
	else if (doc->rotation == 2)
	{
		dir_x = -1;
		dir_y = -1;
	}
	else if (doc->rotation == 3)
	{
		dir_x = 1;
		dir_y = -1;
	}
	else
		dir_x = dir_y = 1;

	if (doc->mirrored)
		dir_y *= -1;

	pixels = cairo_image_surface_get_data( surface );
	rowstride = cairo_image_surface_get_stride( surface );
	SDL_LockSurface( display );
	if (doc->rotation % 2)
	{
		for (y = 0; y < dest.h; y++)
		{
			src = pixels + (xoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * 4 + (yoff + (dir_x < 0 ? dest.w - 1 : 0)) * rowstride;
			dst = display->pixels + (dest.y + y) * display->pitch
				+ dest.x * fmt.BytesPerPixel;
			for (x = 0; x < dest.w; x++)
			{
				*dst = MapDisplayPixel( *src, &fmt, theme, rtd->night );
				
				src = (void*)src + dir_x * rowstride;
				dst = (void*)dst + fmt.BytesPerPixel;
			}
		}
	}
	else
	{
		for (y = 0; y < dest.h; y++)
		{
			src = pixels + (yoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * rowstride + (xoff + (dir_x < 0 ? dest.w - 1 : 0)) * 4;
			dst = display->pixels + (dest.y + y) * display->pitch
				+ dest.x * fmt.BytesPerPixel;
			for (x = 0; x < dest.w; x++)
			{
				*dst = MapDisplayPixel( *src, &fmt, theme, rtd->night );
				
				src += dir_x;
				dst = (void*)dst + fmt.BytesPerPixel;
			}
		}
	}
	
	if (list)
	{
		poppler_page_get_size( page, &pwidth, &pheight );
		if (theme && rtd->night)
		{
			ia = 0xFF - theme->highlight.a;
			ar = theme->highlight.a * theme->highlight.r;
			ag = theme->highlight.a * theme->highlight.g;
			ab = theme->highlight.a * theme->highlight.b;
		}
		else
		{
			ia = 0xFF - rtd->c_highlight.a;
			ar = rtd->c_highlight.a * rtd->c_highlight.r;
			ag = rtd->c_highlight.a * rtd->c_highlight.g;
			ab = rtd->c_highlight.a * rtd->c_highlight.b;
		}
		n = g_list_length( list );
		for (i = 0; i < n; i++)
		{
			rect = g_list_nth_data( list, i );
			tmp_d = pheight - rect->y2;
			rect->y2 = pheight - rect->y1;
			rect->y1 = tmp_d;
			rect->x1 *= tscale;
			rect->y1 *= tscale;
			rect->x2 *= tscale;
			rect->y2 *= tscale;
			rect->x1 -= xoff;
			rect->y1 -= yoff;
			rect->x2 -= xoff;
			rect->y2 -= yoff;
			if (doc->rotation % 2)
			{
				tmp_d = rect->x1;
				rect->x1 = rect->y1;
				rect->y1 = tmp_d;
				tmp_d = rect->x2;
				rect->x2 = rect->y2;
				rect->y2 = tmp_d;
			}

			if (doc->mirrored)
			{
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}

			if (doc->rotation == 1)
			{
				tmp_d = rect->x1;
				rect->x1 = dest.w - rect->x2;
				rect->x2 = dest.w - tmp_d;
			}
			else if (doc->rotation == 2)
			{
				tmp_d = rect->x1;
				rect->x1 = dest.w - rect->x2;
				rect->x2 = dest.w - tmp_d;
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}
			else if (doc->rotation == 3)
			{
				tmp_d = rect->y1;
				rect->y1 = dest.h - rect->y2;
				rect->y2 = dest.h - tmp_d;
			}

			if (rect->x1 > dest.w)
				continue;
			else if (rect->x1 < 0)
				rect->x1 = 0;
			
			if (rect->x2 < 0)
				continue;
			else if (rect->x2 > dest.w)
				rect->x2 = dest.w;
			
			if (rect->y1 > dest.h)
				continue;
			else if (rect->y1 < 0)
				rect->y1 = 0;
			
			if (rect->y2 < 0)
				continue;
			else if (rect->y2 > dest.h)
				rect->y2 = dest.h;
			
			if (doc->rotation % 2)
			{
				for (y = rect->y1; y < (int)rect->y2; y++)
				{
					src = pixels + (xoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * 4 + (yoff + dir_x * (int)rect->x1 + (dir_x < 0 ? dest.w - 1 : 0)) * rowstride;
					dst = display->pixels + (dest.y + y) * display->pitch
						+ (dest.x + (int)rect->x1) * fmt.BytesPerPixel;
					for (x = rect->x1; x < (int)rect->x2; x++)
					{
						*dst = MapBlendPixel( *src, &fmt, ia, ar, ag, ab, theme, rtd->night );
						
						src = (void*)src + dir_x * rowstride;
						dst = (void*)dst + fmt.BytesPerPixel;
					}
				}
			}
			else
			{
				for (y = rect->y1; y < (int)rect->y2; y++)
				{
					src = pixels + (yoff + dir_y * y + (dir_y < 0 ? dest.h - 1 : 0)) * rowstride + (xoff + dir_x * (int)rect->x1 + (dir_x < 0 ? dest.w - 1 : 0)) * 4;
					dst = display->pixels + (dest.y + y) * display->pitch
						+ (dest.x + (int)rect->x1) * fmt.BytesPerPixel;
					for (x = rect->x1; x < (int)rect->x2; x++)
					{
						*dst = MapBlendPixel( *src, &fmt, ia, ar, ag, ab, theme, rtd->night );
						
						src += dir_x;
						dst = (void*)dst + fmt.BytesPerPixel;
					}
				}
			}
		}
		
		g_list_free( list );
	}
	
	SDL_UnlockSurface( display );
	return;
}

// Draw a single character using bitmap font
void DrawChar( SDL_Surface *display, int x, int y, char c, Uint32 color )
{
	SDL_Rect pixel;
	int row, col;
	unsigned char bits;
	
	// Only handle printable ASCII characters
	if (c < 32 || c > 126)
		return;
	
	// Get the bitmap for this character
	const unsigned char *char_bitmap = bitmap_font[c - 32];
	
	// Draw the character pixel by pixel
	pixel.w = 2;  // Make pixels 2x2 for better visibility
	pixel.h = 2;
	
	for (row = 0; row < 7; row++)
	{
		bits = char_bitmap[row];
		for (col = 0; col < 5; col++)
		{
			if (bits & (1 << (4 - col)))
			{
				pixel.x = x + col * 2;
				pixel.y = y + row * 2;
				SDL_FillRect( display, &pixel, color );
			}
		}
	}
}

// Draw a string using bitmap font
void DrawString( SDL_Surface *display, int x, int y, const char *str, Uint32 color )
{
	int i;
	int char_width = 12; // 5 pixels * 2 + 2 pixel spacing
	
	for (i = 0; str[i] != '\0'; i++)
	{
		DrawChar( display, x + i * char_width, y, str[i], color );
	}
}

void	RenderSearchBox( SDL_Surface *display, IBuffer *input, RState state )
{
	SDL_Rect box, inner_box, cursor, text_bg;
	int text_height = 20;
	int box_padding = 10;
	int box_height = text_height + 2 * box_padding;
	int box_width = 600;
	int char_width = 12;
	char search_text[80];
	Uint32 white_color, gray_color, yellow_color;
	
	if (state != SEARCH && state != GOTO)
		return;
	
	// Get colors
	white_color = SDL_MapRGB( display->format, 255, 255, 255 );
	gray_color = SDL_MapRGB( display->format, 180, 180, 180 );
	yellow_color = SDL_MapRGB( display->format, 255, 255, 0 );
	
	// Draw outer border
	box.x = (display->w - box_width) / 2;
	box.y = display->h - box_height - 40;
	box.w = box_width;
	box.h = box_height;
	SDL_FillRect( display, &box, SDL_MapRGB( display->format, 200, 200, 200 ) );
	
	// Draw inner background
	inner_box.x = box.x + 2;
	inner_box.y = box.y + 2;
	inner_box.w = box.w - 4;
	inner_box.h = box.h - 4;
	SDL_FillRect( display, &inner_box, SDL_MapRGB( display->format, 40, 40, 40 ) );
	
	// Draw text background area
	text_bg.x = inner_box.x + box_padding;
	text_bg.y = inner_box.y + box_padding;
	text_bg.w = inner_box.w - 2 * box_padding;
	text_bg.h = text_height;
	SDL_FillRect( display, &text_bg, SDL_MapRGB( display->format, 30, 30, 30 ) );
	
	// Draw prompt
	const char *prompt = state == SEARCH ? "Search: " : "Go to page: ";
	DrawString( display, text_bg.x + 4, text_bg.y + 3, prompt, gray_color );
	
	// Draw input text
	if (input->used > 0)
	{
		// Null terminate for display
		memcpy( search_text, input->buff, input->used );
		search_text[input->used] = '\0';
		DrawString( display, text_bg.x + 4 + strlen(prompt) * char_width, text_bg.y + 3, search_text, white_color );
	}
	
	// Draw cursor
	cursor.x = text_bg.x + 4 + (strlen(prompt) + input->cur) * char_width - 2;
	cursor.y = text_bg.y + 2;
	cursor.w = 2;
	cursor.h = 16;
	
	// Blinking cursor effect
	if ((SDL_GetTicks() / 500) % 2 == 0)
		SDL_FillRect( display, &cursor, yellow_color );
}

static int	ViewWidth( Green_RTD *rtd )
{
	SDL_Surface	*display = SDL_GetVideoSurface();
	
	if (rtd->side_by_side && Green_IsDocValid( rtd, rtd->doc_cur )
		&& rtd->docs[rtd->doc_cur]->page_count > 1)
		return display->w / 2;
	
	return display->w;
}

static void	FreeTOC( TOCNode *node )
{
	int	i;
	
	if (!node)
		return;
	
	for (i = 0; i < node->n_children; i++)
		FreeTOC( node->children[i] );
	free( node->children );
	free( node->title );
	free( node );
	return;
}

static TOCNode	*BuildTOCNode( PopplerDocument *doc, PopplerIndexIter *iter )
{
	PopplerIndexIter	*child;
	PopplerAction		*action;
	PopplerDest		*dest;
	TOCNode			*node, **arr = NULL;
	int			n = 0;
	
	if (!iter)
		return NULL;
	
	node = calloc( 1, sizeof( *node ) );
	if (!node)
		return NULL;
	
	node->page = -1;
	node->open = poppler_index_iter_is_open( iter );
	action = poppler_index_iter_get_action( iter );
	if (action)
	{
		if (action->type == POPPLER_ACTION_GOTO_DEST && action->goto_dest.dest)
		{
			dest = action->goto_dest.dest;
			if (dest->type == POPPLER_DEST_NAMED && dest->named_dest)
			{
				PopplerDest	*resolved = poppler_document_find_dest( doc, dest->named_dest );
				if (resolved)
				{
					node->page = resolved->page_num;
					poppler_dest_free( resolved );
				}
			}
			else
				node->page = dest->page_num;
		}
		if (action->goto_dest.title)
			node->title = strdup( action->goto_dest.title );
		poppler_action_free( action );
	}
	if (!node->title)
		node->title = strdup( "" );
	
	child = poppler_index_iter_get_child( iter );
	if (child)
	{
		do
		{
			arr = realloc( arr, (n + 1) * sizeof( *arr ) );
			if (!arr)
			{
				poppler_index_iter_free( child );
				FreeTOC( node );
				return NULL;
			}
			arr[n] = BuildTOCNode( doc, child );
			n++;
		}	while (poppler_index_iter_next( child ));
		
		poppler_index_iter_free( child );
		node->children = arr;
		node->n_children = n;
	}
	
	return node;
}

static void	RebuildTOCFlat( void );

static void	BuildTOC( Green_RTD *rtd )
{
	PopplerIndexIter	*iter;
	TOCNode			**arr = NULL;
	int			n = 0;
	
	FreeTOC( g_toc );
	g_toc = NULL;
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return;
	
	iter = poppler_index_iter_new( rtd->docs[rtd->doc_cur]->doc );
	if (!iter)
		return;
	
	g_toc = calloc( 1, sizeof( *g_toc ) );
	if (!g_toc)
	{
		poppler_index_iter_free( iter );
		return;
	}
	g_toc->page = -1;
	
	do
	{
		arr = realloc( arr, (n + 1) * sizeof( *arr ) );
		if (!arr)
		{
			poppler_index_iter_free( iter );
			FreeTOC( g_toc );
			g_toc = NULL;
			return;
		}
		arr[n] = BuildTOCNode( rtd->docs[rtd->doc_cur]->doc, iter );
		n++;
	}	while (poppler_index_iter_next( iter ));
	
	poppler_index_iter_free( iter );
	g_toc->children = arr;
	g_toc->n_children = n;
	g_toc_sel = 0;
	RebuildTOCFlat();
	return;
}

static void	EnsureTOC( Green_RTD *rtd )
{
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
		return;
	
	if (g_toc_doc != rtd->doc_cur)
	{
		g_toc_doc = rtd->doc_cur;
		BuildTOC( rtd );
	}
	return;
}

static void	CollectVisible( TOCNode *node, int depth, TOCEntry **arr, int *count, int *cap )
{
	int	i;
	
	for (i = 0; i < node->n_children; i++)
	{
		TOCNode	*c = node->children[i];
		
		if (*count >= *cap)
		{
			*cap = *cap ? *cap * 2 : 32;
			*arr = realloc( *arr, *cap * sizeof( TOCEntry ) );
			if (!*arr)
				return;
		}
		
		(*arr)[*count].node = c;
		(*arr)[*count].depth = depth;
		(*count)++;
		if (c->open)
			CollectVisible( c, depth + 1, arr, count, cap );
	}
	
	return;
}

static void	RebuildTOCFlat( void )
{
	TOCEntry	*arr = NULL;
	int		count = 0, cap = 0;
	
	free( g_toc_entries );
	g_toc_entries = NULL;
	g_toc_count = 0;
	if (g_toc)
		CollectVisible( g_toc, 0, &arr, &count, &cap );
	g_toc_entries = arr;
	g_toc_count = count;
	if (g_toc_sel >= g_toc_count)
		g_toc_sel = g_toc_count ? g_toc_count - 1 : 0;
	return;
}

static void	RenderTOC( SDL_Surface *display, Green_RTD *rtd )
{
	SDL_Rect	panel, hl;
	Uint32		title_col, entry_col, sel_col, sel_bg;
	int		panel_w, x0, y0, line_h, max_lines, start, i, indent, max_chars;
	char		buf[256];
	
	if (!g_toc_active)
		return;
	
	panel_w = display->w / 3;
	if (panel_w > 500)
		panel_w = 500;
	if (panel_w < 200)
		panel_w = display->w < 200 ? display->w : 200;
	
	panel.x = 0;
	panel.y = 0;
	panel.w = panel_w;
	panel.h = display->h;
	SDL_FillRect( display, &panel, SDL_MapRGB( display->format, 0x14, 0x14, 0x14 ) );
	
	{
		const Green_Theme	*theme = Green_GetTheme( rtd );
		
		if (theme && theme->dark)
			SDL_FillRect( display, &panel, SDL_MapRGB( display->format, theme->background.r, theme->background.g, theme->background.b ) );
	}
	
	title_col = SDL_MapRGB( display->format, 0xE8, 0xE8, 0xE8 );
	entry_col = SDL_MapRGB( display->format, 0xB8, 0xB8, 0xB8 );
	sel_col = SDL_MapRGB( display->format, 0xFF, 0xFF, 0x00 );
	sel_bg = SDL_MapRGB( display->format, 0x2E, 0x2E, 0x5E );
	
	x0 = 8;
	y0 = 8;
	DrawString( display, x0, y0, "Table of Contents", title_col );
	y0 += 22;
	line_h = 16;
	max_lines = (display->h - y0) / line_h;
	if (max_lines <= 0)
		return;
	
	if (g_toc_count <= 0)
	{
		DrawString( display, x0, y0, "No table of contents", entry_col );
		return;
	}
	
	start = 0;
	if (g_toc_count > max_lines)
	{
		start = g_toc_sel - max_lines + 1;
		if (start < 0)
			start = 0;
		if (start + max_lines > g_toc_count)
			start = g_toc_count - max_lines;
	}
	
	for (i = start; i < g_toc_count && i < start + max_lines; i++)
	{
		TOCNode	*n = g_toc_entries[i].node;
		int	y = y0 + (i - start) * line_h;
		
		if (i == g_toc_sel)
		{
			hl.x = 0;
			hl.y = y;
			hl.w = panel_w;
			hl.h = line_h;
			SDL_FillRect( display, &hl, sel_bg );
		}
		
		indent = x0 + g_toc_entries[i].depth * 14;
		if (n->n_children > 0)
		{
			if (n->open)
				DrawString( display, indent, y, "-", i == g_toc_sel ? sel_col : entry_col );
			else
				DrawString( display, indent, y, "+", i == g_toc_sel ? sel_col : entry_col );
		}
		
		max_chars = (panel_w - indent - 26) / 12;
		if (max_chars < 1)
			max_chars = 1;
		if (max_chars > (int)sizeof( buf ) - 1)
			max_chars = sizeof( buf ) - 1;
		strncpy( buf, n->title, max_chars );
		buf[max_chars] = 0;
		DrawString( display, indent + 26, y, buf, i == g_toc_sel ? sel_col : entry_col );
	}
	
	return;
}

static int	FindTOCIndex( TOCNode *target )
{
	int	i;
	
	for (i = 0; i < g_toc_count; i++)
		if (g_toc_entries[i].node == target)
			return i;
	
	return -1;
}

static int	FindParentIndex( TOCNode *target )
{
	int	i, j;
	
	for (i = 0; i < g_toc_count; i++)
		for (j = 0; j < g_toc_entries[i].node->n_children; j++)
			if (g_toc_entries[i].node->children[j] == target)
				return i;
	
	return -1;
}

static int	TOCGoToPage( Green_RTD *rtd, TOCNode *node, unsigned short *flags )
{
	Green_Document	*doc;
	
	if (!node || node->page < 0 || !Green_IsDocValid( rtd, rtd->doc_cur ))
		return 0;
	
	doc = rtd->docs[rtd->doc_cur];
	if (Green_GotoPage( doc, node->page, true ))
	{
		*flags |= FLAG_RENDER;
		return 1;
	}
	return 0;
}

static RState	TOCInput( Green_RTD *rtd, SDL_Event *event, unsigned short *flags )
{
	switch (event->key.keysym.sym)
	{
		case SDLK_TAB:
		case SDLK_ESCAPE:
			g_toc_active = false;
			*flags |= FLAG_RENDER;
			return NORMAL;
		case SDLK_j:
		case SDLK_DOWN:
			if (g_toc_count > 0 && g_toc_sel < g_toc_count - 1)
				g_toc_sel++;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
		case SDLK_UP:
			if (g_toc_count > 0 && g_toc_sel > 0)
				g_toc_sel--;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_l:
		case SDLK_RIGHT:
			if (g_toc_count > 0)
			{
				TOCNode	*n = g_toc_entries[g_toc_sel].node;
				
				if (n->n_children > 0)
				{
					if (!n->open)
					{
						n->open = true;
						RebuildTOCFlat();
					}
					else
					{
						int	idx = FindTOCIndex( n->children[0] );
						if (idx >= 0)
							g_toc_sel = idx;
					}
				}
				else
				{
					if (TOCGoToPage( rtd, n, flags ))
					{
						g_toc_active = false;
						return NORMAL;
					}
				}
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_h:
		case SDLK_LEFT:
			if (g_toc_count > 0)
			{
				TOCNode	*n = g_toc_entries[g_toc_sel].node;
				
				if (n->n_children > 0 && n->open)
				{
					n->open = false;
					RebuildTOCFlat();
				}
				else
				{
					int	idx = FindParentIndex( n );
					if (idx >= 0)
						g_toc_sel = idx;
				}
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_RETURN:
			if (g_toc_count > 0)
			{
				if (TOCGoToPage( rtd, g_toc_entries[g_toc_sel].node, flags ))
				{
					g_toc_active = false;
					return NORMAL;
				}
			}
			break;
		default:
			break;
	}
	
	return TOC;
}

void	Render( Green_RTD *rtd )
{
	Green_Document	*doc;
	PopplerPage	*page = NULL;
	SDL_Surface	*display = SDL_GetVideoSurface();
	SDL_Rect	rect;
	double	tscale;
	int	w, h, w2, h2;
	
	rect.x = rect.y = 0;
	rect.w = display->w;
	rect.h = display->h;
	if (rtd->night)
	{
		const Green_Theme	*theme = Green_GetTheme( rtd );
		
		if (theme)
			SDL_FillRect( display, &rect, SDL_MapRGB( display->format, theme->background.r, theme->background.g, theme->background.b ) );
		else
			SDL_FillRect( display, &rect, SDL_MapRGB( display->format, 0x10, 0x10, 0x10 ) );
	}
	else
		SDL_FillRect( display, &rect, SDL_MapRGB( display->format, rtd->c_background.r, rtd->c_background.g, rtd->c_background.b ));
	if (!Green_IsDocValid( rtd, rtd->doc_cur ))
	{
		SDL_UpdateRect( display, 0, 0, 0, 0 );
		return;
	}
	
	doc = rtd->docs[rtd->doc_cur];
	
	if (rtd->side_by_side && doc->page_count > 1)
	{
		int left_page, right_page;
		PopplerPage *left_page_obj = NULL, *right_page_obj = NULL;
		
		// Book-style pairs: 1-2, 3-4, 5-6, etc.
		// If current page is odd (1, 3, 5...), show current and next
		// If current page is even (2, 4, 6...), show previous and current
		if (doc->page_cur % 2 == 0) { // 0-indexed, so page 0 is page 1, page 1 is page 2
			left_page = doc->page_cur;
			right_page = doc->page_cur + 1;
		} else {
			left_page = doc->page_cur - 1;
			right_page = doc->page_cur;
		}
		
		
		if (left_page >= 0 && right_page < doc->page_count)
		{
			tscale = Green_Fit( doc, display->w / 2, display->h ) * doc->finescale;
			
			left_page_obj = poppler_document_get_page( doc->doc, left_page );
			right_page_obj = poppler_document_get_page( doc->doc, right_page );
			
			
			Green_GetDimension( left_page_obj, &w, &h, tscale, doc->rotation % 2 );
			Green_GetDimension( right_page_obj, &w2, &h2, tscale, doc->rotation % 2 );
			
			rect.w = w > display->w / 2 ? display->w / 2 : w;
			rect.h = h > display->h ? display->h : h;
			rect.x = (display->w / 2 - rect.w) / 2;
			rect.y = (display->h - rect.h) / 2;
			RenderPage( rtd, rect, doc->xoffset, doc->yoffset, left_page_obj, tscale, left_page );
			
			rect.w = w2 > display->w / 2 ? display->w / 2 : w2;
			rect.h = h2 > display->h ? display->h : h2;
			rect.x = display->w / 2 + (display->w / 2 - rect.w) / 2;
			rect.y = (display->h - rect.h) / 2;
			RenderPage( rtd, rect, doc->xoffset, doc->yoffset, right_page_obj, tscale, right_page );
			
			g_object_unref( G_OBJECT( left_page_obj ) );
			g_object_unref( G_OBJECT( right_page_obj ) );
		}
		else
		{
			tscale = Green_Fit( doc, display->w, display->h ) * doc->finescale;
			page = poppler_document_get_page( doc->doc, doc->page_cur );
			Green_GetDimension( page, &w, &h, tscale, doc->rotation % 2 );
			rect.w = w > display->w ? display->w : w;
			rect.h = h > display->h ? display->h : h;
			rect.x = (display->w - rect.w) / 2;
			rect.y = (display->h - rect.h) / 2;
			RenderPage( rtd, rect, doc->xoffset, doc->yoffset, page, tscale, doc->page_cur );
			g_object_unref( G_OBJECT( page ) );
		}
	}
	else
	{
		tscale = Green_Fit( doc, display->w, display->h ) * doc->finescale;
		page = poppler_document_get_page( doc->doc, doc->page_cur );
		Green_GetDimension( page, &w, &h, tscale, doc->rotation % 2 );
		rect.w = w > display->w ? display->w : w;
		rect.h = h > display->h ? display->h : h;
		rect.x = (display->w - rect.w) / 2;
		rect.y = (display->h - rect.h) / 2;
		RenderPage( rtd, rect, doc->xoffset, doc->yoffset, page, tscale, doc->page_cur );
		g_object_unref( G_OBJECT( page ) );
	}
	
	// Render search box if in search or goto mode
	RenderSearchBox( display, &g_input, g_state );
	
	// Render table of contents panel if active
	RenderTOC( display, rtd );
	
	SDL_UpdateRect( display, 0, 0, 0, 0 );
	return;
}

RState	NormalInput( Green_RTD *rtd, SDL_Event *event, unsigned short *flags )
{
	Green_Document	*doc = NULL;
	SDL_Surface	*display = SDL_GetVideoSurface();
	RState	state = NORMAL;
	int	f = 0, vw = ViewWidth( rtd );
	
	if (Green_IsDocValid( rtd, rtd->doc_cur ))
		doc = rtd->docs[rtd->doc_cur];
	
	switch (event->key.keysym.sym)
	{
		case 'q':
			*flags |= FLAG_QUIT;
			break;
		case 'c':
			Green_Close( rtd, rtd->doc_cur );
			*flags |= FLAG_RENDER;
			break;
		case 'g':
			state = GOTO;
			break;
		case SDLK_s:
		case '/':
			/* type s-SEARCHSTRING-<Enter> to search string */
			state = SEARCH;
			break;
		case 'n':
		case ';':
			if (!doc || !doc->search_str)
				break;
			
			doc->page_cur = Green_FindNext( doc, doc->page_cur + 1 );
			*flags |= FLAG_RENDER;
			break;
		case ',':
			if (!doc || !doc->search_str)
				break;
			
			doc->page_cur = Green_FindPrevious( doc, doc->page_cur );
			*flags |= FLAG_RENDER;
			break;
		case 'f':
			state = FIT;
			break;
		case SDLK_UP:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the top and need to go to previous page pair
			if (rtd->side_by_side && doc->yoffset == 0) {
				if (!Green_GotoPage( doc, doc->page_cur - 2, true ))
					break;
			} else {
				Green_ScrollRelative( doc, 0, - display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_k:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the top and need to go to previous page pair
			if (rtd->side_by_side && doc->yoffset == 0) {
				if (!Green_GotoPage( doc, doc->page_cur - 2, true ))
					break;
			} else {
				Green_ScrollRelative( doc, 0, - display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_DOWN:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the bottom and need to go to next page pair
			if (rtd->side_by_side) {
				int max_x, max_y;
				Green_GetScrollRegion( doc, vw, display->h, &max_x, &max_y );
				if (doc->yoffset >= max_y) {
					if (!Green_GotoPage( doc, doc->page_cur + 2, true ))
						break;
				} else {
					Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
				}
			} else {
				Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
                case SDLK_j:
			if (!doc)
				break;
			
			// In side-by-side mode, check if we're at the bottom and need to go to next page pair
			if (rtd->side_by_side) {
				int max_x, max_y;
				Green_GetScrollRegion( doc, vw, display->h, &max_x, &max_y );
				if (doc->yoffset >= max_y) {
					if (!Green_GotoPage( doc, doc->page_cur + 2, true ))
						break;
				} else {
					Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
				}
			} else {
				Green_ScrollRelative( doc, 0, display->h * rtd->step, vw, display->h, 1 );
			}
			*flags |= FLAG_RENDER;
			break;
		case SDLK_LEFT:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, - vw * rtd->step, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_h:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, - vw * rtd->step, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_l:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, vw * rtd->step, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_RIGHT:
			if (!doc)
				break;
			
			Green_ScrollRelative( doc, vw * rtd->step, 0, vw, display->h, 1 );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_PAGEUP:
			if (!doc)
				break;
			
			if (rtd->side_by_side) {
				if (!Green_GotoPage( doc, doc->page_cur - 2, true ))
					break;
			} else {
				if (!Green_GotoPage( doc, doc->page_cur - 1, true ))
					break;
			}
			
			*flags |= FLAG_RENDER;
			break;
		case SDLK_PAGEDOWN:
			if (!doc)
				break;
			
			if (rtd->side_by_side) {
				if (!Green_GotoPage( doc, doc->page_cur + 2, true ))
					break;
			} else {
				if (!Green_GotoPage( doc, doc->page_cur + 1, true ))
					break;
			}
			
			*flags |= FLAG_RENDER;
			break;
		case SDLK_m:
			if (!doc)
				break;
			
			state = MIRROR;
			break;
		case SDLK_r:
			if (!doc)
				break;
			
			state = ROTATE;
			break;
		case SDLK_PLUS:
		case SDLK_EQUALS:
			if (!doc)
				break;
			
			Green_Zoom( doc, vw, display->h, doc->finescale * rtd->zoomstep );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_MINUS:
			if (!doc)
				break;
			
			Green_Zoom( doc, vw, display->h, doc->finescale / rtd->zoomstep );
			*flags |= FLAG_RENDER;
			break;
		case SDLK_F12:
			f++;
		case SDLK_F11:
			f++;
		case SDLK_F10:
			f++;
		case SDLK_F9:
			f++;
		case SDLK_F8:
			f++;
		case SDLK_F7:
			f++;
		case SDLK_F6:
			f++;
		case SDLK_F5:
			f++;
		case SDLK_F4:
			f++;
		case SDLK_F3:
			f++;
		case SDLK_F2:
			f++;
		case SDLK_F1:
			if (!Green_IsDocValid( rtd, f ))
				break;
			
			rtd->doc_cur = f;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_TAB:
			if (!doc)
				break;
			
			EnsureTOC( rtd );
			g_toc_active = true;
			state = TOC;
			*flags |= FLAG_RENDER;
			break;
		case SDLK_d:
			rtd->side_by_side = !rtd->side_by_side;
			*flags |= FLAG_RENDER;
			break;
		case 'i':
			rtd->night = !rtd->night;
			{
				const Green_Theme	*theme = Green_GetTheme( rtd );
				
				if (theme)
					fprintf( stderr, "mode: %s theme: %s\n", rtd->night ? "dark" : "light", theme->name );
			}
			*flags |= FLAG_RENDER;
			break;
		case 't':
			if (rtd->themes && rtd->themes_count > 1)
			{
				int	idx = rtd->night ? rtd->theme_dark_cur : rtd->theme_light_cur,
					i;
				
				for (i = 1; i < rtd->themes_count; i++)
				{
					int	next = (idx + i) % rtd->themes_count;
					
					if (rtd->themes[next].dark == rtd->night)
					{
						if (rtd->night)
							rtd->theme_dark_cur = next;
						else
							rtd->theme_light_cur = next;
						fprintf( stderr, "theme: %s\n", rtd->themes[next].name );
						break;
					}
				}
				*flags |= FLAG_RENDER;
			}
			break;
		default:
			break;
	}
	
	return state;
}

Uint32	live_timer( Uint32 interval, void *param )
{
	SDL_Event	event;
	
	event.type = SDL_USEREVENT;
	SDL_PushEvent( &event );
	return interval;
}

int	Green_SDL_Main( Green_RTD *rtd )
{
	SDL_TimerID	timer = NULL;
	SDL_Surface	*display;
	SDL_Event	event;
	Uint32	mouse_last = 0, mouse_cur;
	Uint16	left_x = 0, left_y = 0, right_x = 0, right_y = 0;
	unsigned short	flags = FLAG_RENDER;
	unsigned char	event_count;
	char	*str;
	long	tmp;
	int	x, y, width, height;
	struct termios orig_termios, new_termios;
	
	/* Save current terminal settings and disable echo */
	if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
		new_termios = orig_termios;
		new_termios.c_lflag &= ~(ECHO | ECHONL);
		tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
	}
	
	if (SDL_Init( SDL_INIT_VIDEO | SDL_INIT_TIMER ))
	{
		fprintf( stderr, "SDL_Init failed: %s\n", SDL_GetError() );
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
		return 1;
	}
	
	SDL_WM_SetCaption( "green - the PDF reader", NULL );
	display = SDL_SetVideoMode( rtd->width, rtd->height, 0, SDL_SWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE | (rtd->flags&GREEN_FULLSCREEN ? SDL_FULLSCREEN : 0) );
	if (!display)
	{
		SDL_Quit();
		fprintf( stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError() );
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
		return 2;
	}
	
	if (display->format->palette)
	{
		SDL_Quit();
		fprintf( stderr, "Palettes are not supported!\n" );
		tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
                return 3;
	}
	
	timer = SDL_AddTimer( live_interval, live_timer, NULL );
	mouse_last = SDL_GetTicks();
	if (!rtd->mouse.visibility)
		SDL_ShowCursor( SDL_DISABLE );
	
	do
	{
		if (flags&FLAG_RENDER)
		{
			Render( rtd );
			flags ^= FLAG_RENDER;
		}
		
		event_count = 0;
		if (!SDL_WaitEvent( &event ))
		{
			SDL_Quit();
			tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
			return -1;
		}
		
		do
		{
			switch (event.type)
			{
				case SDL_QUIT:
					flags |= FLAG_QUIT;
					break;
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_ESCAPE)
					{
						g_state = NORMAL;
						if (g_toc_active)
						{
							g_toc_active = false;
							flags |= FLAG_RENDER;
						}
					}
					else if (g_state == TOC)
						g_state = TOCInput( rtd, &event, &flags );
					else if (event.key.keysym.sym == SDLK_RETURN)
					{
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						g_input.buff[g_input.used] = 0;
						switch (g_state)
						{
							case GOTO:
								tmp = strtol( g_input.buff, &str, 10 );
								if (*str || tmp <=0 || tmp > rtd->docs[rtd->doc_cur]->page_count )
									break;
								
								rtd->docs[rtd->doc_cur]->page_cur = tmp - 1;
								rtd->docs[rtd->doc_cur]->xoffset = 0;
								rtd->docs[rtd->doc_cur]->yoffset = 0;
								flags |= FLAG_RENDER;
								break;
							case SEARCH:
								free( rtd->docs[rtd->doc_cur]->search_str );
								rtd->docs[rtd->doc_cur]->search_str = NULL;
								if (!strlen( g_input.buff ))
									break;
								
								rtd->docs[rtd->doc_cur]->search_str = strdup( g_input.buff );
								tmp = Green_FindNext( rtd->docs[rtd->doc_cur], rtd->docs[rtd->doc_cur]->page_cur );
								if (tmp < 0)
								{
									free( rtd->docs[rtd->doc_cur]->search_str );
									rtd->docs[rtd->doc_cur]->search_str = NULL;
									break;
								}
								
								rtd->docs[rtd->doc_cur]->page_cur = tmp;
								flags |= FLAG_RENDER;
								break;
							default:
								break;
						}
						
						g_state = NORMAL;
					}
					else if (g_state == NORMAL)
					{
						g_state = NormalInput( rtd, &event, &flags );
						if (g_state != NORMAL)
						{
							g_input.used = 0;
							g_input.cur = 0;
						}
					}
					else if (g_state == GOTO || g_state == SEARCH)
						GetInput( &g_input, &event );
					else if (g_state == FIT)
					{
						g_state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'n')
							rtd->docs[rtd->doc_cur]->fit_method = NATURAL;
						else if (event.key.keysym.sym == 'w')
							rtd->docs[rtd->doc_cur]->fit_method = WIDTH;
						else if (event.key.keysym.sym == 'h')
							rtd->docs[rtd->doc_cur]->fit_method = HEIGHT;
						else if (event.key.keysym.sym == 'p')
							rtd->docs[rtd->doc_cur]->fit_method = PAGE;
						
						if (event.key.keysym.sym == 'n'
							|| event.key.keysym.sym == 'w'
							|| event.key.keysym.sym == 'h'
							|| event.key.keysym.sym == 'p')
						{
							rtd->docs[rtd->doc_cur]->finescale = 1;
							rtd->docs[rtd->doc_cur]->xoffset = 0;
							rtd->docs[rtd->doc_cur]->yoffset = 0;
							flags |= FLAG_RENDER;
						}
					}
					else if (g_state == MIRROR)
					{
						g_state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'h')
						{
							Green_MirrorH( rtd->docs[rtd->doc_cur] );
							flags |= FLAG_RENDER;
						}
						else if (event.key.keysym.sym == 'v')
						{
							Green_MirrorV( rtd->docs[rtd->doc_cur] );
							flags |= FLAG_RENDER;
						}
					}
					else if (g_state == ROTATE)
					{
						g_state = NORMAL;
						if (!Green_IsDocValid( rtd, rtd->doc_cur ))
							break;
						
						if (event.key.keysym.sym == 'l')
						{
							Green_RotateLeft( rtd->docs[rtd->doc_cur] );
							Green_ValidateOffset( rtd->docs[rtd->doc_cur], display->w, display->h );
							flags |= FLAG_RENDER;
						}
						else if (event.key.keysym.sym == 'r')
						{
							Green_RotateRight( rtd->docs[rtd->doc_cur] );
							Green_ValidateOffset( rtd->docs[rtd->doc_cur], display->w, display->h );
							flags |= FLAG_RENDER;
						}
					}
					
					break;
				case SDL_VIDEORESIZE:
					display = SDL_SetVideoMode( event.resize.w, event.resize.h, 0, SDL_HWSURFACE | SDL_ANYFORMAT | SDL_RESIZABLE );
					if (!display)
					{
						SDL_Quit();
						fprintf( stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError() );
						tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
						return -5;
					}
					
					if (Green_IsDocValid( rtd, rtd->doc_cur ))
						Green_ValidateOffset( rtd->docs[rtd->doc_cur], ViewWidth( rtd ), display->h );
					
					flags |= FLAG_RENDER;
					break;
				case SDL_MOUSEMOTION:
					if (rtd->mouse.visibility > 0)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					break;
				case SDL_MOUSEBUTTONDOWN:
					if (rtd->mouse.visibility > 0)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					if (!(rtd->mouse.flags&0x01) || !Green_IsDocValid( rtd, rtd->doc_cur ))
						break;
					
					switch (event.button.button)
					{
						case SDL_BUTTON_LEFT:
							left_x = event.button.x;
							left_y = event.button.y;
							break;
						case SDL_BUTTON_RIGHT:
							right_x = event.button.x;
							right_y = event.button.y;
							break;
						case SDL_BUTTON_WHEELDOWN:
							Green_Zoom( rtd->docs[rtd->doc_cur], ViewWidth( rtd ), display->h, rtd->docs[rtd->doc_cur]->finescale * rtd->zoomstep );
							Render( rtd );
							break;
						case SDL_BUTTON_WHEELUP:
							Green_Zoom( rtd->docs[rtd->doc_cur], ViewWidth( rtd ), display->h, rtd->docs[rtd->doc_cur]->finescale / rtd->zoomstep );
							Render( rtd );
							break;
					}
					
					break;
				case SDL_MOUSEBUTTONUP:
					if (rtd->mouse.visibility)
					{
						mouse_last = SDL_GetTicks();
						SDL_ShowCursor( SDL_ENABLE );
					}
					
					if (!(rtd->mouse.flags&0x01) || !Green_IsDocValid( rtd, rtd->doc_cur ))
						break;
					
					if (event.button.button == SDL_BUTTON_RIGHT)
					{
						Green_ScrollRelative( rtd->docs[rtd->doc_cur], right_x - event.button.x, right_y - event.button.y, ViewWidth( rtd ), display->h, 1 );
						flags |= FLAG_RENDER;
					}
					
					break;
				case SDL_USEREVENT:
					// Force render if in search or goto mode for cursor blinking
					if (g_state == SEARCH || g_state == GOTO)
						flags |= FLAG_RENDER;
					
					if (rtd->mouse.visibility > 0)
					{
						mouse_cur = SDL_GetTicks();
						if ((Uint32)(mouse_cur - mouse_last) > rtd->mouse.visibility)
							SDL_ShowCursor( SDL_DISABLE );
					}
					
					SDL_GetMouseState( &x, &y );
					if (rtd->mouse.border_size && Green_IsDocValid( rtd, rtd->doc_cur ) && x >= 0 && y >= 0 && x <= display->w && y <= display->h)
					{
						width = display->w * rtd->mouse.border_size / 100;
						height = display->h * rtd->mouse.border_size / 100;
						
						if (x < width)
							width = -((width - x) * display->w / width * rtd->mouse.border_speed * live_interval / 1000);
						else if (x > display->w - width)
							width = (x + width - display->w) * display->w / width * rtd->mouse.border_speed * live_interval / 1000;
						else
							width = 0;
						
						if (y < height)
							height = -((height - y) * display->h / height * rtd->mouse.border_speed * live_interval / 1000);
						else if (y > display->h - height)
							height = (y + height - display->h) * display->h / height * rtd->mouse.border_speed * live_interval / 1000;
						else
							height = 0;
						
						if (width || height)
						{
							Green_ScrollRelative( rtd->docs[rtd->doc_cur], width, height, ViewWidth( rtd ), display->h, 0 );
							flags |= FLAG_RENDER;
						}
					}
					
					break;
			}
			
			event_count++;
			
		}	while (event_count && SDL_PollEvent( &event ));
		
	}	while (!(flags&FLAG_QUIT));
	
	SDL_RemoveTimer( timer );
	SDL_Quit();
	
	/* Flush any remaining SDL events */
	SDL_Event dummy;
	while (SDL_PollEvent(&dummy));
	
	/* Clear terminal input buffer using tcflush */
	tcflush(STDIN_FILENO, TCIFLUSH);
	
	/* Restore original terminal settings */
	tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
	
	return 0;
}
