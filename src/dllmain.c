#include <windows.h>
#include "ddraw.h"
#include <stdio.h>
#include "dllmain.h"
#include "IDirectDraw.h"
#include "dd.h"
#include "ddclipper.h"
#include "debug.h"
#include "config.h"
#include "hook.h"


/* export for cncnet cnc games */
BOOL GameHandlesClose;

/* export for some warcraft II tools */
PVOID FakePrimarySurface;


HMODULE g_ddraw_module;

#if !defined(WINAPI_FAMILY_PARTITION)

#define _DPI_AWARENESS_CONTEXTS_

DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);

typedef enum DPI_AWARENESS
{
    DPI_AWARENESS_INVALID = -1,
    DPI_AWARENESS_UNAWARE = 0,
    DPI_AWARENESS_SYSTEM_AWARE = 1,
    DPI_AWARENESS_PER_MONITOR_AWARE = 2
} DPI_AWARENESS;

#define DPI_AWARENESS_CONTEXT_UNAWARE               ((DPI_AWARENESS_CONTEXT)-1)
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE          ((DPI_AWARENESS_CONTEXT)-2)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE     ((DPI_AWARENESS_CONTEXT)-3)
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2  ((DPI_AWARENESS_CONTEXT)-4)
#define DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED     ((DPI_AWARENESS_CONTEXT)-5)

typedef enum DPI_HOSTING_BEHAVIOR
{
    DPI_HOSTING_BEHAVIOR_INVALID = -1,
    DPI_HOSTING_BEHAVIOR_DEFAULT = 0,
    DPI_HOSTING_BEHAVIOR_MIXED = 1
} DPI_HOSTING_BEHAVIOR;

#endif

BOOL WINAPI DllMain(HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
#if defined(LOG_ENABLED)
        dbg_init();
        TRACE("cnc-ddraw = %p\n", hDll);
#endif

#if _DEBUG 
        SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)dbg_exception_handler);
#endif
        g_ddraw_module = hDll;

        char buf[1024];

        if (GetEnvironmentVariable("__COMPAT_LAYER", buf, sizeof(buf)))
        {
            char* s = strtok(buf, " ");

            while (s)
            {
                if (_strcmpi(s, "WIN95") == 0 || _strcmpi(s, "WIN98") == 0 || _strcmpi(s, "NT4SP5") == 0)
                {
                    char mes[128] = { 0 };

                    _snprintf(
                        mes,
                        sizeof(mes),
                        "Please disable the '%s' compatibility mode for all game executables and "
                        "then try to start the game again.",
                        s);

                    MessageBoxA(NULL, mes, "Compatibility modes detected - cnc-ddraw", MB_OK);

                    break;
                }

                s = strtok(NULL, " ");
            }
        }

        BOOL set_dpi_aware = FALSE;

        HMODULE shcore_dll = GetModuleHandle("shcore.dll");
        HMODULE user32_dll = GetModuleHandle("user32.dll");

        if (user32_dll)
        {
            SETPROCESSDPIAWARENESSCONTEXTPROC set_awareness_context =
                (SETPROCESSDPIAWARENESSCONTEXTPROC)GetProcAddress(user32_dll, "SetProcessDpiAwarenessContext");

            if (set_awareness_context)
            {
                set_dpi_aware = set_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            }
        }

        if (!set_dpi_aware && shcore_dll)
        {
            SETPROCESSDPIAWERENESSPROC set_awareness =
                (SETPROCESSDPIAWERENESSPROC)GetProcAddress(shcore_dll, "SetProcessDpiAwareness");

            if (set_awareness)
            {
                HRESULT result = set_awareness(PROCESS_PER_MONITOR_DPI_AWARE);

                set_dpi_aware = result == S_OK || result == E_ACCESSDENIED;
            }
        }

        if (!set_dpi_aware && user32_dll)
        {
            SETPROCESSDPIAWAREPROC set_aware =
                (SETPROCESSDPIAWAREPROC)GetProcAddress(user32_dll, "SetProcessDPIAware");

            if (set_aware)
                set_aware();
        }

        timeBeginPeriod(1);
        hook_early_init();
        break;
    }
    case DLL_PROCESS_DETACH:
    {
        TRACE("cnc-ddraw DLL_PROCESS_DETACH\n");

        cfg_save();

        timeEndPeriod(1);
        hook_exit();
        break;
    }
    }

    return TRUE;
}

HRESULT WINAPI DirectDrawCreate(GUID FAR* lpGUID, LPDIRECTDRAW FAR* lplpDD, IUnknown FAR* pUnkOuter)
{
    TRACE("-> %s(lpGUID=%p, lplpDD=%p, pUnkOuter=%p)\n", __FUNCTION__, lpGUID, lplpDD, pUnkOuter);
    HRESULT ret = dd_CreateEx(lpGUID, (LPVOID*)lplpDD, &IID_IDirectDraw, pUnkOuter);
    TRACE("<- %s\n", __FUNCTION__);
    return ret;
}

HRESULT WINAPI DirectDrawCreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER FAR* lplpDDClipper, IUnknown FAR* pUnkOuter)
{
    TRACE("-> %s(dwFlags=%08X, DDClipper=%p, unkOuter=%p)\n", __FUNCTION__, (int)dwFlags, lplpDDClipper, pUnkOuter);
    HRESULT ret = dd_CreateClipper(dwFlags, (IDirectDrawClipperImpl**)lplpDDClipper, pUnkOuter);
    TRACE("<- %s\n", __FUNCTION__);
    return ret;
}

HRESULT WINAPI DirectDrawCreateEx(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter)
{
    TRACE("-> %s(lpGUID=%p, lplpDD=%p, riid=%08X, pUnkOuter=%p)\n", __FUNCTION__, lpGuid, lplpDD, iid, pUnkOuter);
    HRESULT ret = dd_CreateEx(lpGuid, lplpDD, &IID_IDirectDraw7, pUnkOuter);
    TRACE("<- %s\n", __FUNCTION__);
    return ret;
}

HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACK lpCallback, LPVOID lpContext)
{
    TRACE("-> %s(lpCallback=%p, lpContext=%p)\n", __FUNCTION__, lpCallback, lpContext);

    if (lpCallback)
        lpCallback(NULL, "Primary Display Driver", "display", lpContext);

    TRACE("<- %s\n", __FUNCTION__);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA lpCallback, LPVOID lpContext, DWORD dwFlags)
{
    TRACE("-> %s(lpCallback=%p, lpContext=%p, dwFlags=%d)\n", __FUNCTION__, lpCallback, lpContext, dwFlags);

    if (lpCallback)
        lpCallback(NULL, "Primary Display Driver", "display", lpContext, NULL);

    TRACE("<- %s\n", __FUNCTION__);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExW(LPDDENUMCALLBACKEXW lpCallback, LPVOID lpContext, DWORD dwFlags)
{
    TRACE("-> %s(lpCallback=%p, lpContext=%p, dwFlags=%d)\n", __FUNCTION__, lpCallback, lpContext, dwFlags);

    if (lpCallback)
        lpCallback(NULL, L"Primary Display Driver", L"display", lpContext, NULL);

    TRACE("<- %s\n", __FUNCTION__);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateW(LPDDENUMCALLBACKW lpCallback, LPVOID lpContext)
{
    TRACE("-> %s(lpCallback=%p, lpContext=%p)\n", __FUNCTION__, lpCallback, lpContext);

    if (lpCallback)
        lpCallback(NULL, L"Primary Display Driver", L"display", lpContext);

    TRACE("<- %s\n", __FUNCTION__);
    return DD_OK;
}

DWORD WINAPI CompleteCreateSysmemSurface(DWORD a)
{
    TRACE("NOT_IMPLEMENTED -> %s()\n", __FUNCTION__);
    DWORD ret = 0;
    TRACE("NOT_IMPLEMENTED <- %s\n", __FUNCTION__);
    return ret;
}

HRESULT WINAPI D3DParseUnknownCommand(LPVOID lpCmd, LPVOID* lpRetCmd)
{
    TRACE("NOT_IMPLEMENTED -> %s(lpCmd=%p, lpRetCmd=%p)\n", __FUNCTION__, lpCmd, lpRetCmd);
    HRESULT ret = E_FAIL;
    TRACE("NOT_IMPLEMENTED <- %s\n", __FUNCTION__);
    return ret;
}

DWORD WINAPI AcquireDDThreadLock()
{
    TRACE("NOT_IMPLEMENTED -> %s()\n", __FUNCTION__);
    DWORD ret = 0;
    TRACE("NOT_IMPLEMENTED <- %s\n", __FUNCTION__);
    return ret;
}

DWORD WINAPI ReleaseDDThreadLock()
{
    TRACE("NOT_IMPLEMENTED -> %s()\n", __FUNCTION__);
    DWORD ret = 0;
    TRACE("NOT_IMPLEMENTED <- %s\n", __FUNCTION__);
    return ret;
}

DWORD WINAPI DDInternalLock(DWORD a, DWORD b)
{
    TRACE("NOT_IMPLEMENTED -> %s()\n", __FUNCTION__);
    DWORD ret = 0;
    TRACE("NOT_IMPLEMENTED <- %s\n", __FUNCTION__);
    return ret;
}

DWORD WINAPI DDInternalUnlock(DWORD a)
{
    TRACE("NOT_IMPLEMENTED -> %s()\n", __FUNCTION__);
    DWORD ret = 0;
    TRACE("NOT_IMPLEMENTED <- %s\n", __FUNCTION__);
    return ret;
}
