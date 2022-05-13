#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <SDL/SDL.h>
#include "text.h"

#define KFN_ID	0x006e666b

#define SX 1024
#define SY 768

#define TEXT_CURCHAR_X	(512+128+64)

#define TEXT_INFOTEXT_X	(32)
#define TEXT_INFOTEXT_Y	(736)

#define TEXT_AREA_X	(512+128)
#define TEXT_AREA_Y	(64)
#define TEXT_AREA_W	(380)
#define TEXT_AREA_H	(512)

#define INPUT_X	((SX/2)-128)
#define INPUT_Y ((SY/2)-32)
#define INPUT_W 256
#define INPUT_H 64

typedef struct
{
	uint8_t idlen;
	uint8_t colortype;
	uint8_t imagetype;
	unsigned short pals;
	unsigned short paln;
	uint8_t pald;
	short xoffset;
	short yoffset;
	unsigned short width;
	unsigned short height;
	uint8_t bpp;
	uint8_t flags;
} __attribute__((packed)) tgahead_t;

tgahead_t savehead =
{
	0,
	0,
	3,
	0,
	0,
	0,
	0,
	0xFFFF, // h
	0xFFFF, // w
	0xFFFF, // h
	8,
	0x20
};

tgahead_t loadhead;

// font stuff

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

// internal storage
typedef struct znak_s
{
	struct znak_s *next;
	struct znak_s *prev;
	uint16_t code;
	int width;
	int height;
	int xpow;	// for OpenGL
	int space;
	int xoffs;
	int yoffs;
	int gltex;
	uint32_t pixo;
	uint8_t pixels[];
} znak_t;

// mouse click
typedef struct
{
	int x, y;
	char *text;
	void (*func)();
} clickzone_t;

void load_tga();
void save_tga();
void export_bin();
void char_goto();
void char_new();
void font_combine();

clickzone_t czones[] =
{
	{32, 686, "Import Character", load_tga},
	{32, 704, "Export Character", save_tga},
	{256, 686, "Export to out.kfn", export_bin},
	{480, 686, "Goto Character", char_goto},
	{480, 704, "New Character", char_new},
	{704, 686, "Combine in.bitf", font_combine},
};

char *fontfile;
float scale = 10.0f;

SDL_Surface *screen;
int konec = 0;
int mx = 0;
int my = 0;

int font_height;
znak_t *charlist;
znak_t *curchar;

char *infotext = NULL;
float infolevel = 0.0f;

int getnum = 0;
char textnum[10];
void (*numfunc)(int);
char *numtitle = NULL;

int dual_new[2];

float test_text_back[] = {0.0f, 0.0f, 0.0f};
float test_text_color[] = {1.0f, 1.0f, 1.0f};
char test_text[1024] = "ABCDEFGHIJLKMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz";

int getpow(int in)
{
	int i = 4;
	while(i < in)
		i *= 2;
	return i;
}

void GL_AddTextureFromBuffer(int tex, void *buff, int width, int height)
{
	// loading 8bpp
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, buff);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void InfoText(char *text)
{
	infotext = text;
	infolevel = 2.5f;
}

void GetNum(char *text, void (*func)(int), int start)
{
	memset(textnum, 0, sizeof(textnum));
	if(start >= 0)
		sprintf(textnum, "%X", start);
	getnum = strlen(textnum) | 0x80;
	numfunc = func;
	numtitle = text;
}

char *UTF8(char *in, uint16_t *code)
{
	int count;

	if(!*in)
	{
		*code = 0;
		return in;
	}

	if(!(*in & 0x80))
	{
		*code = *in;
		return in+1;
	}

	if((*in & 0xE0) == 0xC0)
	{
		count = 1;
		*code = (*in & 0x1F);
	} else
	if((*in & 0xF0) == 0xE0)
	{
		count = 2;
		*code = (*in & 0x0F);
	} else {
		*code = '?';
		return in+1;
	}

	in++;

	while(count--)
	{
		if(!*in)
		{
			*code = 0;
			return in;
		}
		if((*in & 0xC0) != 0x80)
		{
			*code = '?';
			return in;
		}
		*code <<= 6;
		*code |= *in & 0x3F;
		in++;
	}

	return in;
}

void LoadText()
{
	FILE *f;

	f = fopen("test.txt", "rb");
	if(f)
	{
		memset(test_text, 0, sizeof(test_text));
		fread(test_text, 1, sizeof(test_text)-1, f);
		fclose(f);
	}
}

znak_t *CharByCode(int code)
{
	znak_t *zn = charlist;
	while(zn)
	{
		if(zn->code == code)
			return zn;
		zn = zn->next;
	}
	return NULL;
}

void func_goto(int newpos)
{
	znak_t *zn = charlist;
	while(zn)
	{
		if(zn->code == newpos)
		{
			curchar = zn;
			break;
		}
		zn = zn->next;
	}
}

void func_create(int newcode)
{
	znak_t *zn = charlist;
	znak_t *new;
	znak_t *old = curchar;
	// check
	if(newcode > 0xFFFF || newcode < 0x20)
		return;
	// search
	if(CharByCode(newcode))
	{
		InfoText("* this character does already exist");
		return;
	}
	// find slot
	zn = charlist;
	while(1)
	{
		if(zn->code > newcode)
		{
			zn = zn->prev;
			break;
		}
		if(!zn->next)
			break;
		zn = zn->next;
	}
	// add
	new = malloc(sizeof(znak_t));
	new->prev = zn;
	new->next = zn->next;
	if(zn->next)
		zn->next->prev = new;
	zn->next = new;
	curchar = new;
	curchar->code = newcode;
	curchar->height = curchar->width = 0;
	load_tga();
	if(!curchar->height)
	{
		// load error, delete
		curchar->prev->next = curchar->next;
		if(curchar->next)
			curchar->next->prev = curchar->prev;
		free(curchar);
		curchar = old;
		return;
	}
	curchar->xoffs = 0;
	curchar->yoffs = 0;
	curchar->space = curchar->width;
}

void DeleteChar(znak_t *zn)
{
	znak_t *temp = zn;
	zn->prev->next = zn->next;
	if(zn->next)
		zn->next->prev = zn->prev;

	glDeleteTextures(1, &temp->gltex);

	if(zn == curchar)
		curchar = zn->prev;

	free(temp);
}

void Input_GetNum()
{
	SDL_Event event;

	while(SDL_PollEvent(&event))
	{      
		if(event.type == SDL_QUIT)
			konec = 1;
		if(event.type == SDL_MOUSEMOTION)
		{
			mx = event.motion.x;
			my = event.motion.y;
		}
		if(event.type == SDL_KEYDOWN)
		{
			if(event.key.keysym.sym == SDLK_ESCAPE)
			{
				numfunc = NULL;
				getnum = 0;
			}
			if(event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER)
			{
				if(numfunc)
				{
					int get;
					if(sscanf(textnum, "%X", &get) == 1)
					{
						getnum = 0;
						numfunc(get);
					}
				} else
					getnum = 0;
			}
			if(event.key.keysym.sym == SDLK_BACKSPACE)
			{
				if(getnum & 0x7F)
				{
					getnum = (getnum & 0x7F) - 1;
					textnum[getnum] = 0;
					getnum |= 0x80;
				}
			}
			if((getnum & 0x7F) < 8)
			{
				if(event.key.keysym.sym >= SDLK_KP0 && event.key.keysym.sym <= SDLK_KP9)
				{
					getnum &= 0x7F;
					textnum[getnum++] = '0' + event.key.keysym.sym - SDLK_KP0;
					getnum |= 0x80;
				}
				if(event.key.keysym.sym >= SDLK_0 && event.key.keysym.sym <= SDLK_9)
				{
					getnum &= 0x7F;
					textnum[getnum++] = '0' + event.key.keysym.sym - SDLK_0;
					getnum |= 0x80;
				}
				if(event.key.keysym.sym >= SDLK_a && event.key.keysym.sym <= SDLK_f)
				{
					getnum &= 0x7F;
					textnum[getnum++] = 'A' + event.key.keysym.sym - SDLK_a;
					getnum |= 0x80;
				}
			}
		}
	}
}

void Input()
{
	SDL_Event event;

	while(SDL_PollEvent(&event))
	{      
		if(event.type == SDL_QUIT)
			konec = 1;
		if(event.type == SDL_KEYDOWN)
		{
			switch(event.key.keysym.sym)
			{
				// ZOOM
				case SDLK_PAGEDOWN:
//					if(scale > 4.0f)
//						scale -= 2.0f;
					font_height--;
				break;
				case SDLK_PAGEUP:
//					if(scale < 16.0f)
//						scale += 2.0f;
					font_height++;
				break;
				// MOVE
				case SDLK_KP_MINUS:
					if(curchar->prev)
						curchar = curchar->prev;
				break;
				case SDLK_KP_PLUS:
					if(curchar->next)
						curchar = curchar->next;
				break;
				// OFFSETs
				case SDLK_UP:
					curchar->yoffs--;
				break;
				case SDLK_DOWN:
					curchar->yoffs++;
				break;
				case SDLK_LEFT:
					curchar->xoffs--;
				break;
				case SDLK_RIGHT:
					curchar->xoffs++;
				break;
				// WIDTH
				case SDLK_SPACE:
					curchar->space++;
				break;
				case SDLK_BACKSPACE:
					if(curchar->space)
						curchar->space--;
				break;
				// DELETE
				case SDLK_DELETE:
					if(curchar->code > 0x7F)
						DeleteChar(curchar);
				break;
				// END
				case SDLK_END:
					while(1)
					{
						if(!curchar->next)
							break;
						curchar = curchar->next;
					}
				break;
				// HOME
				case SDLK_HOME:
					curchar = charlist;
				break;
				// GOTO
				case SDLK_g:
					GetNum("Goto character", func_goto, curchar->code);
				break;
				// NEW
				case SDLK_n:
					GetNum("Create character", func_create, -1);
				break;
				// TEST
				case SDLK_KP_ENTER:
					InfoText("It is alive ... ALIVE.");
				break;
			}
		}
		if(event.type == SDL_MOUSEMOTION)
		{
			mx = event.motion.x;
			my = event.motion.y;
		}
		if(event.type == SDL_MOUSEBUTTONDOWN)
		{
			if(event.button.button == SDL_BUTTON_LEFT)
			{
				int i;
				mx = event.button.x;
				my = event.button.y;
				for(i = 0; i < sizeof(czones) / sizeof(clickzone_t); i++)
				{
					if(czones[i].func && mx >= czones[i].x && mx < czones[i].x + 16 + strlen(czones[i].text)*8 && my >= czones[i].y && my < czones[i].y + 16)
						czones[i].func();
				}
				if(mx >= TEXT_AREA_X && mx < TEXT_AREA_X + TEXT_AREA_W && my >= TEXT_AREA_Y && my < TEXT_AREA_Y + TEXT_AREA_H)
				{
					if(test_text_color[0])
					{
						test_text_color[0] = test_text_color[1] = test_text_color[2] = 0.0f;
						test_text_back[0] = test_text_back[1] = test_text_back[2] = 1.0f;
					} else {
						test_text_color[0] = test_text_color[1] = test_text_color[2] = 1.0f;
						test_text_back[0] = test_text_back[1] = test_text_back[2] = 0.0f;
					}
				}
			}
			if(event.button.button == SDL_BUTTON_RIGHT)
			{
				if(mx >= TEXT_AREA_X && mx < TEXT_AREA_X + TEXT_AREA_W && my >= TEXT_AREA_Y && my < TEXT_AREA_Y + TEXT_AREA_H)
					LoadText();
			}
		}
	}
}

void DrawFrame(int x, int y, int w, int h)
{
	glBegin(GL_LINE_LOOP);
	glVertex3i(x, y, 0);
	glVertex3i(x + w, y, 0);
	glVertex3i(x + w, y + h, 0);
	glVertex3i(x, y + h, 0);
	glEnd();
}

void DrawBox(int x, int y, int w, int h)
{
	glBegin(GL_TRIANGLE_STRIP);
	glVertex3i(x, y, 0);
	glVertex3i(x + w, y, 0);
	glVertex3i(x, y + h, 0);
	glVertex3i(x + w, y + h, 0);
	glEnd();
}

void DrawChar(znak_t *zn)
{
	glBindTexture(GL_TEXTURE_2D, zn->gltex);
	glEnable(GL_TEXTURE_2D);
	glBegin(GL_TRIANGLE_STRIP);
		glTexCoord2f(0.0f, 0.0f);
		glVertex3i(zn->xoffs, zn->yoffs, 0);
		glTexCoord2f(1.0f, 0.0f);
		glVertex3i(zn->xoffs + zn->xpow, zn->yoffs, 0);
		glTexCoord2f(0.0f, 1.0f);
		glVertex3i(zn->xoffs, zn->yoffs + zn->height, 0);
		glTexCoord2f(1.0f, 1.0f);
		glVertex3i(zn->xoffs + zn->xpow, zn->yoffs + zn->height, 0);
	glEnd();
	glDisable(GL_TEXTURE_2D);
}

void DrawText(int x, int y, char *text)
{
	uint16_t code;
	int sx = x;

	glPushMatrix();

	while(*text)
	{
		text = UTF8(text, &code);
		if(code == 0x20)
			x += charlist->space;
		if(code == '\n')
		{
			x = sx;
			y += font_height;
		}
		if(code > 0x20)
		{
			znak_t *zn = CharByCode(code);
			if(zn)
			{
				glLoadIdentity();
				glTranslatef(x, y, 0);
				DrawChar(zn);
				x += zn->space;
			}
		}
	}

	glPopMatrix();
}

void Draw()
{
	int i;

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// clickable text
	for(i = 0; i < sizeof(czones) / sizeof(clickzone_t); i++)
	{
		int m, x;
		if(!czones[i].func)
			continue;
		glColor3f(1.0f, 0.4f, 0.0f);
		x = T_Write(czones[i].x + 8, czones[i].y, czones[i].text) + 8;
		if(mx >= czones[i].x && mx < x && my >= czones[i].y && my < czones[i].y + 16)
		{
			glColor3f(0.0f, 0.0f, 0.0f);
			m = 1;
		} else {
			glColor3f(0.2f, 0.2f, 0.2f);
			m = 0;
		}
		glBegin(GL_TRIANGLE_STRIP);
			glVertex3i(czones[i].x, czones[i].y, 0);
			glVertex3i(x, czones[i].y, 0);
			glVertex3i(czones[i].x, czones[i].y + 16, 0);
			glVertex3i(x, czones[i].y + 16, 0);
		glEnd();
		if(m)
			glColor3f(1.0f, 1.0f, 1.0f);
		else
			glColor3f(0.6f, 0.6f, 0.6f);
		glBegin(GL_LINE_LOOP);
			glVertex3i(czones[i].x, czones[i].y, 0);
			glVertex3i(x, czones[i].y, 0);
			glVertex3i(x, czones[i].y + 17, 0);
			glVertex3i(czones[i].x, czones[i].y + 17, 0);
		glEnd();
	}

	// bottom help; key list
	T_WriteSpecial(8, SY - 16, "\b\x8C[PageUp/PageDown]\b\x8F Height \b\x8C [Arrows]\b\x8F Change offset \b\x8C [Spc/BkSpc]\b\x8F Change width \b\x8C [+/-]\b\x8F Change character \b\x8C [Del]\b\x8F Delete character");

	// info text
	if(infotext)
	{
		glColor3f(infolevel, infolevel, infolevel);
		T_Write(TEXT_INFOTEXT_X, TEXT_INFOTEXT_Y, infotext);
		infolevel -= 0.04f;
		if(infolevel <= 0.0f)
			infotext = NULL;
	}

	// top text; selected character
	if(curchar->code > 0x7F)
	{
		char text[256];
		sprintf(text, "\b<Current character \b?0x%04X\b< (UNICODE)", curchar->code);
		T_WriteSpecial(TEXT_CURCHAR_X, 16, text);
	} else
	{
		int x = TEXT_CURCHAR_X;
		char text[256];
		glColor3f(0.0f, 1.0f, 1.0f);
		x = T_Write(x, 16, "Current character '");
		glColor3f(1.0f, 1.0f, 1.0f);
		glEnable(GL_TEXTURE_2D);
		T_PutChar(x, 16, curchar->code);
		glColor3f(0.0f, 1.0f, 1.0f);
		sprintf(text, "' (0x%02X)", curchar->code);
		x = T_Write(x+8, 16, text);
	}

	// test text
	glColor3f(1.0f, 1.0f, 0.0f);
	T_Write(TEXT_AREA_X + 128, TEXT_AREA_Y - 2 - 16, "Font test:");
	glColor3fv(test_text_back);
	DrawBox(TEXT_AREA_X - 2, TEXT_AREA_Y - 2, TEXT_AREA_W + 4, TEXT_AREA_H + 4);
	glColor3f(0.4f, 0.4f, 0.4f);
	DrawFrame(TEXT_AREA_X - 2, TEXT_AREA_Y - 2, TEXT_AREA_W + 4, TEXT_AREA_H + 4);
	glColor3fv(test_text_color);
	DrawText(TEXT_AREA_X, TEXT_AREA_Y, test_text);

	// input
	if(getnum)
	{
		int x;
		glColor3f(0.3f, 0.3f, 0.3f);
		glBegin(GL_TRIANGLE_STRIP);
			glVertex3i(INPUT_X, INPUT_Y, 1);
			glVertex3i(INPUT_X + INPUT_W, INPUT_Y, 1);
			glVertex3i(INPUT_X, INPUT_Y + INPUT_H, 1);
			glVertex3i(INPUT_X + INPUT_W, INPUT_Y + INPUT_H, 1);
		glEnd();
		glColor3f(1.0f, 1.0f, 1.0f);
		glBegin(GL_TRIANGLE_STRIP);
			glVertex3i(INPUT_X + 14, INPUT_Y + 30, 1);
			glVertex3i(INPUT_X + INPUT_W - 18, INPUT_Y + 30, 1);
			glVertex3i(INPUT_X + 14, INPUT_Y + 50, 1);
			glVertex3i(INPUT_X + INPUT_W - 18, INPUT_Y + 50, 1);
		glEnd();
		T_Write(INPUT_X + 32, INPUT_Y + 8, numtitle);
		glColor3f(0.0f, 0.0f, 0.0f);
		x = T_Write(INPUT_X + 16, INPUT_Y + 32, textnum);
		glEnable(GL_TEXTURE_2D);
		T_PutChar(x, INPUT_Y + 32, '_');
		glDisable(GL_TEXTURE_2D);
	}

	// scale
	glTranslatef(96.0f, 96.0f, 0.0f);
	glScalef(scale, scale, 1.0f);

	// CHAR
	if(curchar->width && curchar->height)
	{
		glColor3f(1.0f, 1.0f, 1.0f);
		DrawChar(curchar);
	}

	// GRID
	glBegin(GL_LINES);
	for(i = -16; i <= 32; i++)
	{
		if(!i)
			continue;
		if(i & 1)
			glColor3f(0.4f, 0.4f, 0.4f);
		else
			glColor3f(0.5f, 0.5f, 0.5f);
		glVertex3i(i, -16, 0);
		glVertex3i(i, 32, 0);
		glVertex3i(-16, i, 0);
		glVertex3i(32, i, 0);
	}
	glColor3f(0.8f, 0.8f, 0.8f);
	glVertex3i(0, -17, 0);
	glVertex3i(0, 33, 0);
	glVertex3i(-17, 0, 0);
	glVertex3i(33, 0, 0);
	glEnd();

	// frame
	if(curchar->width && curchar->height)
	{
		// FRAME
		glColor3f(1.0f, 0.0f, 0.0f);
		DrawFrame(0, 0, curchar->space, font_height);
		// character
		glColor3f(0.0f, 1.0f, 0.0f);
		DrawFrame(curchar->xoffs, curchar->yoffs, curchar->width, curchar->height);
	} else {
		// FRAME
		glColor3f(1.0f, 0.0f, 0.0f);
		DrawFrame(0, 0, curchar->space, font_height);
		// X
		glColor3f(1.0f, 0.0f, 0.0f);
		glBegin(GL_LINES);
		glVertex3i(0, 0, 0);
		glVertex3i(curchar->space, font_height, 0);
		glVertex3i(curchar->space, 0, 0);
		glVertex3i(0, font_height, 0);
		glEnd();
	}

	SDL_GL_SwapBuffers();
}

int LoadCharacter(FILE *f, kfn_cinfo_t *info, uint16_t code)
{
	znak_t *new;
	int x, y;
	uint8_t *src, *dst, *tmp;
	int xpow = getpow(info->w);
	int count = info->w * info->h;

	new = malloc(sizeof(znak_t) + count);
	if(!new)
		return 1;
	new->prev = curchar;
	new->next = NULL;
	new->code = code;
	new->xpow = xpow;
	new->width = info->w;
	new->height = info->h;
	new->space = info->s;
	new->xoffs = info->x;
	new->yoffs = info->y;

	if(curchar)
		curchar->next = new;
	curchar = new;

	if(!charlist)
		charlist = new;

	if(!count)
		// ok; no pixels
		return 0;

	fseek(f, info->pixo, SEEK_SET);
	fread(new->pixels, info->w, info->h, f);

	tmp = malloc(xpow * info->h);
	if(!tmp)
		// meh; will be invisible
		return 0;

	src = new->pixels;
	dst = tmp;

	for(y = 0; y < info->h; y++)
		for(x = 0; x < xpow; x++)
		{
			if(x < info->w)
			{
				*dst = *src;
				src++;
			} else
			{
				*dst = 0;
			}
			dst++;
		}

	glGenTextures(1, &new->gltex);
	GL_AddTextureFromBuffer(new->gltex, tmp, new->xpow, new->height);

	free(tmp);

	return 0;
}

int LoadFont(char *file)
{
	FILE *f;
	kfn_head_t head;
	int i;

	f = fopen(file, "rb");
	if(!f)
		return 1;

	fread(&head, 1, sizeof(kfn_head_t), f);

	if(head.id != KFN_ID || head.format)
	{
		fclose(f);
		return 1;
	}

	font_height = head.line_height;

	// go trough all the ranges
	for(i = 0; i < head.num_ranges; i++)
	{
		kfn_range_t range;
		int j;

		fread(&range, 1, sizeof(kfn_range_t), f);

		// go trough all characters
		for(j = 0; j < range.count; j++)
		{
			kfn_cinfo_t info;
			int offs;

			fread(&info, 1, sizeof(kfn_cinfo_t), f);
			offs = ftell(f);

			if(LoadCharacter(f, &info, range.first + j))
			{
				fclose(f);
				return 1;
			}

			fseek(f, offs, SEEK_SET);
		}
	}

	fclose(f);

	return 0;
}

void font_combine()
{
//	InfoText("* fonts combined");
	return;
}

void save_tga()
{
	// save character to TGA
	FILE *f;
	char temp[16];
	if(!curchar->width || !curchar->height)
		return;
	sprintf(temp, "char/%04X.tga", curchar->code);
	f = fopen(temp, "wb");
	if(f)
	{
		savehead.yoffset = curchar->height;
		savehead.width = curchar->width;
		savehead.height = curchar->height;
		fwrite(&savehead, 1, sizeof(tgahead_t), f);
		fwrite(curchar->pixels, curchar->height, curchar->width, f);
		fclose(f);
		InfoText("* character exported");
	} else
		InfoText("* export error: Failed to create file");
}

void load_tga()
{
	// load character from TGA
	FILE *f;
	char temp[16];
	tgahead_t loadhead;
	if(curchar->code == ' ')
		return;
	sprintf(temp, "char/%04X.tga", curchar->code);
	f = fopen(temp, "rb");
	if(f)
	{
		fread(&loadhead, 1, sizeof(tgahead_t), f);
		if(loadhead.yoffset == loadhead.height)
		{
			int w = loadhead.width;
			int h = loadhead.height;
			loadhead.yoffset = savehead.yoffset;
			loadhead.width = savehead.width;
			loadhead.height = savehead.height;
			if(!memcmp(&loadhead, &savehead, sizeof(tgahead_t)) && w <= 4096 && h <= 4096)
			{
				int xpow = getpow(w);
				int count = w * h;
				znak_t *new;
				uint8_t *src, *dst, *tmp;
				int x, y;

				new = malloc(sizeof(znak_t) + count);
				if(!new)
				{
					fclose(f);
					InfoText("* import error: Memory error");
					return;
				}

				new->code = curchar->code;
				new->width = w;
				new->height = h;
				new->xpow = xpow;
				new->space = curchar->space;
				new->xoffs = curchar->xoffs;
				new->yoffs = curchar->yoffs;

				fread(new->pixels, h, w, f);

				tmp = malloc(xpow * h);

				src = new->pixels;
				dst = tmp;
				for(y = 0; y < new->height; y++)
					for(x = 0; x < xpow; x++)
					{
						if(x < new->width)
						{
							*dst = *src;
							src++;
						} else
						{
							*dst = 0;
						}
						dst++;
					}

				new->prev = curchar->prev;
				new->next = curchar->next;
				curchar->prev->next = new;
				if(curchar->next)
					curchar->next->prev = new;

				glDeleteTextures(1, &curchar->gltex);

				if(curchar == charlist)
					charlist = new;

				free(curchar);
				curchar = new;

				glGenTextures(1, &new->gltex);
				GL_AddTextureFromBuffer(new->gltex, tmp, new->xpow, new->height);

				free(tmp);

				fclose(f);
				InfoText("* character imported");
				return;
			}
		}
		fclose(f);
		InfoText("* import error: TGA format error");
	} else
		InfoText("* import error: Failed to open file");
}

void export_bin()
{
	FILE *f;
	kfn_head_t head;
	znak_t *znak, *temp;
	int count, i;
	uint32_t pixo, pixs;
	uint16_t last;

	// prepare header
	head.id = KFN_ID;
	head.format = 0;
	head.reserved = 0;
	head.line_height = font_height;
	head.num_ranges = 0;

	// count all the ranges and pixel offsets
	pixo = 0;
	pixs = sizeof(kfn_head_t);
	count = 0;
	last = charlist->code;
	znak = charlist;
	temp = charlist;
	while(1)
	{
		if(znak->code != last)
		{
			kfn_range_t range;
			// mark this range
			range.first = temp->code;
			range.count = 1 + znak->prev->code - temp->code;
			head.num_ranges++;
			pixs += sizeof(kfn_range_t) + range.count * sizeof(kfn_cinfo_t);
			// mark all characters
			for(i = 0; i < range.count; i++)
			{
				temp->pixo = pixo;
				pixo += temp->width * temp->height;
				temp = temp->next;
			}
			// start next one
			temp = znak;
		}
		last = znak->code + 1;

		if(!znak->next)
		{
			kfn_range_t range;
			// mark this range
			range.first = temp->code;
			range.count = 1 + znak->code - temp->code;
			head.num_ranges++;
			pixs += sizeof(kfn_range_t) + range.count * sizeof(kfn_cinfo_t);
			// mark all characters
			for(i = 0; i < range.count; i++)
			{
				temp->pixo = pixo;
				pixo += temp->width * temp->height;
				temp = temp->next;
			}
			// done
			break;
		}

		znak = znak->next;
	}

	// create file
	f = fopen("out.kfn", "wb");
	if(!f)
	{
		InfoText("* failed to create out.kfn");
		return;
	}

	// store header
	fwrite(&head, 1, sizeof(kfn_head_t), f);

	// store all ranges and characters
	count = 0;
	last = charlist->code;
	znak = charlist;
	temp = charlist;
	while(1)
	{
		if(znak->code != last)
		{
			kfn_range_t range;
			// store this range
			range.first = temp->code;
			range.count = 1 + znak->prev->code - temp->code;
			fwrite(&range, 1, sizeof(kfn_range_t), f);
			// store all characters
			for(i = 0; i < range.count; i++)
			{
				kfn_cinfo_t info;

				info.w = temp->width;
				info.h = temp->height;
				info.x = temp->xoffs;
				info.y = temp->yoffs;
				info.s = temp->space;
				info.pixo = temp->pixo + pixs;

				fwrite(&info, 1, sizeof(kfn_cinfo_t), f);

				temp = temp->next;
			}
			// start next one
			temp = znak;
		}
		last = znak->code + 1;

		if(!znak->next)
		{
			kfn_range_t range;
			// store this range
			range.first = temp->code;
			range.count = 1 + znak->code - temp->code;
			fwrite(&range, 1, sizeof(kfn_range_t), f);
			// store all characters
			for(i = 0; i < range.count; i++)
			{
				kfn_cinfo_t info;

				info.w = temp->width;
				info.h = temp->height;
				info.x = temp->xoffs;
				info.y = temp->yoffs;
				info.s = temp->space;
				info.pixo = temp->pixo + pixs;

				fwrite(&info, 1, sizeof(kfn_cinfo_t), f);

				temp = temp->next;
			}
			// done
			break;
		}

		znak = znak->next;
	}

	// store all pixels
	znak = charlist;
	while(znak)
	{
		if(znak->width * znak->height)
			fwrite(znak->pixels, znak->width, znak->height, f);
		znak = znak->next;
	}

	fclose(f);

	InfoText("* font exported");
}

void char_goto()
{
	GetNum("Goto character", func_goto, curchar->code);
}

void char_new()
{
	GetNum("Create character", func_create, -1);
}

int main(int argc, char **argv)
{
	int i;
	FILE *f;
	char *font_name = "font.kfn";

	printf("kgsws' kfn font edit\n");
	if(argc > 1)
		font_name = argv[1];

	mkdir("char", 0777);

	if(SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("\t- SDL init error\n");
		return 1;
	}
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	screen = SDL_SetVideoMode(SX, SY, 32, SDL_OPENGL | SDL_GL_DOUBLEBUFFER | SDL_HWPALETTE | SDL_HWSURFACE | SDL_HWACCEL);
	if(!screen)
	{
		SDL_Quit();
		printf("- SDL video error\n");
		return 1;
	}

	SDL_WM_SetCaption("kgsws' KFN editor", NULL);
//	SDL_ShowCursor(0);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

	if(LoadFont(font_name))
	{
		SDL_Quit();
		printf("- failed to load font\n");
		return 1;
	}

	LoadText();

	glViewport(0, 0, SX, SY);
	glShadeModel(GL_SMOOTH);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(0.0f);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);

	glDepthFunc(GL_GEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glAlphaFunc(GL_GREATER, 0.0);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0f, SX, SY, 0.0f, 1.0f, -1.0f);

	T_Init();

	while(!konec)
	{
		Draw();
		if(getnum)
			Input_GetNum();
		else
			Input();
		usleep(10*1000);
	}
	SDL_Quit();

	printf("- finished\n");
	return 0;
}

