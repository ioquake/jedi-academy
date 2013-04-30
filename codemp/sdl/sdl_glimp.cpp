/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "sdl_local.h"
#include "../renderer/tr_local.h"
#ifdef notyet
#include "sdl_icon.h"
#endif

#define CLIENT_WINDOW_TITLE	"Jedi Outcast"
#define CLIENT_WINDOW_MIN_TITLE	"JO"

/* Just hack it for now. */
#ifdef MACOS_X
#include <OpenGL/OpenGL.h>
typedef CGLContextObj QGLContext;
#define GLimp_GetCurrentContext() CGLGetCurrentContext()
#define GLimp_SetCurrentContext(ctx) CGLSetCurrentContext(ctx)
#else
typedef void *QGLContext;
#define GLimp_GetCurrentContext() (NULL)
#define GLimp_SetCurrentContext(ctx)
#endif

static QGLContext opengl_context;
static float displayAspect;

typedef enum
{
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
} rserr_t;

static SDL_Surface *screen = NULL;
static const SDL_VideoInfo *videoInfo = NULL;

cvar_t *r_allowSoftwareGL; // Don't abort out if a hardware visual can't be obtained
cvar_t *r_allowResize; // make window resizable
cvar_t *r_centerWindow;
cvar_t *r_sdlDriver;
cvar_t *r_noborder;

// Whether the current hardware supports dynamic glows/flares.
extern bool g_bDynamicGlowSupported;

// Hack variable for deciding which kind of texture rectangle thing to do (for some
// reason it acts different on radeon! It's against the spec!).
bool g_bTextureRectangleHack = false;

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( void )
{
	IN_Shutdown();

	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	screen = NULL;
}

/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize(void)
{
	SDL_WM_IconifyWindow();
}


/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( char *comment )
{
}

/*
===============
GLimp_CompareModes
===============
*/
static int GLimp_CompareModes( const void *a, const void *b )
{
	const float ASPECT_EPSILON = 0.001f;
	SDL_Rect *modeA = *(SDL_Rect **)a;
	SDL_Rect *modeB = *(SDL_Rect **)b;
	float aspectA = (float)modeA->w / (float)modeA->h;
	float aspectB = (float)modeB->w / (float)modeB->h;
	int areaA = modeA->w * modeA->h;
	int areaB = modeB->w * modeB->h;
	float aspectDiffA = fabs( aspectA - displayAspect );
	float aspectDiffB = fabs( aspectB - displayAspect );
	float aspectDiffsDiff = aspectDiffA - aspectDiffB;

	if( aspectDiffsDiff > ASPECT_EPSILON )
		return 1;
	else if( aspectDiffsDiff < -ASPECT_EPSILON )
		return -1;
	else
		return areaA - areaB;
}


/*
===============
GLimp_DetectAvailableModes
===============
*/
static void GLimp_DetectAvailableModes(void)
{
	char buf[ MAX_STRING_CHARS ] = { 0 };
	SDL_Rect **modes;
	int numModes;
	int i;

	modes = SDL_ListModes( videoInfo->vfmt, SDL_OPENGL | SDL_FULLSCREEN );

	if( !modes )
	{
		Com_Printf( "Can't get list of available modes\n" );
		return;
	}

	if( modes == (SDL_Rect **)-1 )
	{
		Com_Printf( "Display supports any resolution\n" );
		return; // can set any resolution
	}

	for( numModes = 0; modes[ numModes ]; numModes++ );

	if( numModes > 1 )
		qsort( modes, numModes, sizeof( SDL_Rect* ), GLimp_CompareModes );

	for( i = 0; i < numModes; i++ )
	{
		const char *newModeString = va( "%ux%u ", modes[ i ]->w, modes[ i ]->h );

		if( strlen( newModeString ) < (int)sizeof( buf ) - strlen( buf ) )
			Q_strcat( buf, sizeof( buf ), newModeString );
		else
			Com_Printf( "Skipping mode %ux%x, buffer too small\n", modes[i]->w, modes[i]->h );
	}

	if( *buf )
	{
		buf[ strlen( buf ) - 1 ] = 0;
		Com_Printf( "Available modes: '%s'\n", buf );
		Cvar_Set( "r_availableModes", buf );
	}
}

/*
===============
GLimp_SetMode
===============
*/
static int GLimp_SetMode(int mode, qboolean fullscreen, qboolean noborder)
{
	const char*   glstring;
	int sdlcolorbits;
	int colorbits, depthbits, stencilbits;
	int tcolorbits, tdepthbits, tstencilbits;
	int samples;
	int i = 0;
	SDL_Surface *vidscreen = NULL;
	Uint32 flags = SDL_OPENGL;

	Com_Printf( "Initializing OpenGL display\n");

	if ( r_allowResize->integer )
		flags |= SDL_RESIZABLE;

	if( videoInfo == NULL )
	{
		static SDL_VideoInfo sVideoInfo;
		static SDL_PixelFormat sPixelFormat;

		videoInfo = SDL_GetVideoInfo( );

		// Take a copy of the videoInfo
		memcpy( &sPixelFormat, videoInfo->vfmt, sizeof( SDL_PixelFormat ) );
		sPixelFormat.palette = NULL; // Should already be the case
		memcpy( &sVideoInfo, videoInfo, sizeof( SDL_VideoInfo ) );
		sVideoInfo.vfmt = &sPixelFormat;
		videoInfo = &sVideoInfo;

		if( videoInfo->current_h > 0 )
		{
			// Guess the display aspect ratio through the desktop resolution
			// by assuming (relatively safely) that it is set at or close to
			// the display's native aspect ratio
			displayAspect = (float)videoInfo->current_w / (float)videoInfo->current_h;

			Com_Printf( "Estimated display aspect: %.3f\n", displayAspect );
		}
		else
		{
			Com_Printf( "Cannot estimate display aspect, assuming 1.333\n" );
		}
	}

	Com_Printf ( "...setting mode %d:", mode );

	if (mode == -2)
	{
		// use desktop video resolution
		if( videoInfo->current_h > 0 )
		{
			glConfig.vidWidth = videoInfo->current_w;
			glConfig.vidHeight = videoInfo->current_h;
		}
		else
		{
			glConfig.vidWidth = 640;
			glConfig.vidHeight = 480;
			Com_Printf( "Cannot determine display resolution, assuming 640x480\n" );
		}

#if 0
		glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
#endif
	}
	else if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, mode ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}
	Com_Printf(" %d %d\n", glConfig.vidWidth, glConfig.vidHeight);

	if (fullscreen)
	{
		flags |= SDL_FULLSCREEN;
		glConfig.isFullscreen = qtrue;
	}
	else
	{
		if (noborder)
			flags |= SDL_NOFRAME;

		glConfig.isFullscreen = qfalse;
	}

	colorbits = r_colorbits->value;
	if ((!colorbits) || (colorbits >= 32))
		colorbits = 24;

	if (!r_depthbits->value)
		depthbits = 24;
	else
		depthbits = r_depthbits->value;
	stencilbits = r_stencilbits->value;
#ifdef notyet
	samples = r_ext_multisample->value;
#else
	samples = 0;
#endif

	for (i = 0; i < 16; i++)
	{
		// 0 - default
		// 1 - minus colorbits
		// 2 - minus depthbits
		// 3 - minus stencil
		if ((i % 4) == 0 && i)
		{
			// one pass, reduce
			switch (i / 4)
			{
				case 2 :
					if (colorbits == 24)
						colorbits = 16;
					break;
				case 1 :
					if (depthbits == 24)
						depthbits = 16;
					else if (depthbits == 16)
						depthbits = 8;
				case 3 :
					if (stencilbits == 24)
						stencilbits = 16;
					else if (stencilbits == 16)
						stencilbits = 8;
			}
		}

		tcolorbits = colorbits;
		tdepthbits = depthbits;
		tstencilbits = stencilbits;

		if ((i % 4) == 3)
		{ // reduce colorbits
			if (tcolorbits == 24)
				tcolorbits = 16;
		}

		if ((i % 4) == 2)
		{ // reduce depthbits
			if (tdepthbits == 24)
				tdepthbits = 16;
			else if (tdepthbits == 16)
				tdepthbits = 8;
		}

		if ((i % 4) == 1)
		{ // reduce stencilbits
			if (tstencilbits == 24)
				tstencilbits = 16;
			else if (tstencilbits == 16)
				tstencilbits = 8;
			else
				tstencilbits = 0;
		}

		sdlcolorbits = 4;
		if (tcolorbits == 24)
			sdlcolorbits = 8;

#ifdef __sgi /* Fix for SGIs grabbing too many bits of color */
		if (sdlcolorbits == 4)
			sdlcolorbits = 0; /* Use minimum size for 16-bit color */

		/* Need alpha or else SGIs choose 36+ bit RGB mode */
		SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 1);
#endif

		SDL_GL_SetAttribute( SDL_GL_RED_SIZE, sdlcolorbits );
		SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, sdlcolorbits );
		SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, sdlcolorbits );
		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, tdepthbits );
		SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, tstencilbits );

		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, samples ? 1 : 0 );
		SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, samples );

#ifdef notyet
		if(r_stereoEnabled->integer)
		{
			glConfig.stereoEnabled = qtrue;
			SDL_GL_SetAttribute(SDL_GL_STEREO, 1);
		}
		else
		{
			glConfig.stereoEnabled = qfalse;
			SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
		}
#endif
		
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

#if 0 // See http://bugzilla.icculus.org/show_bug.cgi?id=3526
		// If not allowing software GL, demand accelerated
		if( !r_allowSoftwareGL->integer )
		{
			if( SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 ) < 0 )
			{
				Com_Printf( "Unable to guarantee accelerated "
						"visual with libSDL < 1.2.10\n" );
			}
		}
#endif

		if( SDL_GL_SetAttribute( SDL_GL_SWAP_CONTROL, r_swapInterval->integer ) < 0 )
			Com_Printf( "r_swapInterval requires libSDL >= 1.2.10\n" );

#ifdef USE_ICON
		{
			SDL_Surface *icon = SDL_CreateRGBSurfaceFrom(
					(void *)CLIENT_WINDOW_ICON.pixel_data,
					CLIENT_WINDOW_ICON.width,
					CLIENT_WINDOW_ICON.height,
					CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
					CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width,
#ifdef Q3_LITTLE_ENDIAN
					0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000
#else
					0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF
#endif
					);

			SDL_WM_SetIcon( icon, NULL );
			SDL_FreeSurface( icon );
		}
#endif

		SDL_WM_SetCaption(CLIENT_WINDOW_TITLE, CLIENT_WINDOW_MIN_TITLE);
		SDL_ShowCursor(0);

		if (!(vidscreen = SDL_SetVideoMode(glConfig.vidWidth, glConfig.vidHeight, colorbits, flags)))
		{
			Com_Printf( "SDL_SetVideoMode failed: %s\n", SDL_GetError( ) );
			continue;
		}

		opengl_context = GLimp_GetCurrentContext();

		Com_Printf( "Using %d/%d/%d Color bits, %d depth, %d stencil display.\n",
				sdlcolorbits, sdlcolorbits, sdlcolorbits, tdepthbits, tstencilbits);

		glConfig.colorBits = tcolorbits;
		glConfig.depthBits = tdepthbits;
		glConfig.stencilBits = tstencilbits;
		break;
	}

	GLimp_DetectAvailableModes();

	if (!vidscreen)
	{
		Com_Printf( "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	screen = vidscreen;

	glstring = (char *) qglGetString (GL_RENDERER);
	Com_Printf( "GL_RENDERER: %s\n", glstring );

	return RSERR_OK;
}

/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static qboolean GLimp_StartDriverAndSetMode(int mode, qboolean fullscreen, qboolean noborder)
{
	rserr_t err;

	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		char driverName[ 64 ];

		if (SDL_Init(SDL_INIT_VIDEO) == -1)
		{
			Com_Printf("SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n",
					SDL_GetError());
			return qfalse;
		}

		SDL_VideoDriverName( driverName, sizeof( driverName ) - 1 );
		Com_Printf( "SDL using driver \"%s\"\n", driverName );
		Cvar_Set( "r_sdlDriver", driverName );
	}

	if (fullscreen && Cvar_VariableIntegerValue( "in_nograb" ) )
	{
		Com_Printf( "Fullscreen not allowed with in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}
	
	err = (rserr_t) GLimp_SetMode(mode, fullscreen, noborder);

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
			return qfalse;
		case RSERR_INVALID_MODE:
			Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
			return qfalse;
		default:
			break;
	}

	return qtrue;
}

//--------------------------------------------
static void GLW_InitTextureCompression( void )
{
	qboolean newer_tc, old_tc;

	// Check for available tc methods.
	newer_tc = ( strstr( glConfig.extensions_string, "ARB_texture_compression" )
		&& strstr( glConfig.extensions_string, "EXT_texture_compression_s3tc" )) ? qtrue : qfalse;
	old_tc = ( strstr( glConfig.extensions_string, "GL_S3_s3tc" )) ? qtrue : qfalse;

	if ( old_tc )
	{
		Com_Printf ("...GL_S3_s3tc available\n" );
	}

	if ( newer_tc )
	{
		Com_Printf ("...GL_EXT_texture_compression_s3tc available\n" );
	}

	if ( !r_ext_compressed_textures->value )
	{
		// Compressed textures are off
		glConfig.textureCompression = TC_NONE;
		Com_Printf ("...ignoring texture compression\n" );
	}
	else if ( !old_tc && !newer_tc )
	{
		// Requesting texture compression, but no method found
		glConfig.textureCompression = TC_NONE;
		Com_Printf ("...no supported texture compression method found\n" );
		Com_Printf (".....ignoring texture compression\n" );
	}
	else
	{
		// some form of supported texture compression is avaiable, so see if the user has a preference
		if ( r_ext_preferred_tc_method->integer == TC_NONE )
		{
			// No preference, so pick the best
			if ( newer_tc )
			{
				Com_Printf ("...no tc preference specified\n" );
				Com_Printf (".....using GL_EXT_texture_compression_s3tc\n" );
				glConfig.textureCompression = TC_S3TC_DXT;
			}
			else
			{
				Com_Printf ("...no tc preference specified\n" );
				Com_Printf (".....using GL_S3_s3tc\n" );
				glConfig.textureCompression = TC_S3TC;
			}
		}
		else
		{
			// User has specified a preference, now see if this request can be honored
			if ( old_tc && newer_tc )
			{
				// both are avaiable, so we can use the desired tc method
				if ( r_ext_preferred_tc_method->integer == TC_S3TC )
				{
					Com_Printf ("...using preferred tc method, GL_S3_s3tc\n" );
					glConfig.textureCompression = TC_S3TC;
				}
				else
				{
					Com_Printf ("...using preferred tc method, GL_EXT_texture_compression_s3tc\n" );
					glConfig.textureCompression = TC_S3TC_DXT;
				}
			}
			else
			{
				// Both methods are not available, so this gets trickier
				if ( r_ext_preferred_tc_method->integer == TC_S3TC )
				{
					// Preferring to user older compression
					if ( old_tc )
					{
						Com_Printf ("...using GL_S3_s3tc\n" );
						glConfig.textureCompression = TC_S3TC;
					}
					else
					{
						// Drat, preference can't be honored 
						Com_Printf ("...preferred tc method, GL_S3_s3tc not available\n" );
						Com_Printf (".....falling back to GL_EXT_texture_compression_s3tc\n" );
						glConfig.textureCompression = TC_S3TC_DXT;
					}
				}
				else
				{
					// Preferring to user newer compression
					if ( newer_tc )
					{
						Com_Printf ("...using GL_EXT_texture_compression_s3tc\n" );
						glConfig.textureCompression = TC_S3TC_DXT;
					}
					else
					{
						// Drat, preference can't be honored 
						Com_Printf ("...preferred tc method, GL_EXT_texture_compression_s3tc not available\n" );
						Com_Printf (".....falling back to GL_S3_s3tc\n" );
						glConfig.textureCompression = TC_S3TC;
					}
				}
			}
		}
	}
}

/*
** GLW_InitExtensions
*/
static void GLimp_InitExtensions( void )
{
	if ( !r_allowExtensions->integer )
	{
		Com_Printf ("*** IGNORING OPENGL EXTENSIONS ***\n" );
		g_bDynamicGlowSupported = false;
		Cvar_Set( "r_DynamicGlow","0" );
		return;
	}

	Com_Printf ("Initializing OpenGL extensions\n" );

	// Select our tc scheme
	GLW_InitTextureCompression();

	// GL_EXT_texture_env_add
	glConfig.textureEnvAddAvailable = qfalse;
	if ( strstr( glConfig.extensions_string, "EXT_texture_env_add" ) )
	{
		if ( r_ext_texture_env_add->integer )
		{
			glConfig.textureEnvAddAvailable = qtrue;
			Com_Printf ("...using GL_EXT_texture_env_add\n" );
		}
		else
		{
			glConfig.textureEnvAddAvailable = qfalse;
			Com_Printf ("...ignoring GL_EXT_texture_env_add\n" );
		}
	}
	else
	{
		Com_Printf ("...GL_EXT_texture_env_add not found\n" );
	}

	// GL_EXT_texture_filter_anisotropic
	glConfig.maxTextureFilterAnisotropy = 0;
	if ( strstr( glConfig.extensions_string, "EXT_texture_filter_anisotropic" ) )
	{
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF	//can't include glext.h here ... sigh
		qglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glConfig.maxTextureFilterAnisotropy );
		Com_Printf ("...GL_EXT_texture_filter_anisotropic available\n" );

		if ( r_ext_texture_filter_anisotropic->integer>1 )
		{
			Com_Printf ("...using GL_EXT_texture_filter_anisotropic\n" );
		}
		else
		{
			Com_Printf ("...ignoring GL_EXT_texture_filter_anisotropic\n" );
		}
		Cvar_Set( "r_ext_texture_filter_anisotropic_avail", va("%f",glConfig.maxTextureFilterAnisotropy) );
		if ( r_ext_texture_filter_anisotropic->value > glConfig.maxTextureFilterAnisotropy )
		{
			Cvar_Set( "r_ext_texture_filter_anisotropic", va("%f",glConfig.maxTextureFilterAnisotropy) );
		}
	}
	else
	{
		Com_Printf ("...GL_EXT_texture_filter_anisotropic not found\n" );
		Cvar_Set( "r_ext_texture_filter_anisotropic_avail", "0" );
	}

	// GL_EXT_clamp_to_edge
	glConfig.clampToEdgeAvailable = qfalse;
	if ( strstr( glConfig.extensions_string, "GL_EXT_texture_edge_clamp" ) )
	{
		glConfig.clampToEdgeAvailable = qtrue;
		Com_Printf ("...Using GL_EXT_texture_edge_clamp\n" );
	}

#if 0
	// WGL_EXT_swap_control
	qwglSwapIntervalEXT = ( BOOL (WINAPI *)(int)) SDL_GL_GetProcAddress( "wglSwapIntervalEXT" );
	if ( qwglSwapIntervalEXT )
	{
		Com_Printf ("...using WGL_EXT_swap_control\n" );
		r_swapInterval->modified = qtrue;	// force a set next frame
	}
	else
	{
		Com_Printf ("...WGL_EXT_swap_control not found\n" );
	}
#endif

	// GL_ARB_multitexture
	qglMultiTexCoord2fARB = NULL;
	qglActiveTextureARB = NULL;
	qglClientActiveTextureARB = NULL;
	if ( strstr( glConfig.extensions_string, "GL_ARB_multitexture" )  )
	{
		if ( r_ext_multitexture->integer )
		{
			qglMultiTexCoord2fARB = ( PFNGLMULTITEXCOORD2FARBPROC ) SDL_GL_GetProcAddress( "glMultiTexCoord2fARB" );
			qglActiveTextureARB = ( PFNGLACTIVETEXTUREARBPROC ) SDL_GL_GetProcAddress( "glActiveTextureARB" );
			qglClientActiveTextureARB = ( PFNGLCLIENTACTIVETEXTUREARBPROC ) SDL_GL_GetProcAddress( "glClientActiveTextureARB" );

			if ( qglActiveTextureARB )
			{
				qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &glConfig.maxActiveTextures );

				if ( glConfig.maxActiveTextures > 1 )
				{
					Com_Printf ("...using GL_ARB_multitexture\n" );
				}
				else
				{
					qglMultiTexCoord2fARB = NULL;
					qglActiveTextureARB = NULL;
					qglClientActiveTextureARB = NULL;
					Com_Printf ("...not using GL_ARB_multitexture, < 2 texture units\n" );
				}
			}
		}
		else
		{
			Com_Printf ("...ignoring GL_ARB_multitexture\n" );
		}
	}
	else
	{
		Com_Printf ("...GL_ARB_multitexture not found\n" );
	}

	// GL_EXT_compiled_vertex_array
	qglLockArraysEXT = NULL;
	qglUnlockArraysEXT = NULL;
	if ( strstr( glConfig.extensions_string, "GL_EXT_compiled_vertex_array" ) )
	{
		if ( r_ext_compiled_vertex_array->integer )
		{
			Com_Printf ("...using GL_EXT_compiled_vertex_array\n" );
			qglLockArraysEXT = ( void ( APIENTRY * )( int, int ) ) SDL_GL_GetProcAddress( "glLockArraysEXT" );
			qglUnlockArraysEXT = ( void ( APIENTRY * )( void ) ) SDL_GL_GetProcAddress( "glUnlockArraysEXT" );
			if (!qglLockArraysEXT || !qglUnlockArraysEXT) {
				Com_Error (ERR_FATAL, "bad getprocaddress");
			}
		}
		else
		{
			Com_Printf ("...ignoring GL_EXT_compiled_vertex_array\n" );
		}
	}
	else
	{
		Com_Printf ("...GL_EXT_compiled_vertex_array not found\n" );
	}

	qglPointParameterfEXT = NULL;
	qglPointParameterfvEXT = NULL;

	//3d textures -rww
	qglTexImage3DEXT = NULL;
	qglTexSubImage3DEXT = NULL;

	if ( strstr( glConfig.extensions_string, "GL_EXT_point_parameters" ) )
	{
		if ( r_ext_compiled_vertex_array->integer || 1)
		{
			Com_Printf ("...using GL_EXT_point_parameters\n" );
			qglPointParameterfEXT = ( void ( APIENTRY * )( GLenum, GLfloat) ) SDL_GL_GetProcAddress( "glPointParameterfEXT" );
			qglPointParameterfvEXT = ( void ( APIENTRY * )( GLenum, GLfloat *) ) SDL_GL_GetProcAddress( "glPointParameterfvEXT" );

			//3d textures -rww
			qglTexImage3DEXT = (void ( APIENTRY * ) (GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *) ) SDL_GL_GetProcAddress( "glTexImage3DEXT" );
			qglTexSubImage3DEXT = (void ( APIENTRY * ) (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *) ) SDL_GL_GetProcAddress( "glTexSubImage3DEXT" );

			if (!qglPointParameterfEXT || !qglPointParameterfvEXT) 
			{
				Com_Error (ERR_FATAL, "bad getprocaddress");
			}
		}
		else
		{
			Com_Printf ("...ignoring GL_EXT_point_parameters\n" );
		}
	}
	else
	{
		Com_Printf ("...GL_EXT_point_parameters not found\n" );
	}

	bool bNVRegisterCombiners = false;
	// Register Combiners.
	if ( strstr( glConfig.extensions_string, "GL_NV_register_combiners" ) )
	{
		// NOTE: This extension requires multitexture support (over 2 units).
		if ( glConfig.maxActiveTextures >= 2 )
		{
			bNVRegisterCombiners = true;
			// Register Combiners function pointer address load.	- AReis
			// NOTE: VV guys will _definetly_ not be able to use regcoms. Pixel Shaders are just as good though :-)
			// NOTE: Also, this is an nVidia specific extension (of course), so fragment shaders would serve the same purpose
			// if we needed some kind of fragment/pixel manipulation support.
			qglCombinerParameterfvNV = ( PFNGLCOMBINERPARAMETERFVNV ) SDL_GL_GetProcAddress( "glCombinerParameterfvNV" );
			qglCombinerParameterivNV = ( PFNGLCOMBINERPARAMETERIVNV ) SDL_GL_GetProcAddress( "glCombinerParameterivNV" );
			qglCombinerParameterfNV = ( PFNGLCOMBINERPARAMETERFNV ) SDL_GL_GetProcAddress( "glCombinerParameterfNV" );
			qglCombinerParameteriNV = ( PFNGLCOMBINERPARAMETERINV ) SDL_GL_GetProcAddress( "glCombinerParameteriNV" );
			qglCombinerInputNV = ( PFNGLCOMBINERINPUTNV ) SDL_GL_GetProcAddress( "glCombinerInputNV" );
			qglCombinerOutputNV = ( PFNGLCOMBINEROUTPUTNV ) SDL_GL_GetProcAddress( "glCombinerOutputNV" );
			qglFinalCombinerInputNV = ( PFNGLFINALCOMBINERINPUTNV ) SDL_GL_GetProcAddress( "glFinalCombinerInputNV" );
			qglGetCombinerInputParameterfvNV	= ( PFNGLGETCOMBINERINPUTPARAMETERFVNV ) SDL_GL_GetProcAddress( "glGetCombinerInputParameterfvNV" );
			qglGetCombinerInputParameterivNV	= ( PFNGLGETCOMBINERINPUTPARAMETERIVNV ) SDL_GL_GetProcAddress( "glGetCombinerInputParameterivNV" );
			qglGetCombinerOutputParameterfvNV = ( PFNGLGETCOMBINEROUTPUTPARAMETERFVNV ) SDL_GL_GetProcAddress( "glGetCombinerOutputParameterfvNV" );
			qglGetCombinerOutputParameterivNV = ( PFNGLGETCOMBINEROUTPUTPARAMETERIVNV ) SDL_GL_GetProcAddress( "glGetCombinerOutputParameterivNV" );
			qglGetFinalCombinerInputParameterfvNV = ( PFNGLGETFINALCOMBINERINPUTPARAMETERFVNV ) SDL_GL_GetProcAddress( "glGetFinalCombinerInputParameterfvNV" );
			qglGetFinalCombinerInputParameterivNV = ( PFNGLGETFINALCOMBINERINPUTPARAMETERIVNV ) SDL_GL_GetProcAddress( "glGetFinalCombinerInputParameterivNV" );

			// Validate the functions we need.
			if ( !qglCombinerParameterfvNV || !qglCombinerParameterivNV || !qglCombinerParameterfNV || !qglCombinerParameteriNV || !qglCombinerInputNV ||
				 !qglCombinerOutputNV || !qglFinalCombinerInputNV || !qglGetCombinerInputParameterfvNV || !qglGetCombinerInputParameterivNV ||
				 !qglGetCombinerOutputParameterfvNV || !qglGetCombinerOutputParameterivNV || !qglGetFinalCombinerInputParameterfvNV || !qglGetFinalCombinerInputParameterivNV )
			{
				bNVRegisterCombiners = false;
				qglCombinerParameterfvNV = NULL;
				qglCombinerParameteriNV = NULL;
				Com_Printf ("...GL_NV_register_combiners failed\n" );
			}
		}
		else
		{
			bNVRegisterCombiners = false;
			Com_Printf ("...ignoring GL_NV_register_combiners\n" );
		}
	}
	else
	{
		bNVRegisterCombiners = false;
		Com_Printf ("...GL_NV_register_combiners not found\n" );
	}

	// NOTE: Vertex and Fragment Programs are very dependant on each other - this is actually a
	// good thing! So, just check to see which we support (one or the other) and load the shared
	// function pointers. ARB rocks!

	// Vertex Programs.
	bool bARBVertexProgram = false;
	if ( strstr( glConfig.extensions_string, "GL_ARB_vertex_program" ) )
	{
		bARBVertexProgram = true;
	}
	else
	{
		bARBVertexProgram = false;
		Com_Printf ("...GL_ARB_vertex_program not found\n" );
	}

	// Fragment Programs.
	bool bARBFragmentProgram = false;
	if ( strstr( glConfig.extensions_string, "GL_ARB_fragment_program" ) )
	{
		bARBFragmentProgram = true;
	}
	else
	{
		bARBFragmentProgram = false;
		Com_Printf ("...GL_ARB_fragment_program not found\n" );
	}

	// If we support one or the other, load the shared function pointers.
	if ( bARBVertexProgram || bARBFragmentProgram )
	{
		qglProgramStringARB					= (PFNGLPROGRAMSTRINGARBPROC)  SDL_GL_GetProcAddress("glProgramStringARB");
		qglBindProgramARB					= (PFNGLBINDPROGRAMARBPROC)    SDL_GL_GetProcAddress("glBindProgramARB");
		qglDeleteProgramsARB				= (PFNGLDELETEPROGRAMSARBPROC) SDL_GL_GetProcAddress("glDeleteProgramsARB");
		qglGenProgramsARB					= (PFNGLGENPROGRAMSARBPROC)    SDL_GL_GetProcAddress("glGenProgramsARB");
		qglProgramEnvParameter4dARB			= (PFNGLPROGRAMENVPARAMETER4DARBPROC)    SDL_GL_GetProcAddress("glProgramEnvParameter4dARB");
		qglProgramEnvParameter4dvARB		= (PFNGLPROGRAMENVPARAMETER4DVARBPROC)   SDL_GL_GetProcAddress("glProgramEnvParameter4dvARB");
		qglProgramEnvParameter4fARB			= (PFNGLPROGRAMENVPARAMETER4FARBPROC)    SDL_GL_GetProcAddress("glProgramEnvParameter4fARB");
		qglProgramEnvParameter4fvARB		= (PFNGLPROGRAMENVPARAMETER4FVARBPROC)   SDL_GL_GetProcAddress("glProgramEnvParameter4fvARB");
		qglProgramLocalParameter4dARB		= (PFNGLPROGRAMLOCALPARAMETER4DARBPROC)  SDL_GL_GetProcAddress("glProgramLocalParameter4dARB");
		qglProgramLocalParameter4dvARB		= (PFNGLPROGRAMLOCALPARAMETER4DVARBPROC) SDL_GL_GetProcAddress("glProgramLocalParameter4dvARB");
		qglProgramLocalParameter4fARB		= (PFNGLPROGRAMLOCALPARAMETER4FARBPROC)  SDL_GL_GetProcAddress("glProgramLocalParameter4fARB");
		qglProgramLocalParameter4fvARB		= (PFNGLPROGRAMLOCALPARAMETER4FVARBPROC) SDL_GL_GetProcAddress("glProgramLocalParameter4fvARB");
		qglGetProgramEnvParameterdvARB		= (PFNGLGETPROGRAMENVPARAMETERDVARBPROC) SDL_GL_GetProcAddress("glGetProgramEnvParameterdvARB");
		qglGetProgramEnvParameterfvARB		= (PFNGLGETPROGRAMENVPARAMETERFVARBPROC) SDL_GL_GetProcAddress("glGetProgramEnvParameterfvARB");
		qglGetProgramLocalParameterdvARB	= (PFNGLGETPROGRAMLOCALPARAMETERDVARBPROC) SDL_GL_GetProcAddress("glGetProgramLocalParameterdvARB");
		qglGetProgramLocalParameterfvARB	= (PFNGLGETPROGRAMLOCALPARAMETERFVARBPROC) SDL_GL_GetProcAddress("glGetProgramLocalParameterfvARB");
		qglGetProgramivARB					= (PFNGLGETPROGRAMIVARBPROC)     SDL_GL_GetProcAddress("glGetProgramivARB");
		qglGetProgramStringARB				= (PFNGLGETPROGRAMSTRINGARBPROC) SDL_GL_GetProcAddress("glGetProgramStringARB");
		qglIsProgramARB						= (PFNGLISPROGRAMARBPROC)        SDL_GL_GetProcAddress("glIsProgramARB");

		// Validate the functions we need.
		if ( !qglProgramStringARB || !qglBindProgramARB || !qglDeleteProgramsARB || !qglGenProgramsARB ||
			 !qglProgramEnvParameter4dARB || !qglProgramEnvParameter4dvARB || !qglProgramEnvParameter4fARB ||
             !qglProgramEnvParameter4fvARB || !qglProgramLocalParameter4dARB || !qglProgramLocalParameter4dvARB ||
             !qglProgramLocalParameter4fARB || !qglProgramLocalParameter4fvARB || !qglGetProgramEnvParameterdvARB ||
             !qglGetProgramEnvParameterfvARB || !qglGetProgramLocalParameterdvARB || !qglGetProgramLocalParameterfvARB ||
             !qglGetProgramivARB || !qglGetProgramStringARB || !qglIsProgramARB )
		{
			bARBVertexProgram = false;
			bARBFragmentProgram = false;
			qglGenProgramsARB = NULL;	//clear ptrs that get checked
			qglProgramEnvParameter4fARB = NULL;
			Com_Printf ("...ignoring GL_ARB_vertex_program\n" );
			Com_Printf ("...ignoring GL_ARB_fragment_program\n" );
		}
	}

	// Figure out which texture rectangle extension to use.
	bool bTexRectSupported = false;
	if ( Q_strnicmp( glConfig.vendor_string, "ATI Technologies",16 )==0
		&& Q_strnicmp( glConfig.version_string, "1.3.3",5 )==0 
		&& glConfig.version_string[5] < '9' ) //1.3.34 and 1.3.37 and 1.3.38 are broken for sure, 1.3.39 is not
	{
		g_bTextureRectangleHack = true;
	}
	
	if ( strstr( glConfig.extensions_string, "GL_NV_texture_rectangle" )
		   || strstr( glConfig.extensions_string, "GL_EXT_texture_rectangle" ) )
	{
		bTexRectSupported = true;
	}
	
#if 0
	// OK, so not so good to put this here, but no one else uses it!!! -AReis
	typedef const char * (WINAPI * PFNWGLGETEXTENSIONSSTRINGARBPROC) (HDC hdc);
	PFNWGLGETEXTENSIONSSTRINGARBPROC			qwglGetExtensionsStringARB;
	qwglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC) SDL_GL_GetProcAddress("wglGetExtensionsStringARB");

	const char *wglExtensions = NULL;
#endif
	bool bHasPixelFormat = false;
	bool bHasRenderTexture = false;

#if 0
	// Get the WGL extensions string.
	if ( qwglGetExtensionsStringARB )
	{
		wglExtensions = qwglGetExtensionsStringARB( glw_state.hDC );
	}

	// This externsion is used to get the wgl extension string.
	if ( wglExtensions )
	{
		// Pixel Format.
		if ( strstr( wglExtensions, "WGL_ARB_pixel_format" ) )
		{
			qwglGetPixelFormatAttribivARB			=	(PFNWGLGETPIXELFORMATATTRIBIVARBPROC) SDL_GL_GetProcAddress("wglGetPixelFormatAttribivARB");
			qwglGetPixelFormatAttribfvARB			=	(PFNWGLGETPIXELFORMATATTRIBFVARBPROC) SDL_GL_GetProcAddress("wglGetPixelFormatAttribfvARB");
			qwglChoosePixelFormatARB				=	(PFNWGLCHOOSEPIXELFORMATARBPROC) SDL_GL_GetProcAddress("wglChoosePixelFormatARB");
	
			// Validate the functions we need.
			if ( !qwglGetPixelFormatAttribivARB || !qwglGetPixelFormatAttribfvARB || !qwglChoosePixelFormatARB )
			{
				Com_Printf ("...ignoring WGL_ARB_pixel_format\n" );
			}
			else
			{
				bHasPixelFormat = true;
			}
		}
		else
		{
			Com_Printf ("...ignoring WGL_ARB_pixel_format\n" );
		}

		// Offscreen pixel-buffer.
		// NOTE: VV guys can use the equivelant SetRenderTarget() with the correct texture surfaces.
		bool bWGLARBPbuffer = false;
		if ( strstr( wglExtensions, "WGL_ARB_pbuffer" ) && bHasPixelFormat )
		{
			bWGLARBPbuffer = true;
			qwglCreatePbufferARB		=	(PFNWGLCREATEPBUFFERARBPROC) SDL_GL_GetProcAddress("wglCreatePbufferARB");
			qwglGetPbufferDCARB			=	(PFNWGLGETPBUFFERDCARBPROC) SDL_GL_GetProcAddress("wglGetPbufferDCARB");
			qwglReleasePbufferDCARB		=	(PFNWGLRELEASEPBUFFERDCARBPROC) SDL_GL_GetProcAddress("wglReleasePbufferDCARB");
			qwglDestroyPbufferARB		=	(PFNWGLDESTROYPBUFFERARBPROC) SDL_GL_GetProcAddress("wglDestroyPbufferARB");
			qwglQueryPbufferARB			=	(PFNWGLQUERYPBUFFERARBPROC) SDL_GL_GetProcAddress("wglQueryPbufferARB");
	
			// Validate the functions we need.
			if ( !qwglCreatePbufferARB || !qwglGetPbufferDCARB || !qwglReleasePbufferDCARB || !qwglDestroyPbufferARB || !qwglQueryPbufferARB )
			{
				bWGLARBPbuffer = false;
				Com_Printf ("...WGL_ARB_pbuffer failed\n" );
			}
		}
		else
		{
			bWGLARBPbuffer = false;
			Com_Printf ("...WGL_ARB_pbuffer not found\n" );
		}

		// Render-Texture (requires pbuffer ext (and it's dependancies of course).
		if ( strstr( wglExtensions, "WGL_ARB_render_texture" ) && bWGLARBPbuffer )
		{
			qwglBindTexImageARB			=	(PFNWGLBINDTEXIMAGEARBPROC) SDL_GL_GetProcAddress("wglBindTexImageARB");
			qwglReleaseTexImageARB		=	(PFNWGLRELEASETEXIMAGEARBPROC) SDL_GL_GetProcAddress("wglReleaseTexImageARB");
			qwglSetPbufferAttribARB		=	(PFNWGLSETPBUFFERATTRIBARBPROC) SDL_GL_GetProcAddress("wglSetPbufferAttribARB");
	
			// Validate the functions we need.
			if ( !qwglCreatePbufferARB || !qwglGetPbufferDCARB || !qwglReleasePbufferDCARB || !qwglDestroyPbufferARB || !qwglQueryPbufferARB )
			{
				Com_Printf ("...ignoring WGL_ARB_render_texture\n" );
			}
			else
			{
				bHasRenderTexture = true;
			}
		}
		else
		{
			Com_Printf ("...ignoring WGL_ARB_render_texture\n" );
		}
	}
#endif

	// Find out how many general combiners they have.
	#define GL_MAX_GENERAL_COMBINERS_NV       0x854D
	GLint iNumGeneralCombiners = 0;
	qglGetIntegerv( GL_MAX_GENERAL_COMBINERS_NV, &iNumGeneralCombiners );

	// Only allow dynamic glows/flares if they have the hardware
	if ( bTexRectSupported && bARBVertexProgram && bHasRenderTexture && qglActiveTextureARB && glConfig.maxActiveTextures >= 4 &&
		( ( bNVRegisterCombiners && iNumGeneralCombiners >= 2 ) || bARBFragmentProgram ) )
	{
		g_bDynamicGlowSupported = true;
		// this would overwrite any achived setting gwg
		// Cvar_Set( "r_DynamicGlow", "1" );
	}
	else
	{
		g_bDynamicGlowSupported = false;
		Cvar_Set( "r_DynamicGlow","0" );
	}
}

#define R_MODE_FALLBACK 3 // 640 * 480

/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
void GLimp_Init( void )
{
	r_allowSoftwareGL = Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );
	r_sdlDriver = Cvar_Get( "r_sdlDriver", "", CVAR_ROM );
	r_allowResize = Cvar_Get( "r_allowResize", "0", CVAR_ARCHIVE );
	r_centerWindow = Cvar_Get( "r_centerWindow", "0", CVAR_ARCHIVE );
	r_noborder = Cvar_Get( "r_noborder", "0", CVAR_ARCHIVE );
	qboolean fullscreen, noborder;

	if( Cvar_VariableIntegerValue( "com_abnormalExit" ) )
	{
		Cvar_Set( "r_mode", va( "%d", R_MODE_FALLBACK ) );
		Cvar_Set( "r_fullscreen", "0" );
		Cvar_Set( "r_centerWindow", "0" );
		Cvar_Set( "com_abnormalExit", "0" );
	}

#ifdef notyet
	Sys_SetEnv( "SDL_VIDEO_CENTERED", r_centerWindow->integer ? "1" : "" );
#endif

	Sys_GLimpInit( );

	fullscreen = (r_fullscreen->integer) ? qtrue : qfalse;
	noborder = (r_noborder->integer) ? qtrue : qfalse;

	// Create the window and set up the context
	if(GLimp_StartDriverAndSetMode(r_mode->integer, fullscreen, noborder))
		goto success;

	// Try again, this time in a platform specific "safe mode"
	Sys_GLimpSafeInit( );

	if(GLimp_StartDriverAndSetMode(r_mode->integer, fullscreen, qfalse))
		goto success;

	// Finally, try the default screen resolution
	if( r_mode->integer != R_MODE_FALLBACK )
	{
		Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n",
				r_mode->integer, R_MODE_FALLBACK );

		if(GLimp_StartDriverAndSetMode(R_MODE_FALLBACK, qfalse, qfalse))
			goto success;
	}

	// Nothing worked, give up
	Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );

success:
#if 0
	// This values force the UI to disable driver selection
	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;
#endif
	glConfig.deviceSupportsGamma = (SDL_SetGamma( 1.0f, 1.0f, 1.0f ) >= 0) ? qtrue : qfalse;

	// Mysteriously, if you use an NVidia graphics card and multiple monitors,
	// SDL_SetGamma will incorrectly return false... the first time; ask
	// again and you get the correct answer. This is a suspected driver bug, see
	// http://bugzilla.icculus.org/show_bug.cgi?id=4316
	glConfig.deviceSupportsGamma = (SDL_SetGamma( 1.0f, 1.0f, 1.0f ) >= 0) ? qtrue : qfalse;

	if ( -1 == r_ignorehwgamma->integer)
		glConfig.deviceSupportsGamma = qtrue;

	if ( 1 == r_ignorehwgamma->integer)
		glConfig.deviceSupportsGamma = qfalse;

	// get our config strings
	glConfig.vendor_string = (const char *) qglGetString (GL_VENDOR);
	glConfig.renderer_string =  (const char *) qglGetString (GL_RENDERER);
	glConfig.version_string = (const char *) qglGetString (GL_VERSION);
	glConfig.extensions_string = (const char *) qglGetString (GL_EXTENSIONS);

	// OpenGL driver constants
	qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize );
	// stubbed or broken drivers may have reported 0...
	if ( glConfig.maxTextureSize <= 0 )
	{
		glConfig.maxTextureSize = 0;
	}

	// initialize extensions
	GLimp_InitExtensions( );

	Cvar_Get( "r_availableModes", "", CVAR_ROM );

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init( );
}


/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
	// don't flip if drawing to front buffer
	//if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		SDL_GL_SwapBuffers();
	}

	if( r_fullscreen->modified )
	{
		qboolean    fullscreen;
		qboolean    needToToggle = qtrue;
		qboolean    sdlToggled = qfalse;
		SDL_Surface *s = SDL_GetVideoSurface( );

		if( s )
		{
			// Find out the current state
			fullscreen = (!!( s->flags & SDL_FULLSCREEN )) ? qtrue : qfalse;
				
			if( r_fullscreen->integer && Cvar_VariableIntegerValue( "in_nograb" ) )
			{
				Com_Printf( "Fullscreen not allowed with in_nograb 1\n");
				Cvar_Set( "r_fullscreen", "0" );
				r_fullscreen->modified = qfalse;
			}

			// Is the state we want different from the current state?
			needToToggle = (!!r_fullscreen->integer != fullscreen) ? qtrue : qfalse;

			if( needToToggle )
				sdlToggled = SDL_WM_ToggleFullScreen( s );
		}

		if( needToToggle )
		{
			// SDL_WM_ToggleFullScreen didn't work, so do it the slow way
			if( !sdlToggled )
				Cbuf_AddText( "vid_restart\n" );

			IN_Restart( );
		}

		r_fullscreen->modified = qfalse;
	}
}
