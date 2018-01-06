#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define KFN_ID	0x006e666b

#define PIXEL_ALLOC	(16*1024)

#define UNICODE_MARK 	0xFEFF

// kfn info
typedef struct
{
	uint32_t id;
	uint8_t format;
	uint8_t reserved;
	uint16_t line_height;
	uint16_t num_ranges;
} __attribute__((packed)) kfn_head_t;

// character info
typedef struct
{
	uint16_t w, h;
	int16_t x, y, s;
	uint32_t pixo;
} __attribute__((packed)) kfn_cinfo_t;

// ranges
typedef struct
{
	uint16_t first;
	uint16_t count;
} kfn_range_t;

// ranges (memory)
typedef struct range_s
{
	struct range_s *next;
	struct range_s *prev;
	uint16_t first;
	uint16_t count;
	kfn_cinfo_t *info;
} range_t;

range_t *first_range;
range_t *top_range;
int numranges;

// pixels
uint8_t *pixbase;
int pixcount;
int pixmax;

// FT
FT_Library library;
FT_Face face;
int line_height;

uint8_t *request_pixels(int w, int h)
{
	uint8_t *ret;

	w *= h;
	if(pixcount + w > pixmax)
	{
		uint8_t *tmp;

		tmp = realloc(pixbase, pixmax + PIXEL_ALLOC);
		if(!tmp)
			return NULL;
		pixmax += PIXEL_ALLOC;
		pixbase = tmp;
	}
	ret = pixbase + pixcount;
	pixcount += w;
	return ret;
}

int range_join_check()
{
	range_t *range = first_range;

	while(range)
	{
		if(range->next && range->first + range->count == range->next->first)
		{
			range_t *del = range->next;
			// join these ranges
			range->count += range->next->count;
			range->next = range->next->next;
			if(range->next)
				range->next->prev = range;
			if(del == top_range)
				top_range = range;
			free(del);
			numranges--;
			return 1;
		}
		range = range->next;
	}

	return 0;
}

void add_char(uint16_t in)
{
	if(!first_range)
	{
		first_range = malloc(sizeof(range_t));
		if(!first_range)
			return;

		first_range->next = NULL;
		first_range->prev = NULL;
		first_range->first = in;
		first_range->count = 1;
		top_range = first_range;

		numranges++;
	} else
	{
		// go trough existing ranges
		range_t *range = first_range;

		while(range)
		{
			if(in >= range->first && in < range->first + range->count)
			{
				// already in
				return;
			} else
			if(in == range->first + range->count)
			{
				// add to existing range
				range->count++;
				// done
				return;
			} else
			if(in < range->first)
			{
				// create new range
				range_t *new;

				new = malloc(sizeof(range_t));
				if(!new)
					return;

				new->next = range;
				new->prev = range->prev;
				new->first = in;
				new->count = 1;
				if(range->prev)
					range->prev->next = new;
				range->prev = new;
				if(range == first_range)
					first_range = new;
				numranges++;
				// join check
				while(range_join_check());
				// done
				return;
			}
			range = range->next;
		}
		// create new range
		range = malloc(sizeof(range_t));
		if(!range)
			return;

		range->next = NULL;
		range->prev = top_range;
		range->first = in;
		range->count = 1;
		top_range->next = range;
		top_range = range;
		numranges++;
	}
}

void draw_char(FT_GlyphSlotRec *glyph, kfn_cinfo_t *info)
{
	uint8_t *dst;

	if(glyph->bitmap.width && glyph->bitmap.rows)
	{
		dst = request_pixels(glyph->bitmap.width, glyph->bitmap.rows);
		info->w = glyph->bitmap.width;
		info->h = glyph->bitmap.rows;
		memcpy(dst, glyph->bitmap.buffer, info->w * info->h);
		info->pixo = (uint32_t)(dst - pixbase);
	} else
	{
		info->w = 0;
		info->h = 0;
		info->pixo = 0xFFFFFFFF;
	}

	info->s = glyph->advance.x / 64;
	info->x = glyph->bitmap_left;
	info->y = line_height - glyph->bitmap_top;
}

int main(int argc, char **argv)
{
	int i, err, pixo;
	int font_size = 16 * 64;
	int glyph_index;
	FILE *f;
	range_t *range;
	kfn_head_t kfnhead;

	printf("kgsws' font converter\n");

	if(argc < 3)
	{
		printf("usage: %s font.ttf size\n", argv[0]);
		return 1;
	}

	if(sscanf(argv[2], "%d", &font_size) != 1)
		font_size = 16;
	if(font_size < 0)
		font_size = -font_size;
	font_size *= 32;
	if(font_size < 10 * 64)
		font_size = 10 * 64;

	// load character list
	f = fopen("chars.txt", "rb");
	if(!f)
	{
		printf("- failed to open chars.txt");
	}

	// parse character list
	while(1)
	{
		int len;
		uint16_t in;

		len = fread(&in, 1, sizeof(in), f);
		if(len != sizeof(in))
			break;

		if(in < ' ')
			continue;
		if(in == UNICODE_MARK)
			continue;

		add_char(in);
	}

	fclose(f);

	// init freetype
	err = FT_Init_FreeType(&library);
	if(err)
	{
		printf("- FreeType Init error %i\n", err);
		return 1;
	}

	err = FT_New_Face(library, argv[1], 0, &face);
	if(err)
	{
		printf("- FreeType load error %i\n", err);
		return 1;
	}

	err = FT_Set_Char_Size(face, 0, font_size, 0, 0);
	if(err)
	{
		printf("- FreeType size set error %i\n", err);
		return 1;
	}

	FT_Select_Charmap(face, FT_ENCODING_UNICODE);

	line_height = face->size->metrics.y_ppem;

	// build ranges with images
	pixo = sizeof(kfn_head_t);
	range = first_range;
	while(range)
	{
		pixo += sizeof(kfn_range_t) + range->count * sizeof(kfn_cinfo_t);
		range->info = malloc(range->count * sizeof(kfn_cinfo_t));
		for(i = 0; i < range->count; i++)
		{
			// render glyph
			glyph_index = FT_Get_Char_Index(face, range->first + i);
			err = FT_Load_Glyph(face, glyph_index, 0);
			if(err)
			{
				printf("- FreeType glyph error %i for character 0x%04X\n", err, range->first + i);
				return 1;
			}
			err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
			if(err)
			{
				printf("- FreeType glyph render error %i for character 0x%04X\n", err, range->first + i);
				return 1;
			}
			// add pixels and info
			draw_char(face->glyph, range->info + i);
		}

		range = range->next;
	}

	// create KFN
	f = fopen("out.kfn", "wb");

	kfnhead.id = KFN_ID;
	kfnhead.format = 0;
	kfnhead.line_height = line_height;
	kfnhead.num_ranges = numranges;

	// header
	fwrite(&kfnhead, 1, sizeof(kfn_head_t), f);

	// ranges
	range = first_range;
	while(range)
	{
		// recalc pixel offsets first
		for(i = 0; i < range->count; i++)
			if(range->info[i].pixo != 0xFFFFFFFF)
				range->info[i].pixo += pixo;
			else
				range->info[i].pixo = 0;
		// store this range
		fwrite(&range->first, 2, sizeof(uint16_t), f);
		// store range data
		fwrite(range->info, range->count, sizeof(kfn_cinfo_t), f);
		// next
		range = range->next;
	}

	// pixels
	fwrite(pixbase, 1, pixcount, f);

	fclose(f);

	printf("- finished\n");

	return 0;
}

