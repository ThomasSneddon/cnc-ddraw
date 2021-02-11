#include <windows.h>
#include <stdio.h>
#include <d3d9.h>
#include "main.h"
#include "surface.h"
#include "d3d9shader.h"
#include "render_d3d9.h"

#define TEXTURE_COUNT 2

HMODULE Direct3D9_hModule;

static D3DPRESENT_PARAMETERS D3dpp;
static LPDIRECT3D9 D3d;
static LPDIRECT3DDEVICE9 D3dDev;
static LPDIRECT3DVERTEXBUFFER9 VertexBuf;
static IDirect3DTexture9 *SurfaceTex[TEXTURE_COUNT];
static IDirect3DTexture9 *PaletteTex[TEXTURE_COUNT];
static IDirect3DPixelShader9 *PixelShader;
static float ScaleW;
static float ScaleH;
static int BitsPerPixel;

static BOOL CreateResources();
static BOOL SetStates();
static BOOL UpdateVertices(BOOL inCutscene, BOOL stretch);
static void SetMaxFPS();

BOOL Direct3D9_Create()
{
    if (!Direct3D9_Release())
        return FALSE;

    if (!Direct3D9_hModule)
        Direct3D9_hModule = LoadLibrary(d3d9_module_name);

    if (Direct3D9_hModule)
    {
        IDirect3D9 *(WINAPI *D3DCreate9)(UINT) =
            (IDirect3D9 *(WINAPI *)(UINT))GetProcAddress(Direct3D9_hModule, "Direct3DCreate9");

        if (D3DCreate9 && (D3d = D3DCreate9(D3D_SDK_VERSION)))
        {
            BitsPerPixel = ddraw->render.bpp ? ddraw->render.bpp : ddraw->mode.dmBitsPerPel;

            D3dpp.Windowed = ddraw->windowed;
            D3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            D3dpp.hDeviceWindow = ddraw->hWnd;
            D3dpp.PresentationInterval = ddraw->vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
            D3dpp.BackBufferWidth = D3dpp.Windowed ? 0 : ddraw->render.width;
            D3dpp.BackBufferHeight = D3dpp.Windowed ? 0 : ddraw->render.height;
            D3dpp.BackBufferFormat = BitsPerPixel == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8;
            D3dpp.BackBufferCount = 1;

            DWORD behaviorFlags[] = {
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
                D3DCREATE_HARDWARE_VERTEXPROCESSING,
                D3DCREATE_MIXED_VERTEXPROCESSING,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            };

            int i;
            for (i = 0; i < sizeof(behaviorFlags) / sizeof(behaviorFlags[0]); i++)
            {
                if (SUCCEEDED(
                    IDirect3D9_CreateDevice(
                        D3d,
                        D3DADAPTER_DEFAULT,
                        D3DDEVTYPE_HAL,
                        ddraw->hWnd,
                        D3DCREATE_MULTITHREADED | behaviorFlags[i],
                        &D3dpp,
                        &D3dDev)))
                    return D3dDev && CreateResources() && SetStates();
            }
        }
    }

    return FALSE;
}

BOOL Direct3D9_OnDeviceLost()
{
    if (D3dDev && IDirect3DDevice9_TestCooperativeLevel(D3dDev) == D3DERR_DEVICENOTRESET)
        return Direct3D9_Reset();

    return FALSE;
}

BOOL Direct3D9_Reset()
{
    D3dpp.Windowed = ddraw->windowed;
    D3dpp.BackBufferWidth = D3dpp.Windowed ? 0 : ddraw->render.width;
    D3dpp.BackBufferHeight = D3dpp.Windowed ? 0 : ddraw->render.height;
    D3dpp.BackBufferFormat = BitsPerPixel == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8;

    if (D3dDev && SUCCEEDED(IDirect3DDevice9_Reset(D3dDev, &D3dpp)))
        return SetStates();

    return FALSE;
}

BOOL Direct3D9_Release()
{
    if (VertexBuf)
    {
        IDirect3DVertexBuffer9_Release(VertexBuf);
        VertexBuf = NULL;
    }
    
    int i;
    for (i = 0; i < TEXTURE_COUNT; i++)
    {
        if (SurfaceTex[i])
        {
            IDirect3DTexture9_Release(SurfaceTex[i]);
            SurfaceTex[i] = NULL;
        }

        if (PaletteTex[i])
        {
            IDirect3DTexture9_Release(PaletteTex[i]);
            PaletteTex[i] = NULL;
        }
    }
    
    if (PixelShader)
    {
        IDirect3DPixelShader9_Release(PixelShader);
        PixelShader = NULL;
    }

    if (D3dDev)
    {
        IDirect3DDevice9_Release(D3dDev);
        D3dDev = NULL;
    }

    if (D3d)
    {
        IDirect3D9_Release(D3d);
        D3d = NULL;
    }

    return TRUE;
}

static BOOL CreateResources()
{
    BOOL err = FALSE;

    int width = ddraw->width;
    int height = ddraw->height;

    int texWidth =
        width <= 1024 ? 1024 : width <= 2048 ? 2048 : width <= 4096 ? 4096 : width;

    int texHeight =
        height <= texWidth ? texWidth : height <= 2048 ? 2048 : height <= 4096 ? 4096 : height;

    texWidth = texWidth > texHeight ? texWidth : texHeight;

    ScaleW = (float)width / texWidth;;
    ScaleH = (float)height / texHeight;

    err = err || FAILED(
        IDirect3DDevice9_CreateVertexBuffer(
            D3dDev, sizeof(CUSTOMVERTEX) * 4, 0, D3DFVF_XYZRHW | D3DFVF_TEX1, D3DPOOL_MANAGED, &VertexBuf, NULL));

    err = err || !UpdateVertices(InterlockedExchangeAdd(&ddraw->incutscene, 0), TRUE);

    int i;
    for (i = 0; i < TEXTURE_COUNT; i++)
    {
        err = err || FAILED(
            IDirect3DDevice9_CreateTexture(
                D3dDev,
                texWidth,
                texHeight,
                1,
                0,
                ddraw->bpp == 16 ? D3DFMT_R5G6B5 : D3DFMT_L8,
                D3DPOOL_MANAGED,
                &SurfaceTex[i],
                0));

        err = err || !SurfaceTex[i];

        if (ddraw->bpp == 8)
        {
            err = err || FAILED(
                IDirect3DDevice9_CreateTexture(
                    D3dDev,
                    256,
                    256,
                    1,
                    0,
                    D3DFMT_X8R8G8B8,
                    D3DPOOL_MANAGED,
                    &PaletteTex[i],
                    0));

            err = err || !PaletteTex[i];
        }
    }

    if (ddraw->bpp == 8)
    {
        err = err || FAILED(
            IDirect3DDevice9_CreatePixelShader(D3dDev, (DWORD *)PalettePixelShaderSrc, &PixelShader));
    }

    return VertexBuf && (PixelShader || ddraw->bpp == 16) && !err;
}

static BOOL SetStates()
{
    BOOL err = FALSE;

    err = err || FAILED(IDirect3DDevice9_SetFVF(D3dDev, D3DFVF_XYZRHW | D3DFVF_TEX1));
    err = err || FAILED(IDirect3DDevice9_SetStreamSource(D3dDev, 0, VertexBuf, 0, sizeof(CUSTOMVERTEX)));
    err = err || FAILED(IDirect3DDevice9_SetTexture(D3dDev, 0, (IDirect3DBaseTexture9 *)SurfaceTex[0]));

    if (ddraw->bpp == 8)
    {
        err = err || FAILED(IDirect3DDevice9_SetTexture(D3dDev, 1, (IDirect3DBaseTexture9 *)PaletteTex[0]));
        err = err || FAILED(IDirect3DDevice9_SetPixelShader(D3dDev, PixelShader));
    }

    D3DVIEWPORT9 viewData = {
        ddraw->render.viewport.x,
        ddraw->render.viewport.y,
        ddraw->render.viewport.width,
        ddraw->render.viewport.height,
        0.0f,
        1.0f };

    err = err || FAILED(IDirect3DDevice9_SetViewport(D3dDev, &viewData));

    return !err;
}

static BOOL UpdateVertices(BOOL inCutscene, BOOL stretch)
{
    float vpX = stretch ? (float)ddraw->render.viewport.x : 0.0f;
    float vpY = stretch ? (float)ddraw->render.viewport.y : 0.0f;

    float vpW = stretch ? (float)(ddraw->render.viewport.width + ddraw->render.viewport.x) : (float)ddraw->width;
    float vpH = stretch ? (float)(ddraw->render.viewport.height + ddraw->render.viewport.y) : (float)ddraw->height;

    float sH = inCutscene ? ScaleH * ((float)CUTSCENE_HEIGHT / ddraw->height) : ScaleH;
    float sW = inCutscene ? ScaleW * ((float)CUTSCENE_WIDTH / ddraw->width) : ScaleW;

    CUSTOMVERTEX vertices[] =
    {
        { vpX - 0.5f, vpH - 0.5f, 0.0f, 1.0f, 0.0f, sH },
        { vpX - 0.5f, vpY - 0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
        { vpW - 0.5f, vpH - 0.5f, 0.0f, 1.0f, sW,   sH },
        { vpW - 0.5f, vpY - 0.5f, 0.0f, 1.0f, sW,   0.0f }
    };

    void *data;
    if (VertexBuf && SUCCEEDED(IDirect3DVertexBuffer9_Lock(VertexBuf, 0, 0, (void**)&data, 0)))
    {
        memcpy(data, vertices, sizeof(vertices));

        IDirect3DVertexBuffer9_Unlock(VertexBuf);
        return TRUE;
    }

    return FALSE;
}

static void SetMaxFPS()
{
    int maxFPS = ddraw->render.maxfps;
    ddraw->fpsLimiter.tickLengthNs = 0;
    ddraw->fpsLimiter.ticklength = 0;

    if (maxFPS < 0 || ddraw->vsync)
        maxFPS = ddraw->mode.dmDisplayFrequency;

    if (maxFPS > 1000)
        maxFPS = 0;

    if (maxFPS > 0)
    {
        float len = 1000.0f / maxFPS;
        ddraw->fpsLimiter.tickLengthNs = len * 10000;
        ddraw->fpsLimiter.ticklength = len;// + 0.5f;
    }
}

DWORD WINAPI render_d3d9_main(void)
{
    Sleep(500);

    SetMaxFPS();

    DWORD tickStart = 0;
    DWORD tickEnd = 0;
    BOOL needsUpdate = FALSE;

    while (ddraw->render.run && 
        (ddraw->render.forcefps || WaitForSingleObject(ddraw->render.sem, 200) != WAIT_FAILED))
    {
#if _DEBUG
        DrawFrameInfoStart();
#endif

        static int texIndex = 0, palIndex = 0;

        if (ddraw->fpsLimiter.ticklength > 0)
            tickStart = timeGetTime();

        EnterCriticalSection(&ddraw->cs);

        if (ddraw->primary && (ddraw->bpp == 16 || (ddraw->primary->palette && ddraw->primary->palette->data_rgb)))
        {
            if (ddraw->vhack)
            {
                if (detect_cutscene())
                {
                    if (!InterlockedExchange(&ddraw->incutscene, TRUE))
                        UpdateVertices(TRUE, TRUE);
                }
                else
                {
                    if (InterlockedExchange(&ddraw->incutscene, FALSE))
                        UpdateVertices(FALSE, TRUE);
                }
            }

            D3DLOCKED_RECT lock_rc;

            if (InterlockedExchange(&ddraw->render.surfaceUpdated, FALSE))
            {
                if (++texIndex >= TEXTURE_COUNT)
                    texIndex = 0;

                RECT rc = { 0,0,ddraw->width,ddraw->height };

                if (SUCCEEDED(IDirect3DDevice9_SetTexture(D3dDev, 0, (IDirect3DBaseTexture9 *)SurfaceTex[texIndex])) &&
                    SUCCEEDED(IDirect3DTexture9_LockRect(SurfaceTex[texIndex], 0, &lock_rc, &rc, 0)))
                {
                    unsigned char *src = (unsigned char *)ddraw->primary->surface;
                    unsigned char *dst = (unsigned char *)lock_rc.pBits;

                    int i;
                    for (i = 0; i < ddraw->height; i++)
                    {
                        memcpy(dst, src, ddraw->primary->lPitch);

                        src += ddraw->primary->lPitch;
                        dst += lock_rc.Pitch;
                    }

                    IDirect3DTexture9_UnlockRect(SurfaceTex[texIndex], 0);
                }
            }

            if (ddraw->bpp == 8 && InterlockedExchange(&ddraw->render.paletteUpdated, FALSE))
            {
                if (++palIndex >= TEXTURE_COUNT)
                    palIndex = 0;

                RECT rc = { 0,0,256,1 };

                if (SUCCEEDED(IDirect3DDevice9_SetTexture(D3dDev, 1, (IDirect3DBaseTexture9 *)PaletteTex[palIndex])) &&
                    SUCCEEDED(IDirect3DTexture9_LockRect(PaletteTex[palIndex], 0, &lock_rc, &rc, 0)))
                {
                    memcpy(lock_rc.pBits, ddraw->primary->palette->data_rgb, 256 * sizeof(int));

                    IDirect3DTexture9_UnlockRect(PaletteTex[palIndex], 0);
                }
            }

            if (!ddraw->handlemouse)
            {
                ChildWindowExists = FALSE;
                EnumChildWindows(ddraw->hWnd, EnumChildProc, (LPARAM)ddraw->primary);

                if (ddraw->render.width != ddraw->width || ddraw->render.height != ddraw->height)
                {
                    if (ChildWindowExists)
                    {
                        IDirect3DDevice9_Clear(D3dDev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

                        if (!needsUpdate && UpdateVertices(FALSE, FALSE))
                            needsUpdate = TRUE;
                    }
                    else if (needsUpdate)
                    {
                        if (UpdateVertices(FALSE, TRUE))
                            needsUpdate = FALSE;
                    }
                }
            }
        }

        LeaveCriticalSection(&ddraw->cs);

        IDirect3DDevice9_BeginScene(D3dDev);
        IDirect3DDevice9_DrawPrimitive(D3dDev, D3DPT_TRIANGLESTRIP, 0, 2);
        IDirect3DDevice9_EndScene(D3dDev);

        if (ddraw->bnetActive)
            IDirect3DDevice9_Clear(D3dDev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

        if (FAILED(IDirect3DDevice9_Present(D3dDev, NULL, NULL, NULL, NULL)))
        {
            DWORD_PTR dwResult;
            SendMessageTimeout(ddraw->hWnd, WM_D3D9DEVICELOST, 0, 0, 0, 1000, &dwResult);
        }

#if _DEBUG
        DrawFrameInfoEnd();
#endif
        
        if (ddraw->fpsLimiter.ticklength > 0)
        {
            if (ddraw->fpsLimiter.hTimer)
            {
                if (ddraw->vsync)
                {
                    WaitForSingleObject(ddraw->fpsLimiter.hTimer, ddraw->fpsLimiter.ticklength * 2);
                    LARGE_INTEGER liDueTime = { .QuadPart = -ddraw->fpsLimiter.tickLengthNs };
                    SetWaitableTimer(ddraw->fpsLimiter.hTimer, &liDueTime, 0, NULL, NULL, FALSE);
                }
                else
                {
                    FILETIME ft = { 0 };
                    GetSystemTimeAsFileTime(&ft);

                    if (CompareFileTime((FILETIME *)&ddraw->fpsLimiter.dueTime, &ft) == -1)
                    {
                        memcpy(&ddraw->fpsLimiter.dueTime, &ft, sizeof(LARGE_INTEGER));
                    }
                    else
                    {
                        WaitForSingleObject(ddraw->fpsLimiter.hTimer, ddraw->fpsLimiter.ticklength * 2);
                    }

                    ddraw->fpsLimiter.dueTime.QuadPart += ddraw->fpsLimiter.tickLengthNs;
                    SetWaitableTimer(ddraw->fpsLimiter.hTimer, &ddraw->fpsLimiter.dueTime, 0, NULL, NULL, FALSE);
                }
            }
            else
            {
                tickEnd = timeGetTime();

                if (tickEnd - tickStart < ddraw->fpsLimiter.ticklength)
                    Sleep(ddraw->fpsLimiter.ticklength - (tickEnd - tickStart));
            }
        }
    }
    return 0;
}
