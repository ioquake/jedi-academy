// vmachine.cpp -- wrapper to fake virtual machine for client

#include "../game/q_shared.h"
#include "vmachine.h"
#pragma warning (disable : 4514)
/*
==============================================================

VIRTUAL MACHINE

==============================================================
*/
intptr_t	VM_Call( intptr_t callnum, ... )
{
	intptr_t args[9];
	int i;
	va_list ap;
//	assert (cgvm.entryPoint);
	
	if (cgvm.entryPoint)
	{
		va_start(ap, callnum);
		for (i = 0; i < 9; i++) {
			args[i] = va_arg(ap, intptr_t);
		}
		va_end(ap);

		return cgvm.entryPoint( callnum, args[0], args[1], args[2],
			args[3], args[4], args[5], args[6], args[7], args[8] );
	}
	
	return -1;
}

/*
============
VM_DllSyscall

we pass this to the cgame dll to call back into the client
============
*/
extern intptr_t CL_CgameSystemCalls( intptr_t *args );
extern intptr_t CL_UISystemCalls( intptr_t *args );

intptr_t VM_DllSyscall( intptr_t arg, ... ) {
#if !defined(__i386__)  || defined(__clang__)
	intptr_t args[15];
	int i;
	va_list ap;

	args[0] = arg;

	va_start(ap, arg);
	for (i = 1; i < 15; i++)
		args[i] = va_arg(ap, intptr_t);
	va_end(ap);

	return CL_CgameSystemCalls( args );
#else
//	return cgvm->systemCall( &arg );
	return CL_CgameSystemCalls( &arg );
#endif
}
