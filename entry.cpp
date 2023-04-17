#include <Windows.h>
#include <thread>

#include "client.hpp"

BOOL WINAPI DllMain( HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved )
{
	switch ( fdwReason )
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls( hinstDLL );
		std::thread{ ac::client::main }.detach( );
		break;
	}
	return TRUE;
}