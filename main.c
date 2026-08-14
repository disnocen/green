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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "green.h"


#define SCHEME_WIDTH			 1
#define SCHEME_HEIGHT			 2
#define SCHEME_FULLSCREEN		 3
#define SCHEME_FIT			 4
#define SCHEME_CURSORVISIBILITY		 5
#define SCHEME_BACKGROUNDCOLOR		 6
#define SCHEME_HIGHLIGHTCOLOR		 7
#define SCHEME_HIGHLIGHTALPHA		 8
#define SCHEME_CURSORBORDER		 9

#define RGB_TEXT "/usr/share/X11/rgb.txt"

struct SchemeData
{
	char	*name, *data;
	unsigned int	data_size;
	unsigned char	processed;
};

struct SchemeArray
{
	struct SchemeData	**scheme;
	unsigned int	n;
};

struct SchemeProperty
{
	const char	*name;
	unsigned int	id;
	unsigned char	processed;
};


int	Green_SDL_Main( Green_RTD *rtd );


struct SchemeProperty	scheme_property[] =
{
	{"Width", SCHEME_WIDTH, 0},
	{"Height", SCHEME_HEIGHT, 0},
	{"Fullscreen", SCHEME_FULLSCREEN, 0},
	{"Fit", SCHEME_FIT, 0},
	{"Cursor.Visibility", SCHEME_CURSORVISIBILITY, 0},
	{"Cursor.Border", SCHEME_CURSORBORDER, 0},
	{"Background.Color", SCHEME_BACKGROUNDCOLOR, 0},
	{"Highlight.Color", SCHEME_HIGHLIGHTCOLOR, 0},
	{"Highlight.Alpha", SCHEME_HIGHLIGHTALPHA, 0}
};

const char	*help_text =
"Usage:\n"
"    green [<options>] <PDF file 1> [<PDF file 2> [...]]\n"
"\n"
"The following options are available:\n"
"    -config=<filename>          to read a configuration from a non-standard path\n"
"    -scheme=<identifier>        to use a different scheme than the default\n"
"    -theme=<identifier>         to use a specific theme (e.g. dark-nord)\n"
"    -themesdir=<directory>      to load color themes (*.theme) from a directory\n"
"    -fullscreen                 to startup in fullscreen mode\n"
"    -no-fullscreen              to startup in window mode\n"
"    -width=<width>              to specify the window width (in pixels)\n"
"    -height=<height>            to specify the window height (in pixels)\n"
"    -help                       shows this help\n"
"    -version                    displays version information\n"
"\n"
"Further help is available online:\n"
"    http://wiki.github.com/schandinat/green/help\n";


void	PrintHelp()
{
	puts( help_text );
	return;
}

void	PrintVersion()
{
	printf( "green - the PDF reader\nversion: %s\nlicense: GNU GPL version 3\n", "0.2" );
	return;
}

double	ReadFraction( char *str )
{
	double	res;
	long	tmp;
	char	*str2;
	
	res = strtol( str, &str2, 10 );
	if (!*str2)
		return res;
	
	if (*str2 != '/')
		return 0;
	
	str2++;
	tmp = strtol( str2, &str2, 10 );
	if (!tmp || *str2)
		return 0;
	
	res /= tmp;
	return res;
}

char*	ScanIdentifier( char **ptr )
{
	char	*start = *ptr, *id;
	
	while ((**ptr >= 'a' && **ptr <= 'z') || (**ptr >= '0' && **ptr <= '9')
		|| **ptr == '_')
		(*ptr)++;
	
	if (*ptr == start)
		return NULL;
	
	id = malloc( *ptr - start + 1 );
	if (!id)
		return NULL;
	
	memcpy( id, start, *ptr - start );
	id[*ptr - start] = 0;
	return id;
}

int	ReadLine( FILE *f, char *buffer, int len )
{
	char c = '\0';
	int i = 0;

	if (len <= 1)
		return 1;

	unsigned int cnt = fread( &c, 1, 1, f );

	while (c != '\n' && c != '\r') {
		if (cnt != 1) {
			if (feof( f )) {
				buffer[i] = 0;
				return 1;
			}
			else {
				printf( "Read file failed" );
				exit( 1 );
			}
		}

		buffer[i++] = c;

		if (--len == 1)
			break;

		cnt = fread( &c, 1, 1, f );
	}

	buffer[i] = 0;
	return 0;
}

int	TryX11RGBText( Green_RGBA *color, char *str )
{
	if (str == NULL)
		return 1;

	FILE *file = fopen( RGB_TEXT, "r" );
	if (!file)
	{
		return 1;
	}

	char buf[256];
	while (!ReadLine( file, buf, 255 ))
	{
		if (!strcasecmp( str, buf+13 ))
		{

#define GetDigit( d ) \
	(d == ' ' ? 0 : d - '0')

			color->r = (GetDigit( buf[0] )*10 + GetDigit( buf[1] ))*10 + GetDigit( buf[ 2] );
			color->g = (GetDigit( buf[4] )*10 + GetDigit( buf[5] ))*10 + GetDigit( buf[ 6] );
			color->b = (GetDigit( buf[8] )*10 + GetDigit( buf[9] ))*10 + GetDigit( buf[10] );
			fclose( file );
			return 0;
		}
	}

	fclose( file );

	return 1;
}

int	GetColor( Green_RGBA *color, char *str )
{
	Green_RGBA	tmp = {0, 0, 0, 0};
	unsigned char	c, channel = 0;
	int	i = 0;
	
	if (!strncasecmp( str, "0x", 2 ) && strlen( str ) == 8)
	{
		for (i = 2; i < 8; i++)
		{
			if (str[i] >= '0' && str[i] <= '9')
				c = str[i] - '0';
			else if (str[i] >= 'a' && str[i] <= 'f')
				c = str[i] - 'a' + 0x0A;
			else if (str[i] >= 'A' && str[i] <= 'F')
				c = str[i] - 'A' + 0x0A;
			else
				return -1;
			
			if (i % 2)
			{
				channel |= c;
				if (i / 2 == 1)
					tmp.r = channel;
				else if (i / 2 == 2)
					tmp.g = channel;
				else if (i / 2 == 3)
					tmp.b = channel;
			}
			else
				channel = c<<4;
		}
		
		color->r = tmp.r;
		color->g = tmp.g;
		color->b = tmp.b;
	}
	else if (!strcasecmp( str, "black" ))
	{
		color->r = 0x00;
		color->g = 0x00;
		color->b = 0x00;
	}
	else if (!strcasecmp( str, "gray" ))
	{
		color->r = 0x80;
		color->g = 0x80;
		color->b = 0x80;
	}
	else if (!strcasecmp( str, "maroon" ))
	{
		color->r = 0x80;
		color->g = 0x00;
		color->b = 0x00;
	}
	else if (!strcasecmp( str, "red" ))
	{
		color->r = 0xFF;
		color->g = 0x00;
		color->b = 0x00;
	}
	else if (!strcasecmp( str, "green" ))
	{
		color->r = 0x00;
		color->g = 0x80;
		color->b = 0x00;
	}
	else if (!strcasecmp( str, "lime" ))
	{
		color->r = 0x00;
		color->g = 0xFF;
		color->b = 0x00;
	}
	else if (!strcasecmp( str, "olive" ))
	{
		color->r = 0x80;
		color->g = 0x80;
		color->b = 0x00;
	}
	else if (!strcasecmp( str, "yellow" ))
	{
		color->r = 0xFF;
		color->g = 0xFF;
		color->b = 0x00;
	}
	else if (!strcasecmp( str, "navy" ))
	{
		color->r = 0x00;
		color->g = 0x00;
		color->b = 0x80;
	}
	else if (!strcasecmp( str, "blue" ))
	{
		color->r = 0x00;
		color->g = 0x00;
		color->b = 0xFF;
	}
	else if (!strcasecmp( str, "purple" ))
	{
		color->r = 0x80;
		color->g = 0x00;
		color->b = 0x80;
	}
	else if (!strcasecmp( str, "fuchsia" ))
	{
		color->r = 0xFF;
		color->g = 0x00;
		color->b = 0xFF;
	}
	else if (!strcasecmp( str, "teal" ))
	{
		color->r = 0x00;
		color->g = 0x80;
		color->b = 0x80;
	}
	else if (!strcasecmp( str, "aqua" ))
	{
		color->r = 0x00;
		color->g = 0xFF;
		color->b = 0xFF;
	}
	else if (!strcasecmp( str, "silver" ))
	{
		color->r = 0xC0;
		color->g = 0xC0;
		color->b = 0xC0;
	}
	else if (!strcasecmp( str, "white" ))
	{
		color->r = 0xFF;
		color->g = 0xFF;
		color->b = 0xFF;
	}
	else
		if (TryX11RGBText( color, str ))
			return -1;
	
	return 0;
}

int	EvalProperty( Green_RTD *rtd, int id, char *arg )
{
	double	tmpd;
	char	*tmpc, *name, *val, *str;
	long	tmpl;
	int	res = 0;
	
	switch (id)
	{
		case SCHEME_WIDTH:
			tmpl = strtol( arg, &tmpc, 10 );
			if (*tmpc || tmpl < 0)
				res = -1;
			else
				rtd->width = tmpl;
			
			break;
		case SCHEME_HEIGHT:
			tmpl = strtol( arg, &tmpc, 10 );
			if (*tmpc || tmpl < 0)
				res = -1;
			else
				rtd->height = tmpl;
			
			break;
		case SCHEME_FULLSCREEN:
			if (!strcasecmp( arg, "yes" ))
				rtd->flags |= GREEN_FULLSCREEN;
			else if (!strcasecmp( arg, "no" ))
				rtd->flags &= ~GREEN_FULLSCREEN;
			else
				res = -1;
			
			break;
		case SCHEME_FIT:
			if (!strcasecmp( arg, "none" ))
				rtd->fit_method = NATURAL;
			else if (!strcasecmp( arg, "width" ))
				rtd->fit_method = WIDTH;
			else if (!strcasecmp( arg, "height" ))
				rtd->fit_method = HEIGHT;
			else if (!strcasecmp( arg, "page" ))
				rtd->fit_method = PAGE;
			else
				res = -1;
			
			break;
		case SCHEME_CURSORVISIBILITY:
			tmpl = strtol( arg, &tmpc, 10 );
			if (tmpl > 0 && !*tmpc)
				rtd->mouse.visibility = tmpl;
			else if (!strcasecmp( arg, "visible" ))
				rtd->mouse.visibility = -1;
			else if (!strcasecmp( arg, "invisible" ))
				rtd->mouse.visibility = 0;
			else
				res = -1;
			
			break;
		case SCHEME_CURSORBORDER:
			str = strtok_r( arg, ",", &arg );
			if (!str)
			{
				res = -1;
				break;
			}
			
			do
			{
				name = strtok( str, " =" );
				val = strtok( NULL, " =" );
				if (!name || !val || strtok( NULL, " =" ))
				{
					res = -1;
					break;
				}
				
				if (!strcasecmp( name, "size" ))
				{
					tmpl = strtol( val, &tmpc, 10 );
					if (tmpl < 0 || tmpl > 50 || tmpc[0] !='%' || tmpc[1] )
					{
						res = -1;
						break;
					}
					else
						rtd->mouse.border_size = tmpl;
				}
				else if (!strcasecmp( name, "speed" ))
				{
					tmpd = ReadFraction( val );
					if (tmpd > 0)
						rtd->mouse.border_speed = tmpd;
					else
					{
						res = -1;
						break;
					}
				}
				else
				{
					res = -1;
					break;
				}
				
			}	while ((str = strtok_r( NULL, ",", &arg )));
			
			break;
		case SCHEME_BACKGROUNDCOLOR:
			res = GetColor( &rtd->c_background, arg );
			break;
		case SCHEME_HIGHLIGHTCOLOR:
			res = GetColor( &rtd->c_highlight, arg );
			break;
		case SCHEME_HIGHLIGHTALPHA:
			tmpl = strtol( arg, &tmpc, 10 );
			if (!*tmpc && tmpl >= 0 && tmpl <= 0xFF)
				rtd->c_highlight.a = tmpl;
			else if (tmpc[0] == '%' && !tmpc[1] && tmpl >= 0 && tmpl <= 100)
				rtd->c_highlight.a = tmpl * 0xFF / 100;
			else
				res = -1;
			
			break;
	}
	
	return res;
}

int	ProcessSingleScheme( Green_RTD *rtd, struct SchemeArray *schemes, char *name )
{
	struct SchemeData	*data = NULL;
	char	*str, *tmp, *id, *arg;
	int	i, res = 0;
	
	for (i = 0; i < schemes->n; i++)
	{
		if (!strcmp( name, schemes->scheme[i]->name ))
		{
			data = schemes->scheme[i];
			break;
		}
	}
	
	if (!data)
	{
		fprintf( stderr, "Unknown SCHEME '%s'\n", name );
		return -1;
	}
	
	if (data->processed)
		return 0;
	
	data->processed = 1;
	if (!data->data || !data->data_size)
		return 0;
	
	str = data->data + data->data_size - 1;
	do
	{
		while (str > data->data && *(str - 1))
			str--;
		
		if (str[0] == '!')
		{
			tmp = str + 1;
			id = ScanIdentifier( &tmp );
			if (!id || *tmp)
			{
				fprintf( stderr, "Invalid execute indentifier in SCHEME '%s'\n", name );
				free( id );
				res = -1;
				break;
			}
			
			res = ProcessSingleScheme( rtd, schemes, id );
			free( id );
			continue;
		}
		else if ((tmp = strchr( str, '=' )))
		{
			arg = tmp + 1;
			while (tmp > str && *(tmp - 1) == ' ')
				tmp--;
			
			if (tmp == str)
			{
				fprintf( stderr, "Invalid empty property in SCHEME '%s'\n", name );
				res = -1;
				break;
			}
			
			id = malloc( tmp - str + 1 );
			if (!id)
			{
				fprintf( stderr, "Out of memory!\n" );
				res = -1;
				break;
			}
			
			memcpy( id, str, tmp - str );
			id[tmp-str] = 0;
			while (*arg == ' ')
				arg++;
			
			arg = strdup( arg );
			if (!arg)
			{
				fprintf( stderr, "Out of memory!\n" );
				free( id );
				res = -1;
				break;
			}
			
			for (i = 0; i < sizeof( scheme_property ) / sizeof( scheme_property[0] ); i++)
			{
				if (!strcasecmp( id, scheme_property[i].name ))
				{
					if (scheme_property[i].processed)
						break;
					
					scheme_property[i].processed = 1;
					res = EvalProperty( rtd, scheme_property[i].id, arg );
					if (res)
						printf( "Invalid property '%s' in SCHEME '%s'\n", id, name );
					
					break;
				}
			}
			
			if (i == sizeof( scheme_property ) / sizeof( scheme_property[0] ))
				printf( "Ignoring unknown property '%s' with value '%s'\n", id, arg );
			
			free( arg );
			free( id );
		}
		else
			printf( "Ignoring unknown construct '%s' in SCHEME '%s'\n", str, name );
		
	}	while ((str -= 2) > data->data && !res);
	
	return res;
}

int	ProcessSchemes( Green_RTD *rtd, struct SchemeArray *schemes, char *current )
{
	int	res = 0;
	char	*str, *id, *tmp;
	
	do
	{
		str = strrchr( current, ',' );
		if (str)
		{
			*str = 0;
			str++;
		}
		else
			str = current;
		
		tmp = str;
		id = ScanIdentifier( &tmp );
		if (!id || *tmp)
		{
			fprintf( stderr, "Invalid SCHEME indentifier '%s'\n", str );
			free( id );
			res = -1;
			break;
		}
		
		res = ProcessSingleScheme( rtd, schemes, id );
		free( id );
		if (res)
			break;
		
	}	while (str != current);
	
	return res;
}

int	ReadConfigLine( FILE *file, char **line )
{
	char	*tmp, *buff = malloc( 80 );
	int	c, comment = 0, used = 0, size = 80;
	
	if (!buff)
	{
		fprintf( stderr, "Out of memory!\n" );
		return -1;
	}
	
	while ((c = fgetc( file )) == ' ' || c == '\t' );
	do
	{
		if (c == '\n' || c == EOF)
			break;
		
		if (comment)
			continue;
		
		if (c == '\t')
			c = ' ';
		
		if (c == '#')
		{
			comment = 1;
			continue;
		}
		
		buff[used] = c;
		used++;
		if (used < size)
			continue;
		
		size += 80;
		tmp = realloc( buff, size );
		if (tmp)
			buff = tmp;
		else
		{
			free( buff );
			fprintf( stderr, "Out of memory!\n" );
			return -1;
		}
	}	while ((c = fgetc( file )) != EOF);
	
	while (used && buff[used-1] == ' ')
		used--;
	
	buff[used] = 0;
	*line = buff;
	return used || c != EOF ? 0 : 1;
}

int	ParseConfig( char *filename, struct SchemeArray *schemes, char **default_scheme )
{
	struct SchemeData	*scheme = NULL;
	FILE	*file = fopen( filename, "r" );
	char	*line, *tmp;
	void	*ptr;
	int	i, res = 0, state = 0, line_count = 1;
	
	if (!file)
		return 1;
	
	while (!res && !(res = ReadConfigLine( file, &line )))
	{
		line_count++;
		if (!*line)
		{
			free( line );
			continue;
		}
		
		tmp = line;
		switch (state)
		{
			case 0:
				strtok( line, " " );
				tmp = strtok( NULL, "" );
				while (*tmp == ' ')
					tmp++;
				
				if (!strcmp( line, "SCHEME" ))
				{
					if (!*tmp)
					{
						fprintf( stderr, "Missing argument to DEFAULT_SCHEME on line %d\n", line_count );
						res = -1;
						break;
					}
					
					scheme = malloc( sizeof( *scheme ) );
					if (!scheme)
					{
						fprintf( stderr, "Out of memory!\n" );
						res = -1;
						break;
					}				
					
					scheme->data = NULL;
					scheme->data_size = 0;
					scheme->processed = 0;
					scheme->name = ScanIdentifier( &tmp );
					if (!scheme->name)
					{
						fprintf( stderr, "Missing identifier to DEFAULT_SCHEME on line %d\n", line_count );
						free( scheme );
						res = -1;
						break;
					}
					
					if (*tmp)
					{
						fprintf( stderr, "Invalid extra argument to SCHEME on line %d\n", line_count );
						free( scheme->name );
						free( scheme );
						res = -1;
						break;
					}
					
					for (i = 0; i < schemes->n; i++)
					{
						if (!strcmp( scheme->name, schemes->scheme[i]->name ))
						{
							fprintf( stderr, "Duplicated SCHEME '%s' on line %d\n", scheme->name, line_count );
							free( scheme->name );
							free( scheme );
							res = -1;
							break;
						}
					}
					
					if (res)
						break;
					
					state = 1;
					break;
				}
				else if (!strcmp( line, "DEFAULT_SCHEME" ))
				{
					if (!*tmp)
					{
						fprintf( stderr, "Missing argument to DEFAULT_SCHEME on line %d\n", line_count );
						res = -1;
						break;
					}
					
					if (strchr( tmp, ' ' ))
					{
						fprintf( stderr, "Invalid extra argument to DEFAULT_SCHEME on line %d\n", line_count );
						res = -1;
						break;
					}
					
					if (*default_scheme)
					{
						fprintf( stderr, "Forbidden duplicated DEFAULT_SCHEME on line %d\n", line_count );
						res = -1;
						break;
					}
					
					*default_scheme = strdup( tmp );
					if (!*default_scheme)
					{
						fprintf( stderr, "Out of memory!\n" );
						res = -1;
						break;
					}
				}
				else
				{
					fprintf( stderr, "Unknown command '%s' on line %d\n", line, line_count );
					res = -1;
					break;
				}
				
				break;
			case 1:
				if (tmp[0] == '{')
				{
					tmp++;
					state = 2;
					
					while (*tmp == ' ')
						tmp++;
					
					if (*tmp)
						continue;
					else
						break;
				}
				else
				{
					fprintf( stderr, "Expected '{' but got '%c' during SCHEME on line %d\n", tmp[0], line_count );
					free( scheme->name );
					free( scheme );
					res = -1;
					break;
				}
			case 2:
				if (!strcmp( tmp, "}"))
				{
					ptr = realloc( schemes->scheme, (schemes->n + 1) * sizeof( scheme ) );
					if (!ptr)
					{
						fprintf( stderr, "Out of memory!\n" );
						free( scheme->data );
						free( scheme->name );
						free( scheme );
						res = -1;
						break;
					}
					
					schemes->scheme = ptr;
					schemes->scheme[schemes->n] = scheme;
					schemes->n++;
					state = 0;
					break;
				}
				else
				{
					ptr = realloc( scheme->data, scheme->data_size + strlen( tmp ) + 1 );
					if (!ptr)
					{
						fprintf( stderr, "Out of memory!\n" );
						free( scheme->data );
						free( scheme->name );
						free( scheme );
						res = -1;
						break;
					}
					
					memcpy( ptr + scheme->data_size, tmp, strlen( tmp ) + 1 );
					scheme->data = ptr;
					scheme->data_size += strlen( tmp ) + 1;
					break;
				}
		}
		
		free( line );
	}
	
	if (res > 0)
		res = 0;
	
	if (!res && state)
		res = -1;
	
	fclose( file );
	return res;
}

static int	AddTheme( Green_RTD *rtd, const char *name, bool dark, Green_RGBA bg, Green_RGBA text, Green_RGBA hl )
{
	Green_Theme	*tmp;
	int	idx = -1, i;
	
	for (i = 0; i < rtd->themes_count; i++)
	{
		if (!strcmp( name, rtd->themes[i].name ))
		{
			idx = i;
			break;
		}
	}
	
	if (idx < 0)
	{
		tmp = realloc( rtd->themes, (rtd->themes_count + 1) * sizeof( Green_Theme ) );
		if (!tmp)
			return -1;
		
		rtd->themes = tmp;
		idx = rtd->themes_count++;
		memset( &rtd->themes[idx], 0, sizeof( Green_Theme ) );
		strncpy( rtd->themes[idx].name, name, sizeof( rtd->themes[idx].name ) - 1 );
		rtd->themes[idx].name[sizeof( rtd->themes[idx].name ) - 1] = 0;
	}
	
	rtd->themes[idx].dark = dark;
	rtd->themes[idx].background = bg;
	rtd->themes[idx].text = text;
	rtd->themes[idx].highlight = hl;
	return idx;
}

static int	FindTheme( Green_RTD *rtd, const char *name )
{
	int	i;
	
	for (i = 0; i < rtd->themes_count; i++)
		if (!strcmp( name, rtd->themes[i].name ))
			return i;
	
	return -1;
}

static int	ThemeFromScheme( Green_Theme *t, struct SchemeData *data, struct SchemeArray *schemes, int depth )
{
	Green_RGBA	white = {0xFF, 0xFF, 0xFF, 0xFF},
		hl = {0x80, 0xFF, 0x80, 0x80},
		bg = {0x10, 0x10, 0x10, 0xFF};
	char	*str, *tmp, *id, *arg;
	long	tmpl;
	int	i, res = 0;
	
	t->dark = true;
	t->background = bg;
	t->text = white;
	t->highlight = hl;
	
	if (!data || !data->data || !data->data_size || depth > 16)
		return -1;
	
	str = data->data + data->data_size - 1;
	do
	{
		while (str > data->data && *(str - 1))
			str--;
		
		if (str[0] == '!')
		{
			tmp = str + 1;
			id = ScanIdentifier( &tmp );
			if (!id || *tmp)
			{
				fprintf( stderr, "Invalid execute identifier in THEME '%s'\n", data->name );
				free( id );
				res = -1;
				break;
			}
			
			for (i = 0; i < (int)schemes->n; i++)
			{
				if (!strcmp( id, schemes->scheme[i]->name ))
				{
					res = ThemeFromScheme( t, schemes->scheme[i], schemes, depth + 1 );
					break;
				}
			}
			
			if (i == (int)schemes->n)
			{
				fprintf( stderr, "Unknown THEME include '%s'\n", id );
				res = -1;
			}
			free( id );
			continue;
		}
		else if ((tmp = strchr( str, '=' )))
		{
			arg = tmp + 1;
			while (tmp > str && *(tmp - 1) == ' ')
				tmp--;
			
			if (tmp == str)
			{
				fprintf( stderr, "Invalid empty property in THEME '%s'\n", data->name );
				res = -1;
				break;
			}
			
			id = malloc( tmp - str + 1 );
			if (!id)
			{
				fprintf( stderr, "Out of memory!\n" );
				res = -1;
				break;
			}
			memcpy( id, str, tmp - str );
			id[tmp - str] = 0;
			while (*arg == ' ')
				arg++;
			arg = strdup( arg );
			if (!arg)
			{
				fprintf( stderr, "Out of memory!\n" );
				free( id );
				res = -1;
				break;
			}
			
			if (!strcasecmp( id, "mode" ))
				t->dark = !strcasecmp( arg, "dark" ) || !strcasecmp( arg, "night" );
			else if (!strcasecmp( id, "background.color" ))
				res = GetColor( &t->background, arg );
			else if (!strcasecmp( id, "text.color" ))
				res = GetColor( &t->text, arg );
			else if (!strcasecmp( id, "highlight.color" ))
				res = GetColor( &t->highlight, arg );
			else if (!strcasecmp( id, "highlight.alpha" ))
			{
				tmpl = strtol( arg, &tmp, 10 );
				if (!*tmp && tmpl >= 0 && tmpl <= 0xFF)
					t->highlight.a = tmpl;
				else if (tmp[0] == '%' && !tmp[1] && tmpl >= 0 && tmpl <= 100)
					t->highlight.a = tmpl * 0xFF / 100;
				else
					res = -1;
			}
			else
				fprintf( stderr, "Ignoring unknown property '%s' in THEME '%s'\n", id, data->name );
			
			free( arg );
			free( id );
		}
		else
			fprintf( stderr, "Ignoring unknown construct '%s' in THEME '%s'\n", str, data->name );
		
	}	while ((str -= 2) > data->data && !res);
	
	return res;
}

static int	BuildThemeList( Green_RTD *rtd, struct SchemeArray *schemes )
{
	Green_Theme	t;
	int	i, res = 0;
	
	for (i = 0; i < (int)schemes->n; i++)
	{
		if (ThemeFromScheme( &t, schemes->scheme[i], schemes, 0 ))
			continue;
		
		if (AddTheme( rtd, schemes->scheme[i]->name, t.dark, t.background, t.text, t.highlight ) < 0)
		{
			fprintf( stderr, "Out of memory!\n" );
			res = -1;
			break;
		}
	}
	
	return res;
}

static int	LoadThemeDir( char *dir, struct SchemeArray *schemes )
{
	DIR	*d;
	struct dirent	*de;
	char	path[1024], *file_default;
	int	len;
	
	if (!dir || !*dir)
		return 0;
	
	d = opendir( dir );
	if (!d)
		return 0;
	
	while ((de = readdir( d )))
	{
		len = strlen( de->d_name );
		if (len <= 6 || strcmp( de->d_name + len - 6, ".theme" ))
			continue;
		
		snprintf( path, sizeof( path ), "%s/%s", dir, de->d_name );
		file_default = NULL;
		if (ParseConfig( path, schemes, &file_default ))
			fprintf( stderr, "Failed to load theme file '%s'\n", path );
		free( file_default );
	}
	
	closedir( d );
	return 1;
}

int	main( int argc, char *argv[] )
{
	Green_RTD	rtd;
	Green_RGBA	black = {0, 0, 0, 0xFF},
		white = {0xFF, 0xFF, 0xFF, 0xFF},
		dark_bg = {0x10, 0x10, 0x10, 0xFF};
	struct SchemeArray	schemes, theme_schemes;
	char	*opt, *config_file = NULL, *default_scheme = NULL, *current_scheme = NULL;
	char	*themes_dir = NULL, *current_theme = NULL;
	int i, err = 0;
	
	rtd.flags = 0;
	rtd.width = 0;
	rtd.height = 0;	
	rtd.docs = NULL;
	rtd.doc_count = 0;
	rtd.doc_cur = 0;
	rtd.c_background = white;
	rtd.c_highlight.r = 0x80;
	rtd.c_highlight.g = 0xFF;
	rtd.c_highlight.b = 0x80;
	rtd.c_highlight.a = 0x80;
	rtd.fit_method = NATURAL;
	rtd.step = 1;
	rtd.zoomstep = 1.1;
	rtd.bb = 0x04;
	rtd.side_by_side = 1;
	rtd.night = true;
	rtd.mouse.flags = 1;
	rtd.mouse.visibility = 500;
	rtd.mouse.border_size = 0;
	rtd.mouse.border_speed = 1;
	schemes.scheme = NULL;
	schemes.n = 0;
	theme_schemes.scheme = NULL;
	theme_schemes.n = 0;
	rtd.themes = NULL;
	rtd.themes_count = 0;
	rtd.theme_dark_cur = -1;
	rtd.theme_light_cur = -1;
	AddTheme( &rtd, "default-light", false, rtd.c_background, black, rtd.c_highlight );
	rtd.theme_light_cur = FindTheme( &rtd, "default-light" );
	AddTheme( &rtd, "dark", true, dark_bg, white, rtd.c_highlight );
	rtd.theme_dark_cur = FindTheme( &rtd, "dark" );
	g_type_init();
	
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-')
			continue;
		
		opt = argv[i][1] == '-' ? &argv[i][2] : &argv[i][1];
		if (!strncmp( opt, "config=", 7 ))
		{
			opt += 7;
			
			if (config_file)
			{
				fprintf( stderr, "Multiple configuration files are forbidden!\n" );
				return -1;
			}
			
			config_file = opt;
		}
		else if (!strncmp( opt, "scheme=", 7 ))
		{
			opt += 7;
			
			if (current_scheme)
			{
				fprintf( stderr, "Multiple scheme statements are forbidden!\n" );
				return -1;
			}
			
			current_scheme = opt;
		}
		else if (!strncmp( opt, "theme=", 6 ))
		{
			opt += 6;
			
			if (current_theme)
			{
				fprintf( stderr, "Multiple theme statements are forbidden!\n" );
				return -1;
			}
			
			current_theme = opt;
		}
		else if (!strncmp( opt, "themesdir=", 10 ))
		{
			opt += 10;
			
			if (themes_dir)
			{
				fprintf( stderr, "Multiple themesdir statements are forbidden!\n" );
				return -1;
			}
			
			themes_dir = opt;
		}
	}
	
	if (config_file)
	{
		err = ParseConfig( config_file, &schemes, &default_scheme );
		if (err > 0)
		{
			fprintf( stderr, "Specified configuration file not found!\n" );
			return -1;
		}
	}
	else
	{
#ifdef	GREEN_USERCONFIG_FILE
		opt = getenv( "HOME" );
		if (opt && (config_file = malloc( strlen( opt ) + strlen( GREEN_USERCONFIG_FILE ) + 2 )))
		{
			sprintf( config_file, "%s/%s", opt, GREEN_USERCONFIG_FILE );
			err = ParseConfig( config_file, &schemes, &default_scheme );
			if (err > 0)
			{
				free( config_file );
				config_file = NULL;
				err = 0;
			}
		}
#endif
#ifdef	GREEN_SYSCONFIG_FILE
		if (!config_file)
		{
			config_file = GREEN_SYSCONFIG_FILE;
			err = ParseConfig( config_file, &schemes, &default_scheme );
			if (err > 0)
			{
				config_file = NULL;
				err = 0;
			}
		}
#endif
	}
	
	if (err < 0)
	{
		fprintf( stderr, "Malformed configuration file '%s'!\n", config_file );
		return err;
	}
	
	if (!current_scheme && default_scheme)
		current_scheme = default_scheme;
	
	if (current_scheme)
	{
		if ((err = ProcessSchemes( &rtd, &schemes, current_scheme )))
			return err;
	}
	
	/* load theme files (optional): -themesdir, $XDG_DATA_HOME/green/themes,
	   $HOME/.local/share/green/themes, $HOME/.green/themes */
	{
		char	*theme_dirs[4], *dir, *xdg;
		int	n = 0, j;
		
		theme_dirs[n++] = themes_dir;
		xdg = getenv( "XDG_DATA_HOME" );
		dir = getenv( "HOME" );
		if (xdg && *xdg)
		{
			char	*tmp = malloc( strlen( xdg ) + strlen( "/green/themes" ) + 1 );
			
			if (tmp)
			{
				sprintf( tmp, "%s/%s", xdg, "green/themes" );
				theme_dirs[n++] = tmp;
			}
		}
		else if (dir)
		{
			char	*tmp = malloc( strlen( dir ) + strlen( "/.local/share/green/themes" ) + 1 );
			
			if (tmp)
			{
				sprintf( tmp, "%s/%s", dir, ".local/share/green/themes" );
				theme_dirs[n++] = tmp;
			}
		}
		if (dir)
		{
			char	*tmp = malloc( strlen( dir ) + strlen( "/.green/themes" ) + 1 );
			
			if (tmp)
			{
				sprintf( tmp, "%s/%s", dir, ".green/themes" );
				theme_dirs[n++] = tmp;
			}
		}
		
		for (j = 0; j < n; j++)
		{
			if (!theme_dirs[j])
				continue;
			
			if (LoadThemeDir( theme_dirs[j], &theme_schemes ))
				break;
		}
		
		for (j = 1; j < n; j++)
			free( theme_dirs[j] );
	}
	
	if (BuildThemeList( &rtd, &theme_schemes ))
		return -1;
	
	/* apply the hard-coded default themes to classic colors */
	{
		int	idx = FindTheme( &rtd, "default-light" );
		
		if (idx >= 0)
		{
			rtd.themes[idx].background = rtd.c_background;
			rtd.themes[idx].highlight = rtd.c_highlight;
		}
	}
	
	if (current_theme)
	{
		int	idx = FindTheme( &rtd, current_theme );
		
		if (idx < 0)
		{
			fprintf( stderr, "Unknown THEME '%s'\n", current_theme );
			return -1;
		}
		
		if (rtd.themes[idx].dark)
			rtd.theme_dark_cur = idx;
		else
		{
			rtd.c_background = rtd.themes[idx].background;
			rtd.c_highlight = rtd.themes[idx].highlight;
			rtd.theme_light_cur = idx;
		}
	}
	
	if (rtd.theme_dark_cur < 0 || rtd.theme_light_cur < 0)
	{
		fprintf( stderr, "No usable themes found!\n" );
		return -1;
	}
	
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-')
			continue;
		
		opt = argv[i][1] == '-' ? &argv[i][2] : &argv[i][1];
		if (!strcmp( opt, "h" ) || !strcmp( opt, "help" ))
		{
			PrintHelp();
			return 0;
		}
		else if (!strcmp( opt, "v" ) || !strcmp( opt, "version" ))
		{
			PrintVersion();
			return 0;
		}
		else if (!strncmp( opt, "config=", 7 ) || !strncmp( opt, "scheme=", 7 )
			|| !strncmp( opt, "theme=", 6 ) || !strncmp( opt, "themesdir=", 10 ))
		{
		}
		else if (!strncmp( opt, "fit=", 4 ))
		{
			opt += 4;
			if (!strcmp( opt, "width" ))
				rtd.fit_method = WIDTH;
			else if (!strcmp( opt, "height" ))
				rtd.fit_method = HEIGHT;
			else if (!strcmp( opt, "page" ))
				rtd.fit_method = PAGE;
			else if (!strcmp( opt, "none" ))
				rtd.fit_method = NATURAL;
			else
				err = -1;
		}
		else if (!strncmp( opt, "step=", 5 ))
		{
			opt += 5;
			rtd.step = ReadFraction( opt );
			if (rtd.step <= 0 || rtd.step > 1)
				err = -1;
		}
		else if (!strncmp( opt, "zoomstep=", 9 ))
		{
			opt += 9;
			rtd.zoomstep = ReadFraction( opt );
			if (rtd.zoomstep <= 0)
				err = -1;
			
			rtd.zoomstep += 1;
		}
		else if (!strncmp( opt, "width=", 6 ))
		{
			opt += 6;
			rtd.width = strtol( opt, &opt, 10 );
			if (*opt)
				err = -1;
		}
		else if (!strncmp( opt, "height=", 7 ))
		{
			opt += 7;
			rtd.height = strtol( opt, &opt, 10 );
			if (*opt)
				err = -1;
		}
		else if (!strcmp( opt, "fullscreen" ))
			rtd.flags |= GREEN_FULLSCREEN;
		else if (!strcmp( opt, "no-fullscreen" ))
			rtd.flags &= ~GREEN_FULLSCREEN;
		else
			err = -1;
		
		if (err)
		{
			fprintf( stderr, "Unknown option!\n" );
			PrintHelp();
			return err;
		}
	}
	
	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-')
			continue;
		
		if (Green_Open( &rtd, argv[i] ) < 0)
		{
			fprintf( stderr, "Failed to open: %s\n", argv[i] );
			continue;
		}
	}
	
	return Green_SDL_Main( &rtd );
}
