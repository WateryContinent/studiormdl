//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//
//=============================================================================//
#include <stdio.h>
#include <windows.h>   // GetModuleFileNameA, LoadLibraryA
#include "physdll.h"
#include "filesystem_tools.h"

// Load a DLL by name, first trying the directory our .exe lives in, then the
// normal Sys_LoadModule/filesystem fallback.  This matters because Sys_LoadModule
// with a bare name resolves to  CWD/bin/<name>  which is wrong when studiomdl is
// invoked from a QC directory.
static CSysModule *LoadFromExeDir( const char *pDLLName )
{
    // --- 1. Build <exedir>\<dllname> and try that first ---
    char exePath[MAX_PATH];
    if ( GetModuleFileNameA( NULL, exePath, sizeof(exePath) ) )
    {
        char *lastSlash = strrchr( exePath, '\\' );
        if ( lastSlash )
        {
            strcpy( lastSlash + 1, pDLLName );
            HMODULE hMod = LoadLibraryA( exePath );
            if ( hMod )
                return reinterpret_cast<CSysModule *>( hMod );
        }
    }

    // --- 2. Filesystem-based search (search paths + bare LoadLibrary fallback) ---
    return g_pFullFileSystem->LoadModule( pDLLName );
}

static CSysModule *pPhysicsModule = NULL;

CreateInterfaceFn GetPhysicsFactory( void )
{
    if ( !pPhysicsModule )
    {
        pPhysicsModule = LoadFromExeDir( "vphysics.dll" );
        if ( !pPhysicsModule )
            return NULL;
    }

    return Sys_GetFactory( pPhysicsModule );
}

void PhysicsDLLPath( const char *pPathname )
{
    if ( !pPhysicsModule )
    {
        pPhysicsModule = LoadFromExeDir( pPathname );
    }
}
