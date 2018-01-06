#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <SDL/SDL.h>
#include "text.h"

#include "vga_font.h"

#define CHAR_WIDTH	(1.0f/256.0f)

void GL_AddTextureFromBuffer(int tex, void *buff, int width, int height);

static int texture = -1;

void T_Init()
{
	int i, j, k;
	const uint8_t *src;
	uint8_t *znaky, *dst;

	znaky = malloc(16 * 8 * 256);
	if(!znaky)
		return;
	dst = znaky;

	glGenTextures(1, &texture);

	for(k = 0; k < 16; k++)
		for(j = 0; j < 256; j++)
		{
			src = vga_font + j * 16 + k;
			for(i = 0; i < 8; i++)
			{
				*dst = *src & (128 >> i) ? 0xFF : 0;
				dst++;
			}
		}

	GL_AddTextureFromBuffer(texture, znaky, 8 * 256, 16);
}

void T_PutChar(int x, int y, char in)
{
	glBegin(GL_TRIANGLE_STRIP);
		glTexCoord2f((float)in * CHAR_WIDTH, 0.0f);
		glVertex3i(x, y, 1);
		glTexCoord2f((float)in * CHAR_WIDTH + CHAR_WIDTH, 0.0f);
		glVertex3i(x + 8, y, 1);
		glTexCoord2f((float)in * CHAR_WIDTH, 1.0f);
		glVertex3i(x, y + 16, 1);
		glTexCoord2f((float)in * CHAR_WIDTH + CHAR_WIDTH, 1.0f);
		glVertex3i(x + 8, y + 16, 1);
	glEnd();
}

int T_Write(int x, int y, const char *text)
{
	glBindTexture(GL_TEXTURE_2D, texture);
	glEnable(GL_TEXTURE_2D);

	while(*text)
	{
		T_PutChar(x, y, *text);
		x += 8;
		text++;
	}

	glDisable(GL_TEXTURE_2D);

	return x;
}

void T_WriteSpecial(int x, int y, const char *text)
{
	int px = x;

	glColor3f(1.0f, 1.0f, 1.0f);
	glBindTexture(GL_TEXTURE_2D, texture);
	glEnable(GL_TEXTURE_2D);

	while(*text)
	{
		if(*text == '\n')
		{
			x = px;
			y += 16;
			text++;
			continue;
		}
		if(*text == '\b')
		{
			text++;
			if(!*text)
				break;
			glColor3f((*text & 3) * (1.0f/3.0f), ((*text >> 2) & 3) * (1.0f/3.0f), ((*text >> 4) & 3) * (1.0f/3.0f));
			text++;
			continue;
		}
		T_PutChar(x, y, *text);
		x += 8;
		text++;
	}

	glDisable(GL_TEXTURE_2D);
}

