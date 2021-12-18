#ifdef DEFINE_CALLS
DEF_CALL(int, initialize, const char *exePath,
         const char *appDomainFriendlyName, int propertyCount,
         const char **propertyKeys, const char **propertyValues,
         void **hostHandle, unsigned int *domainId)
DEF_CALL(int, create_delegate, void *hostHandle, unsigned int domainId,
         const char *entryPointAssemblyName, const char *entryPointTypeName,
         const char *entryPointMethodName, void **delegate)
#endif

#ifndef CORECLR_H
#define CORECLR_H

#define IMPORT_PREFIX coreclr
#if _WIN32
// On Windows (compiled with MSVC), CoreCLR uses stdcall for x86
#define IMPORT_CONVENTION __stdcall
#else
// TODO: Check if this is true, most likely it is
#define IMPORT_CONVENTION __cdecl
#endif
#include "func_import.h"
#undef IMPORT_PREFIX
#undef IMPORT_CONVENTION

#endif