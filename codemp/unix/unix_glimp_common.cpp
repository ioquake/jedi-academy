#include <pthread.h>
#include <semaphore.h>
#include "../game/q_shared.h"

/*
===========================================================

SMP acceleration

===========================================================
*/

sem_t	renderCommandsEvent;
sem_t	renderCompletedEvent;
sem_t	renderActiveEvent;

void (*glimpRenderThread)( void );

void *GLimp_RenderThreadWrapper( void *stub ) {
	glimpRenderThread();
}

/*
=======================
GLimp_SpawnRenderThread
=======================
*/
pthread_t	renderThreadHandle;
qboolean GLimp_SpawnRenderThread( void (*function)( void ) ) {

	sem_init( &renderCommandsEvent, 0, 0 );
	sem_init( &renderCompletedEvent, 0, 0 );
	sem_init( &renderActiveEvent, 0, 0 );

	glimpRenderThread = function;

	if (pthread_create( &renderThreadHandle, NULL,
		GLimp_RenderThreadWrapper, NULL)) {
		return qfalse;
	}

	return qtrue;
}

static	void	*smpData;

void *GLimp_RendererSleep( void ) {
	void	*data;

	// after this, the front end can exit GLimp_FrontEndSleep
	sem_post ( &renderCompletedEvent );

	sem_wait ( &renderCommandsEvent );

	data = smpData;

	// after this, the main thread can exit GLimp_WakeRenderer
	sem_post ( &renderActiveEvent );

	return data;
}

void GLimp_FrontEndSleep( void ) {
	sem_wait ( &renderCompletedEvent );
}

void GLimp_WakeRenderer( void *data ) {
	smpData = data;

	// after this, the renderer can continue through GLimp_RendererSleep
	sem_post( &renderCommandsEvent );

	sem_wait( &renderActiveEvent );
}

void Sys_GLimpInit( void )
{
}

void Sys_GLimpSafeInit( void )
{
}
