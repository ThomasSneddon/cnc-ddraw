/*
 * Copyright (c) 2010 Toni Spets <toni.spets@iki.fi>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <ctype.h>
#include <d3d9.h>
#include <initguid.h>
#include "ddraw.h"
#include "main.h"
#include "opengl.h"
#include "palette.h"
#include "surface.h"
#include "clipper.h"
#include "render_d3d9.h"

#define IDR_MYMENU 93

/* from mouse.c */
BOOL WINAPI fake_GetCursorPos(LPPOINT lpPoint);
void mouse_init();
void mouse_lock();
void mouse_unlock();

/* from screenshot.c */
BOOL screenshot(struct IDirectDrawSurfaceImpl *);
void Settings_Load();
void Settings_Save(RECT *lpRect, int windowState);

IDirectDrawImpl *ddraw = NULL;

RECT WindowRect = { .left = -32000, .top = -32000, .right = 0, .bottom = 0 };
int WindowState = -1;
BOOL Direct3D9Active;
BOOL GameHandlesClose;
BOOL ChildWindowExists;

//BOOL WINAPI DllMainCRTStartup(HINSTANCE hinstDLL, DWORD dwReason, LPVOID lpvReserved)
BOOL WINAPI DllMain(HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            printf("cnc-ddraw DLL_PROCESS_ATTACH\n");
            
            //SetProcessPriorityBoost(GetCurrentProcess(), TRUE);
            
            BOOL setDpiAware = FALSE;
            HMODULE hShcore = GetModuleHandle("shcore.dll");
            typedef HRESULT (__stdcall* SetProcessDpiAwareness_)(PROCESS_DPI_AWARENESS value);
            if(hShcore)
            {
                SetProcessDpiAwareness_ setProcessDpiAwareness = 
                    (SetProcessDpiAwareness_)GetProcAddress(hShcore, "SetProcessDpiAwareness");
                    
                if (setProcessDpiAwareness)
                {
                    HRESULT result = setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
                    setDpiAware = result == S_OK || result == E_ACCESSDENIED;
                }   
            }
            if (!setDpiAware)
            {
                HMODULE hUser32 = GetModuleHandle("user32.dll");
                typedef BOOL (__stdcall* SetProcessDPIAware_)();
                if(hUser32)
                {
                    SetProcessDPIAware_ setProcessDPIAware = 
                        (SetProcessDPIAware_)GetProcAddress(hUser32, "SetProcessDPIAware");
                        
                    if (setProcessDPIAware) 
                        setProcessDPIAware();
                }
            }
            
            timeBeginPeriod(1);
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            printf("cnc-ddraw DLL_PROCESS_DETACH\n");
            
            Settings_Save(&WindowRect, WindowState);

            timeEndPeriod(1);
            break;
        }
    }

    return TRUE;
}

BOOL CALLBACK EnumChildProc(HWND hWnd, LPARAM lParam)
{
    ChildWindowExists = TRUE;

    IDirectDrawSurfaceImpl *this = (IDirectDrawSurfaceImpl *)lParam;

    HDC hDC = GetDC(hWnd);

    RECT size;
    GetClientRect(hWnd, &size);

    RECT pos;
    GetWindowRect(hWnd, &pos);

    MapWindowPoints(HWND_DESKTOP, ddraw->hWnd, (LPPOINT)&pos, 2);

    BitBlt(hDC, 0, 0, size.right, size.bottom, this->hDC, pos.left, pos.top, SRCCOPY);

    ReleaseDC(hWnd, hDC);

    return FALSE;
}

static unsigned char getPixel(int x, int y)
{
    return ((unsigned char *)ddraw->primary->surface)[y*ddraw->primary->lPitch + x*ddraw->primary->lXPitch];
}

int* InMovie = (int*)0x00665F58;
int* IsVQA640 = (int*)0x0065D7BC;
BYTE* ShouldStretch = (BYTE*)0x00607D78;

BOOL detect_cutscene()
{
    if (ddraw->width <= CUTSCENE_WIDTH || ddraw->height <= CUTSCENE_HEIGHT)
        return FALSE;

    if (ddraw->isredalert)
    {
        if ((*InMovie && !*IsVQA640) || *ShouldStretch)
        {
            return TRUE;
        }
        return FALSE;
    }
    else if (ddraw->iscnc1)
    {
        return getPixel(CUTSCENE_WIDTH + 1, 0) == 0 || getPixel(CUTSCENE_WIDTH + 5, 1) == 0 ? TRUE : FALSE;
    }

    return FALSE;
}

void LimitGameTicks(BOOL isBltOrFlip)
{
    static DWORD nextGameTick;
    static DWORD lastBltOrFlipTick;
    if (!nextGameTick)
    {
        nextGameTick = timeGetTime();
        return;
    }

    DWORD tickCount = timeGetTime();

    if (isBltOrFlip)
        lastBltOrFlipTick = tickCount;
    else if (lastBltOrFlipTick + (ddraw->ticklength * 2) >= tickCount)
        return;

    nextGameTick += ddraw->ticklength;
    int sleepTime = nextGameTick - tickCount;

    if (sleepTime <= 0 || sleepTime > ddraw->ticklength)
        nextGameTick = tickCount;
    else
        Sleep(sleepTime);
}

HRESULT __stdcall ddraw_Compact(IDirectDrawImpl *This)
{
    printf("DirectDraw::Compact(This=%p) ???\n", This);

    return DD_OK;
}

HRESULT __stdcall ddraw_DuplicateSurface(IDirectDrawImpl *This, LPDIRECTDRAWSURFACE src, LPDIRECTDRAWSURFACE *dest)
{
    printf("DirectDraw::DuplicateSurface(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_EnumDisplayModes(IDirectDrawImpl *This, DWORD dwFlags, LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK lpEnumModesCallback)
{
    DWORD i = 0;
    DDSURFACEDESC s;

    printf("DirectDraw::EnumDisplayModes(This=%p, dwFlags=%08X, lpDDSurfaceDesc=%p, lpContext=%p, lpEnumModesCallback=%p)\n", This, (unsigned int)dwFlags, lpDDSurfaceDesc, lpContext, lpEnumModesCallback);

    if (lpDDSurfaceDesc != NULL)
    {
        return DDERR_UNSUPPORTED;
    }

    SIZE resolutions[] =
    {
        { 320, 200 },
        { 640, 400 },
        { 640, 480 },
        { 800, 600 },
        { 1024, 768 },
        { 1280, 1024 },
        { 1280, 720 },
        { 1920, 1080 },
    };

    for (i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++)
    {
        memset(&s, 0, sizeof(DDSURFACEDESC));
        s.dwSize = sizeof(DDSURFACEDESC);
        s.dwFlags = DDSD_HEIGHT | DDSD_REFRESHRATE | DDSD_WIDTH | DDSD_PIXELFORMAT;
        s.dwHeight = resolutions[i].cy;
        s.dwWidth = resolutions[i].cx;
        s.dwRefreshRate = 60;
        s.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        s.ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
        s.ddpfPixelFormat.dwRGBBitCount = 8;

        if (lpEnumModesCallback(&s, lpContext) == DDENUMRET_CANCEL)
        {
            printf("    DDENUMRET_CANCEL returned, stopping\n");
            break;
        }

        s.ddpfPixelFormat.dwFlags = DDPF_RGB;
        s.ddpfPixelFormat.dwRGBBitCount = 16;
        s.ddpfPixelFormat.dwRBitMask = 0xF800;
        s.ddpfPixelFormat.dwGBitMask = 0x07E0;
        s.ddpfPixelFormat.dwBBitMask = 0x001F;

        if (lpEnumModesCallback(&s, lpContext) == DDENUMRET_CANCEL)
        {
            printf("    DDENUMRET_CANCEL returned, stopping\n");
            break;
        }
    }

    /* Some games crash when you feed them with too many resolutions...
    DEVMODE m;
    while (EnumDisplaySettings(NULL, i, &m))
    {
#if _DEBUG_X
        printf("  %d: %dx%d@%d %d bpp\n", (int)i, (int)m.dmPelsWidth, (int)m.dmPelsHeight, (int)m.dmDisplayFrequency, (int)m.dmBitsPerPel);
#endif
        memset(&s, 0, sizeof(DDSURFACEDESC));
        s.dwSize = sizeof(DDSURFACEDESC);
        s.dwFlags = DDSD_HEIGHT | DDSD_REFRESHRATE | DDSD_WIDTH | DDSD_PIXELFORMAT;
        s.dwHeight = m.dmPelsHeight;
        s.dwWidth = m.dmPelsWidth;
        s.dwRefreshRate = m.dmDisplayFrequency;
        s.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        s.ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8 | DDPF_RGB;
        s.ddpfPixelFormat.dwRGBBitCount = 8;

        if (lpEnumModesCallback(&s, lpContext) == DDENUMRET_CANCEL)
        {
            printf("    DDENUMRET_CANCEL returned, stopping\n");
            break;
        }
        i++;
    }
    */
    return DD_OK;
}

HRESULT __stdcall ddraw_EnumSurfaces(IDirectDrawImpl *This, DWORD a, LPDDSURFACEDESC b, LPVOID c, LPDDENUMSURFACESCALLBACK d)
{
    printf("DirectDraw::EnumSurfaces(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_FlipToGDISurface(IDirectDrawImpl *This)
{
    printf("DirectDraw::FlipToGDISurface(This=%p) ???\n", This);

    return DD_OK;
}

HRESULT __stdcall ddraw_GetCaps(IDirectDrawImpl *This, LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDEmulCaps)
{
    printf("DirectDraw::GetCaps(This=%p, lpDDDriverCaps=%p, lpDDEmulCaps=%p)\n", This, lpDDDriverCaps, lpDDEmulCaps);

    if(lpDDDriverCaps)
    {
        lpDDDriverCaps->dwSize = sizeof(DDCAPS);
        lpDDDriverCaps->dwCaps = DDCAPS_BLT|DDCAPS_PALETTE;
        lpDDDriverCaps->dwCKeyCaps = 0;
        lpDDDriverCaps->dwPalCaps = DDPCAPS_8BIT|DDPCAPS_PRIMARYSURFACE;
        lpDDDriverCaps->dwVidMemTotal = 16777216;
        lpDDDriverCaps->dwVidMemFree = 16777216;
        lpDDDriverCaps->dwMaxVisibleOverlays = 0;
        lpDDDriverCaps->dwCurrVisibleOverlays = 0;
        lpDDDriverCaps->dwNumFourCCCodes = 0;
        lpDDDriverCaps->dwAlignBoundarySrc = 0;
        lpDDDriverCaps->dwAlignSizeSrc = 0;
        lpDDDriverCaps->dwAlignBoundaryDest = 0;
        lpDDDriverCaps->dwAlignSizeDest = 0;
    }

    if(lpDDEmulCaps)
    {
        lpDDEmulCaps->dwSize = 0;
    }

    return DD_OK;
}

HRESULT __stdcall ddraw_GetDisplayMode(IDirectDrawImpl *This, LPDDSURFACEDESC a)
{
    printf("DirectDraw::GetDisplayMode(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_GetFourCCCodes(IDirectDrawImpl *This, LPDWORD a, LPDWORD b)
{
    printf("DirectDraw::GetFourCCCodes(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_GetGDISurface(IDirectDrawImpl *This, LPDIRECTDRAWSURFACE *a)
{
    printf("DirectDraw::GetGDISurface(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_GetMonitorFrequency(IDirectDrawImpl *This, LPDWORD a)
{
    printf("DirectDraw::GetMonitorFrequency(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_GetScanLine(IDirectDrawImpl *This, LPDWORD a)
{
    printf("DirectDraw::GetScanLine(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_GetVerticalBlankStatus(IDirectDrawImpl *This, LPBOOL lpbIsInVB)
{
    printf("DirectDraw::GetVerticalBlankStatus(This=%p, ...) ???\n", This);
    *lpbIsInVB = TRUE;
    return DD_OK;
}

HRESULT __stdcall ddraw_Initialize(IDirectDrawImpl *This, GUID *a)
{
    printf("DirectDraw::Initialize(This=%p, ...) ???\n", This);
    return DD_OK;
}

HRESULT __stdcall ddraw_RestoreDisplayMode(IDirectDrawImpl *This)
{
    printf("DirectDraw::RestoreDisplayMode(This=%p)\n", This);

    if(!This->render.run)
    {
        return DD_OK;
    }

    /* only stop drawing in GL mode when minimized */
    if (This->renderer != render_soft_main)
    {
        EnterCriticalSection(&This->cs);
        This->render.run = FALSE;
        ReleaseSemaphore(ddraw->render.sem, 1, NULL);
        LeaveCriticalSection(&This->cs);

        if (This->render.thread)
        {
            WaitForSingleObject(This->render.thread, INFINITE);
            This->render.thread = NULL;
        }

        if (This->renderer == render_d3d9_main)
            Direct3D9_Release();
    }
    
    if(!ddraw->windowed)
    {
        if (!Direct3D9Active)
            ChangeDisplaySettings(&This->mode, 0);
    }

    return DD_OK;
}

void InitDirect3D9()
{
    Direct3D9Active = Direct3D9_Create();
    if (!Direct3D9Active)
    {
        Direct3D9_Release();
        ShowDriverWarning = TRUE;
        ddraw->renderer = render_soft_main;
    }
}

// LastSetWindowPosTick = Workaround for a wine+gnome bug where each SetWindowPos call triggers a WA_INACTIVE message
DWORD LastSetWindowPosTick;

HRESULT __stdcall ddraw_SetDisplayMode(IDirectDrawImpl *This, DWORD width, DWORD height, DWORD bpp)
{
    printf("DirectDraw::SetDisplayMode(This=%p, width=%d, height=%d, bpp=%d)\n", This, (unsigned int)width, (unsigned int)height, (unsigned int)bpp);

    if (bpp != 8 && bpp != 16)
        return DDERR_INVALIDMODE;

    if (This->render.thread)
    {
        EnterCriticalSection(&This->cs);
        This->render.run = FALSE;
        ReleaseSemaphore(ddraw->render.sem, 1, NULL);
        LeaveCriticalSection(&This->cs);

        WaitForSingleObject(This->render.thread, INFINITE);
        This->render.thread = NULL;
    }

    if (!This->mode.dmPelsWidth)
    {
        This->mode.dmSize = sizeof(DEVMODE);
        This->mode.dmDriverExtra = 0;

        if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &This->mode) == FALSE)
        {
            /* not expected */
            return DDERR_UNSUPPORTED;
        }

        const HANDLE hbicon = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDR_MYMENU), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        if (hbicon)
            SendMessage(This->hWnd, WM_SETICON, ICON_BIG, (LPARAM)hbicon);

        const HANDLE hsicon = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDR_MYMENU), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        if (hsicon)
            SendMessage(This->hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hsicon);
    }

    if (ddraw->altenter)
    {
        ddraw->altenter = FALSE;
        This->render.width = ddraw->width;
        This->render.height = ddraw->height;
    }
    else
    {
        This->render.width = WindowRect.right;
        This->render.height = WindowRect.bottom;
    }

    This->width = width;
    This->height = height;
    This->bpp = bpp;

    ddraw->cursor.x = width / 2;
    ddraw->cursor.y = height / 2;

    if(This->fullscreen)
    {
        This->render.width = This->mode.dmPelsWidth;
        This->render.height = This->mode.dmPelsHeight;

        if (This->windowed) //windowed-fullscreen aka borderless
        {
            This->border = FALSE;
            WindowRect.left = -32000;
            WindowRect.top = -32000;

            // prevent OpenGL from going automatically into fullscreen exclusive mode
            if (This->renderer == render_main)
                This->render.height++;

        }
    }

    if(This->render.width < This->width)
    {
        This->render.width = This->width;
    }
    if(This->render.height < This->height)
    {
        This->render.height = This->height;
    }

    This->render.run = TRUE;
    
    BOOL lockMouse = ddraw->locked;
    mouse_unlock();
	
    memset(&This->render.mode, 0, sizeof(DEVMODE));
    This->render.mode.dmSize = sizeof(DEVMODE);
    This->render.mode.dmFields = DM_PELSWIDTH|DM_PELSHEIGHT;
    This->render.mode.dmPelsWidth = This->render.width;
    This->render.mode.dmPelsHeight = This->render.height;
    if(This->render.bpp)
    {
        This->render.mode.dmFields |= DM_BITSPERPEL;
        This->render.mode.dmBitsPerPel = This->render.bpp;
    }
    
    BOOL maintas = ddraw->maintas;

    if(!This->windowed)
    {
        // Making sure the chosen resolution is valid
        if(!This->devmode)
        {
            int oldWidth = This->render.width;
            int oldHeight = This->render.height;
            
            if (ChangeDisplaySettings(&This->render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
            {
                // fail... compare resolutions
                if (This->render.width > This->mode.dmPelsWidth || This->render.height > This->mode.dmPelsHeight)
                {
                    // chosen game resolution higher than current resolution, use window mode for this case
                    This->windowed = TRUE;
                }
                else
                {
                    // Try 2x scaling
                    This->render.width *= 2;
                    This->render.height *= 2;
                    
                    This->render.mode.dmPelsWidth = This->render.width;
                    This->render.mode.dmPelsHeight = This->render.height;
                    
                    if ((This->render.width > This->mode.dmPelsWidth || This->render.height > This->mode.dmPelsHeight) ||
                        ChangeDisplaySettings(&This->render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
                    {
                        // try current display settings
                        This->render.width = This->mode.dmPelsWidth;
                        This->render.height = This->mode.dmPelsHeight;
                        
                        This->render.mode.dmPelsWidth = This->render.width;
                        This->render.mode.dmPelsHeight = This->render.height;
                        
                        if (ChangeDisplaySettings(&This->render.mode, CDS_TEST) != DISP_CHANGE_SUCCESSFUL)
                        {
                            // everything failed, use window mode instead
                            This->render.width = oldWidth;
                            This->render.height = oldHeight;
                            
                            This->render.mode.dmPelsWidth = This->render.width;
                            This->render.mode.dmPelsHeight = This->render.height;
                            
                            This->windowed = TRUE;
                        }
                        else
                            maintas = TRUE;
                    }
                }
            }
        }
    }

    if (!ddraw->handlemouse)
        This->boxing = maintas = FALSE;
    
    This->render.viewport.width = This->render.width;
    This->render.viewport.height = This->render.height;
    This->render.viewport.x = 0;
    This->render.viewport.y = 0;
    
    if (This->boxing)
    {
        This->render.viewport.width = This->width;
        This->render.viewport.height = This->height;

        int i;
        for (i = 20; i-- > 1;)
        {
            if (This->width * i <= This->render.width && This->height * i <= This->render.height)
            {
                This->render.viewport.width *= i;
                This->render.viewport.height *= i;
                break;
            }
        }

        This->render.viewport.y = This->render.height / 2 - This->render.viewport.height / 2;
        This->render.viewport.x = This->render.width / 2 - This->render.viewport.width / 2;
    }
    else if (maintas)
    {
        This->render.viewport.width = This->render.width;
        This->render.viewport.height = ((float)This->height / This->width) * This->render.viewport.width;
        
        if (This->render.viewport.height > This->render.height)
        {
            This->render.viewport.width = 
                ((float)This->render.viewport.width / This->render.viewport.height) * This->render.height;
                
            This->render.viewport.height = This->render.height;
        }
         
        This->render.viewport.y = This->render.height / 2 - This->render.viewport.height / 2;
        This->render.viewport.x = This->render.width / 2 - This->render.viewport.width / 2;
    }
    
    This->render.scaleW = ((float)This->render.viewport.width / This->width);
    This->render.scaleH = ((float)This->render.viewport.height / This->height);
    This->render.unScaleW = ((float)This->width / This->render.viewport.width);
    This->render.unScaleH = ((float)This->height / This->render.viewport.height);

    if (This->windowed)
    {
        if (!This->border)
        {
            SetWindowLong(This->hWnd, GWL_STYLE, GetWindowLong(This->hWnd, GWL_STYLE) & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU));
        }
        else
        {
            SetWindowLong(This->hWnd, GWL_STYLE, GetWindowLong(This->hWnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
        }

        if (ddraw->wine)
            SetWindowLong(This->hWnd, GWL_STYLE, (GetWindowLong(This->hWnd, GWL_STYLE) | WS_MINIMIZEBOX) & ~(WS_MAXIMIZEBOX | WS_THICKFRAME));

        /* center the window with correct dimensions */
        int x = (WindowRect.left != -32000) ? WindowRect.left : (This->mode.dmPelsWidth / 2) - (This->render.width / 2);
        int y = (WindowRect.top != -32000) ? WindowRect.top : (This->mode.dmPelsHeight / 2) - (This->render.height / 2);
        RECT dst = { x, y, This->render.width + x, This->render.height + y };
        AdjustWindowRect(&dst, GetWindowLong(This->hWnd, GWL_STYLE), FALSE);
        SetWindowPos(ddraw->hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        MoveWindow(This->hWnd, dst.left, dst.top, (dst.right - dst.left), (dst.bottom - dst.top), TRUE);

        if (This->renderer == render_d3d9_main)
            InitDirect3D9();

        if (lockMouse)
            mouse_lock();
    }
    else
    {
        if (This->renderer == render_d3d9_main)
            InitDirect3D9();

        if(!This->devmode)
        {
            if (!Direct3D9Active && ChangeDisplaySettings(&This->render.mode, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
            {
                This->render.run = FALSE;
                return DDERR_INVALIDMODE;
            }
        }

        if (ddraw->wine)
            SetWindowLong(This->hWnd, GWL_STYLE, GetWindowLong(This->hWnd, GWL_STYLE) | WS_MINIMIZEBOX);

        SetWindowPos(This->hWnd, HWND_TOPMOST, 0, 0, This->render.width, This->render.height, SWP_SHOWWINDOW);
        LastSetWindowPosTick = timeGetTime();

        mouse_lock();
    }
    
    if(This->render.viewport.x != 0 || This->render.viewport.y != 0)
    {
        RedrawWindow(This->hWnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);
    }

    InterlockedExchange(&ddraw->minimized, FALSE);
    
    if(This->render.thread == NULL)
    {
        InterlockedExchange(&ddraw->render.paletteUpdated, TRUE);
        InterlockedExchange(&ddraw->render.surfaceUpdated, TRUE);
        ReleaseSemaphore(ddraw->render.sem, 1, NULL);

        This->render.thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)This->renderer, NULL, 0, NULL);
    }

    return DD_OK;
}

HRESULT __stdcall ddraw_SetDisplayMode2(IDirectDrawImpl *This, DWORD width, DWORD height, DWORD bpp, DWORD refreshRate, DWORD flags)
{
    return ddraw_SetDisplayMode(This, width, height, bpp);
}

void ToggleFullscreen()
{
    if (ddraw->windowed)
    {
        mouse_unlock();
        WindowState = ddraw->windowed = FALSE;
        SetWindowLong(ddraw->hWnd, GWL_STYLE, GetWindowLong(ddraw->hWnd, GWL_STYLE) & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU));
        ddraw->altenter = TRUE;
        ddraw_SetDisplayMode(ddraw, ddraw->width, ddraw->height, ddraw->bpp);
        mouse_lock();
    }
    else
    {
        mouse_unlock();
        WindowState = ddraw->windowed = TRUE;

        if (Direct3D9Active)
            Direct3D9_Reset();
        else
            ChangeDisplaySettings(&ddraw->mode, 0);

        ddraw_SetDisplayMode(ddraw, ddraw->width, ddraw->height, ddraw->bpp);
        mouse_lock();
    }
}

BOOL UnadjustWindowRectEx(LPRECT prc, DWORD dwStyle, BOOL fMenu, DWORD dwExStyle)
{
    RECT rc;
    SetRectEmpty(&rc);

    BOOL fRc = AdjustWindowRectEx(&rc, dwStyle, fMenu, dwExStyle);
    if (fRc)
    {
        prc->left -= rc.left;
        prc->top -= rc.top;
        prc->right -= rc.right;
        prc->bottom -= rc.bottom;
    }

    return fRc;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT rc = { 0, 0, ddraw->render.width, ddraw->render.height };
    static BOOL inSizeMove = FALSE;
    static int redrawCount = 0;
    
    switch(uMsg)
    {
        case WM_D3D9DEVICELOST:
        {
            if (Direct3D9Active && Direct3D9_OnDeviceLost())
            {
                if (!ddraw->windowed)
                    mouse_lock();
            }
            return 0;
        }
        case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS *pos = (WINDOWPOS *)lParam;

            if (ddraw->wine && !ddraw->windowed && (pos->x > 0 || pos->y > 0) && LastSetWindowPosTick + 500 < timeGetTime())
                PostMessage(ddraw->hWnd, WM_WINEFULLSCREEN, 0, 0);

            break;
        }
        case WM_WINEFULLSCREEN:
        {
            if (!ddraw->windowed)
            {
                LastSetWindowPosTick = timeGetTime();
                SetWindowPos(ddraw->hWnd, HWND_TOPMOST, 1, 1, ddraw->render.width, ddraw->render.height, SWP_SHOWWINDOW);
                SetWindowPos(ddraw->hWnd, HWND_TOPMOST, 0, 0, ddraw->render.width, ddraw->render.height, SWP_SHOWWINDOW);
            }
            return 0;
        }
        case WM_ENTERSIZEMOVE:
        {
            if (ddraw->windowed)
            {
                inSizeMove = TRUE;
            }
            break;
        }
        case WM_EXITSIZEMOVE:
        {
            if (ddraw->windowed)
            {
                inSizeMove = FALSE;

                if (!ddraw->render.thread)
                    ddraw_SetDisplayMode(ddraw, ddraw->width, ddraw->height, ddraw->bpp);
            }
            break;
        }
        case WM_SIZING:
        {
            RECT *windowrc = (RECT *)lParam;

            if (ddraw->windowed)
            {
                if (inSizeMove)
                {
                    if (ddraw->render.thread)
                    {
                        EnterCriticalSection(&ddraw->cs);
                        ddraw->render.run = FALSE;
                        ReleaseSemaphore(ddraw->render.sem, 1, NULL);
                        LeaveCriticalSection(&ddraw->cs);

                        WaitForSingleObject(ddraw->render.thread, INFINITE);
                        ddraw->render.thread = NULL;
                    }

                    RECT clientrc = { 0 };

                    // maintain aspect ratio
                    if (ddraw->maintas && 
                        CopyRect(&clientrc, windowrc) &&
                        UnadjustWindowRectEx(&clientrc, GetWindowLong(hWnd, GWL_STYLE), FALSE, GetWindowLong(hWnd, GWL_EXSTYLE)) &&
                        SetRect(&clientrc, 0, 0, clientrc.right - clientrc.left, clientrc.bottom - clientrc.top))
                    {
                        float scaleH = (float)ddraw->height / ddraw->width;
                        float scaleW = (float)ddraw->width / ddraw->height;

                        switch (wParam)
                        {
                            case WMSZ_BOTTOMLEFT:
                            case WMSZ_BOTTOMRIGHT:
                            case WMSZ_LEFT:
                            case WMSZ_RIGHT:
                            {
                                windowrc->bottom += scaleH * clientrc.right - clientrc.bottom;
                                break;
                            }
                            case WMSZ_TOP:
                            case WMSZ_BOTTOM:
                            {
                                windowrc->right += scaleW * clientrc.bottom - clientrc.right;
                                break;
                            }
                            case WMSZ_TOPRIGHT:
                            case WMSZ_TOPLEFT:
                            {
                                windowrc->top -= scaleH * clientrc.right - clientrc.bottom;
                                break;
                            }
                        }
                    }

                    //enforce minimum window size
                    if (CopyRect(&clientrc, windowrc) &&
                        UnadjustWindowRectEx(&clientrc, GetWindowLong(hWnd, GWL_STYLE), FALSE, GetWindowLong(hWnd, GWL_EXSTYLE)) &&
                        SetRect(&clientrc, 0, 0, clientrc.right - clientrc.left, clientrc.bottom - clientrc.top))
                    {
                        if (clientrc.right < ddraw->width)
                        {
                            switch (wParam)
                            {
                                case WMSZ_TOPRIGHT:
                                case WMSZ_BOTTOMRIGHT:
                                case WMSZ_RIGHT:
                                case WMSZ_BOTTOM:
                                case WMSZ_TOP:
                                {
                                    windowrc->right += ddraw->width - clientrc.right; 
                                    break;
                                }
                                case WMSZ_TOPLEFT:
                                case WMSZ_BOTTOMLEFT:
                                case WMSZ_LEFT:
                                {
                                    windowrc->left -= ddraw->width - clientrc.right; 
                                    break;
                                }
                            }
                        }
                            
                        if (clientrc.bottom < ddraw->height)
                        {
                            switch (wParam)
                            {
                                case WMSZ_BOTTOMLEFT:
                                case WMSZ_BOTTOMRIGHT:
                                case WMSZ_BOTTOM:
                                case WMSZ_RIGHT:
                                case WMSZ_LEFT:
                                {
                                    windowrc->bottom += ddraw->height - clientrc.bottom; 
                                    break;
                                }
                                case WMSZ_TOPLEFT:
                                case WMSZ_TOPRIGHT:
                                case WMSZ_TOP:
                                {
                                    windowrc->top -= ddraw->height - clientrc.bottom; 
                                    break;
                                }
                            }
                        }
                    }

                    //save new window position
                    if (CopyRect(&clientrc, windowrc) &&
                        UnadjustWindowRectEx(&clientrc, GetWindowLong(hWnd, GWL_STYLE), FALSE, GetWindowLong(hWnd, GWL_EXSTYLE)))
                    {
                        WindowRect.left = clientrc.left;
                        WindowRect.top = clientrc.top;
                        WindowRect.right = clientrc.right - clientrc.left;
                        WindowRect.bottom = clientrc.bottom - clientrc.top;
                    }

                    return TRUE;
                }
            }
            break;
        }
        case WM_SIZE: 
        {
            if (ddraw->windowed)
            {
                if (wParam == SIZE_RESTORED)
                {
                    if (inSizeMove && !ddraw->render.thread)
                    {
                        WindowRect.right = LOWORD(lParam);
                        WindowRect.bottom = HIWORD(lParam);
                    }
                    /*
                    else if (ddraw->wine)
                    {
                        WindowRect.right = LOWORD(lParam);
                        WindowRect.bottom = HIWORD(lParam);
                        if (WindowRect.right != ddraw->render.width || WindowRect.bottom != ddraw->render.height)
                            ddraw_SetDisplayMode(ddraw, ddraw->width, ddraw->height, ddraw->bpp);
                    }
                    */
                }
            }
            return DefWindowProc(hWnd, uMsg, wParam, lParam); /* Carmageddon fix */
        }
        case WM_MOVING:
        {
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
        }
        case WM_MOVE:
        {
            if (ddraw->windowed)
            {
                if (inSizeMove || ddraw->wine)
                {
                    int x = (int)(short)LOWORD(lParam);
                    int y = (int)(short)HIWORD(lParam);

                    if (x != -32000)
                        WindowRect.left = x; // -32000 = Exit/Minimize

                    if (y != -32000)
                        WindowRect.top = y;
                }
            }
            
            if (!ddraw->handlemouse)
                RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);

            return DefWindowProc(hWnd, uMsg, wParam, lParam); /* Carmageddon fix */
        }

        /* C&C and RA really don't want to close down */
        case WM_SYSCOMMAND:
            if (wParam == SC_CLOSE && !GameHandlesClose)
            {
                exit(0);
            }
            if (wParam == SC_MAXIMIZE)
            {
                ToggleFullscreen();
                return 0;
            }
                
            return DefWindowProc(hWnd, uMsg, wParam, lParam);

        case WM_ACTIVATE:
            if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE)
            {
                if (!ddraw->windowed)
                {
                    if (!Direct3D9Active)
                    {
                        ChangeDisplaySettings(&ddraw->render.mode, CDS_FULLSCREEN);

                        if (wParam == WA_ACTIVE)
                        {
                            mouse_lock();
                        }
                    }

                    InterlockedExchange(&ddraw->minimized, FALSE);
                }

                if (!ddraw->handlemouse)
                    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            else if (wParam == WA_INACTIVE)
            {
                if (!ddraw->windowed && !ddraw->locked && ddraw->noactivateapp)
                    return 0;

                mouse_unlock();

                if (ddraw->wine && LastSetWindowPosTick + 500 > timeGetTime())
                    return 0;

                /* minimize our window on defocus when in fullscreen */
                if (!ddraw->windowed)
                {
                    if (!Direct3D9Active)
                    {
                        ShowWindow(ddraw->hWnd, SW_MINIMIZE);
                        ChangeDisplaySettings(&ddraw->mode, 0);
                    }

                    InterlockedExchange(&ddraw->minimized, TRUE);
                }
            }
            return 0;

        //workaround for a bug where sometimes a background window steals the focus
        case WM_WINDOWPOSCHANGING:
        {
            if (ddraw->locked)
            {
                WINDOWPOS *pos = (WINDOWPOS *)lParam;
                
                if (pos->flags == SWP_NOMOVE + SWP_NOSIZE)
                {
                    mouse_unlock();
                    
                    if (GetForegroundWindow() == ddraw->hWnd)
                        mouse_lock();
                }
            }
            break;
        }

        case WM_MOUSELEAVE:
            mouse_unlock();
            return 0;

        case WM_ACTIVATEAPP:
            /* C&C and RA stop drawing when they receive this with FALSE wParam, disable in windowed mode */
            if (ddraw->windowed || ddraw->noactivateapp)
            {
                // let it pass through once (tiberian sun)
                static BOOL oneTime;
                if (wParam && !oneTime && !ddraw->handlemouse && ddraw->noactivateapp)
                {
                    oneTime = TRUE;
                    break;
                }

                return 0;
            }
            break;
        case WM_AUTORENDERER:
        {
            mouse_unlock();
            SetWindowPos(ddraw->hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetWindowPos(ddraw->hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            if (!ddraw->wine)
            {
                ShowWindow(ddraw->hWnd, SW_MINIMIZE);
                ShowWindow(ddraw->hWnd, SW_RESTORE);
            }
            mouse_lock();
            return 0;
        }
        case WM_NCLBUTTONDBLCLK:
        {
            ToggleFullscreen();
            return 0;
        }
        case WM_SYSKEYDOWN:
        {
            if (wParam == VK_RETURN)
            {
                ToggleFullscreen();
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
            if(wParam == VK_CONTROL || wParam == VK_TAB)
            {
                if(GetAsyncKeyState(VK_CONTROL) & 0x8000 && GetAsyncKeyState(VK_TAB) & 0x8000)
                {
                    mouse_unlock();
                    return 0;
                }
            }
            break;

        case WM_KEYUP:
            if (wParam == VK_SNAPSHOT)
                screenshot(ddraw->primary);

            break;
            

        /* button up messages reactivate cursor lock */
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            if (!ddraw->devmode && !ddraw->locked)
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                
                if (x > ddraw->render.viewport.x + ddraw->render.viewport.width || 
                    x < ddraw->render.viewport.x || 
                    y > ddraw->render.viewport.y + ddraw->render.viewport.height ||
                    y < ddraw->render.viewport.y)
                {
                    ddraw->cursor.x = ddraw->width / 2;
                    ddraw->cursor.y = ddraw->height / 2;
                }    
                else
                {
                    ddraw->cursor.x = (x - ddraw->render.viewport.x) * ddraw->render.unScaleW;
                    ddraw->cursor.y = (y - ddraw->render.viewport.y) * ddraw->render.unScaleH;
                }

                mouse_lock();
                return 0;
            }
            /* fall through for lParam */

        /* down messages are ignored if we have no cursor lock */
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_MOUSEMOVE:

            if (!ddraw->devmode)
            {
                if (!ddraw->locked)
                {
                    return 0;
                }
                
                if(ddraw->adjmouse)
                {
                    fake_GetCursorPos(NULL); /* update our own cursor */
                    lParam = MAKELPARAM(ddraw->cursor.x, ddraw->cursor.y);
                }
            }

            if (ddraw->devmode)
            {
                mouse_lock();
                ddraw->cursor.x = GET_X_LPARAM(lParam);
                ddraw->cursor.y = GET_Y_LPARAM(lParam);
            }
            break;

        //Workaround for invisible menu on Load/Save/Delete in Tiberian Sun
        case WM_PARENTNOTIFY:
        {
            if (!ddraw->handlemouse && LOWORD(wParam) == WM_DESTROY)
                redrawCount = 2;
            break;
        }
        case WM_PAINT:
        {
            if (!ddraw->handlemouse && redrawCount > 0)
            {
                redrawCount--;
                RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }

            EnterCriticalSection(&ddraw->cs);
            ReleaseSemaphore(ddraw->render.sem, 1, NULL);
            LeaveCriticalSection(&ddraw->cs);
            break;
        }
        case WM_ERASEBKGND:
        {
            EnterCriticalSection(&ddraw->cs);
            FillRect(ddraw->render.hDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            ReleaseSemaphore(ddraw->render.sem, 1, NULL);
            LeaveCriticalSection(&ddraw->cs);
            break;
        }
    }

    return ddraw->WndProc(hWnd, uMsg, wParam, lParam);
}

HRESULT __stdcall ddraw_SetCooperativeLevel(IDirectDrawImpl *This, HWND hWnd, DWORD dwFlags)
{
    PIXELFORMATDESCRIPTOR pfd;

    printf("DirectDraw::SetCooperativeLevel(This=%p, hWnd=0x%08X, dwFlags=0x%08X)\n", This, (unsigned int)hWnd, (unsigned int)dwFlags);

    /* Red Alert for some weird reason does this on Windows XP */
    if(hWnd == NULL)
    {
        return DD_OK;
    }

    if (This->hWnd == NULL)
    {
        This->hWnd = hWnd;
    }

    if (!This->WndProc)
    {
        mouse_init();

        This->WndProc = (LRESULT(CALLBACK *)(HWND, UINT, WPARAM, LPARAM))GetWindowLong(hWnd, GWL_WNDPROC);

        SetWindowLong(This->hWnd, GWL_WNDPROC, (LONG)WndProc);

        if (!This->render.hDC)
        {
            This->render.hDC = GetDC(This->hWnd);

            memset(&pfd, 0, sizeof(PIXELFORMATDESCRIPTOR));
            pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER | (This->renderer == render_main ? PFD_SUPPORT_OPENGL : 0);
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = ddraw->render.bpp ? ddraw->render.bpp : ddraw->mode.dmBitsPerPel;
            pfd.iLayerType = PFD_MAIN_PLANE;
            SetPixelFormat(This->render.hDC, ChoosePixelFormat(This->render.hDC, &pfd), &pfd);
        }

        SetCursor(LoadCursor(NULL, IDC_ARROW));

        GetWindowText(This->hWnd, (LPTSTR)&This->title, sizeof(This->title));

        ddraw->isredalert = strcmp(This->title, "Red Alert") == 0;
        ddraw->iscnc1 = strcmp(This->title, "Command & Conquer") == 0;

        if (This->vhack && !ddraw->isredalert && !ddraw->iscnc1)
            This->vhack = 0;
    }

    return DD_OK;
}

HRESULT __stdcall ddraw_WaitForVerticalBlank(IDirectDrawImpl *This, DWORD a, HANDLE b)
{
#if _DEBUG_X
    printf("DirectDraw::WaitForVerticalBlank(This=%p, ...) ???\n", This);
#endif
    return DD_OK;
}

ULONG __stdcall ddraw_AddRef(IDirectDrawImpl *This)
{
    printf("DirectDraw::AddRef(This=%p)\n", This);

    This->Ref++;

    return This->Ref;
}

HRESULT __stdcall ddraw_QueryInterface(IDirectDrawImpl *This, REFIID riid, void **obj)
{
    printf("DirectDraw::QueryInterface(This=%p, riid=%08X, obj=%p)\n", This, (unsigned int)riid, obj);

    if (riid && !IsEqualGUID(&IID_IDirectDraw, riid))
    {
        printf("  IID_IDirectDrawX\n");

        ddraw_AddRef(This);
        This->lpVtbl->SetDisplayMode2 = ddraw_SetDisplayMode2;
    }

    *obj = This;

    return S_OK;
}

ULONG __stdcall ddraw_Release(IDirectDrawImpl *This)
{
    printf("DirectDraw::Release(This=%p)\n", This);

    This->Ref--;

    if(This->Ref == 0)
    {
        if(This->render.run)
        {
            EnterCriticalSection(&This->cs);
            This->render.run = FALSE;
            ReleaseSemaphore(ddraw->render.sem, 1, NULL);
            LeaveCriticalSection(&This->cs);

            if (This->render.thread)
            {
                WaitForSingleObject(This->render.thread, INFINITE);
                This->render.thread = NULL;
            }

            if (This->renderer == render_d3d9_main)
                Direct3D9_Release();
        }

        if(This->render.hDC)
        {
            ReleaseDC(This->hWnd, This->render.hDC);
            This->render.hDC = NULL;
        }
        
        if(This->render.ev)
        {
            CloseHandle(This->render.ev);
            ddraw->render.ev = NULL;
        }

        DeleteCriticalSection(&This->cs);

        /* restore old wndproc, subsequent ddraw creation will otherwise fail */
        SetWindowLong(This->hWnd, GWL_WNDPROC, (LONG)This->WndProc);
        HeapFree(GetProcessHeap(), 0, This);
        ddraw = NULL;
        return 0;
    }

    return This->Ref;
}

struct IDirectDrawImplVtbl iface =
{
    /* IUnknown */
    ddraw_QueryInterface,
    ddraw_AddRef,
    ddraw_Release,
    /* IDirectDrawImpl */
    ddraw_Compact,
    ddraw_CreateClipper,
    ddraw_CreatePalette,
    ddraw_CreateSurface,
    ddraw_DuplicateSurface,
    ddraw_EnumDisplayModes,
    ddraw_EnumSurfaces,
    ddraw_FlipToGDISurface,
    ddraw_GetCaps,
    ddraw_GetDisplayMode,
    ddraw_GetFourCCCodes,
    ddraw_GetGDISurface,
    ddraw_GetMonitorFrequency,
    ddraw_GetScanLine,
    ddraw_GetVerticalBlankStatus,
    ddraw_Initialize,
    ddraw_RestoreDisplayMode,
    ddraw_SetCooperativeLevel,
    ddraw_SetDisplayMode,
    ddraw_WaitForVerticalBlank
};

HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACK lpCallback, LPVOID lpContext)
{
    printf("DirectDrawEnumerateA(lpCallback=%p, lpContext=%p) ???\n", lpCallback, lpContext);
    /*
    HMODULE hddraw = LoadLibrary("system32\\ddraw.dll");

    if (hddraw)
    {
        HRESULT(WINAPI *DDrawEnumerateA)(LPDDENUMCALLBACK, LPVOID) = 
            (HRESULT(WINAPI *)(LPDDENUMCALLBACK, LPVOID))GetProcAddress(hddraw, "DirectDrawEnumerateA");

        if (DDrawEnumerateA)
            return DDrawEnumerateA(lpCallback, lpContext);

        FreeLibrary(hddraw);
    }
    */

    if (lpCallback)
        lpCallback(NULL, "display", "(null)", lpContext);

    return DD_OK;
}

int stdout_open = 0;
HRESULT WINAPI DirectDrawCreate(GUID FAR* lpGUID, LPDIRECTDRAW FAR* lplpDD, IUnknown FAR* pUnkOuter) 
{
#if _DEBUG
    if(!stdout_open)
    {
        freopen("ra95stdout.txt", "w", stdout);
        setvbuf(stdout, NULL, _IOLBF, 1024);
        stdout_open = 1;
    }
#endif

    printf("DirectDrawCreate(lpGUID=%p, lplpDD=%p, pUnkOuter=%p)\n", lpGUID, lplpDD, pUnkOuter);

    if(ddraw)
    {
        /* FIXME: check the calling module before passing the call! */
        if (ddraw->DirectDrawCreate)
            return ddraw->DirectDrawCreate(lpGUID, lplpDD, pUnkOuter);

        return DDERR_DIRECTDRAWALREADYCREATED;
    } 

    IDirectDrawImpl *This = (IDirectDrawImpl *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IDirectDrawImpl));
    This->lpVtbl = &iface;
    printf(" This = %p\n", This);
    *lplpDD = (LPDIRECTDRAW)This;
    This->Ref = 0;
    ddraw_AddRef(This);

    ddraw = This;

    if (!This->real_dll)
        This->real_dll = LoadLibrary("system32\\ddraw.dll");

    if(This->real_dll && !This->DirectDrawCreate)
    {
        This->DirectDrawCreate =
            (HRESULT(WINAPI *)(GUID FAR*, LPDIRECTDRAW FAR*, IUnknown FAR*))GetProcAddress(This->real_dll, "DirectDrawCreate");
    }

    InitializeCriticalSection(&This->cs);
    This->render.ev = CreateEvent(NULL, TRUE, FALSE, NULL);
    This->render.sem = CreateSemaphore(NULL, 0, 1, NULL);

    This->wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != 0;

    Settings_Load();

    return DD_OK;
}
