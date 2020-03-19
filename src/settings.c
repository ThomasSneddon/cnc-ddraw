#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <d3d9.h>
#include "main.h"
#include "opengl.h"
#include "render_d3d9.h"
#include "hook.h"

static char SettingsIniPath[MAX_PATH];
static char ProcessFileName[96];

static BOOL GetBool(LPCSTR key, BOOL defaultValue);
static int GetInt(LPCSTR key, int defaultValue);
static DWORD GetString(LPCSTR key, LPCSTR defaultValue, LPSTR outString, DWORD outSize);
static void CreateSettingsIni();

void Settings_Load()
{
    //set up settings ini
    char cwd[MAX_PATH];
    char tmp[256];
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    _snprintf(SettingsIniPath, sizeof(SettingsIniPath), "%s\\ddraw.ini", cwd);

    if (GetFileAttributes(SettingsIniPath) == INVALID_FILE_ATTRIBUTES)
        CreateSettingsIni();

    //get process filename
    char ProcessFilePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, ProcessFilePath, MAX_PATH);
    _splitpath(ProcessFilePath, NULL, NULL, ProcessFileName, NULL);

    //load settings from ini
    ddraw->windowed = GetBool("windowed", FALSE);
    ddraw->border = GetBool("border", TRUE);
    ddraw->boxing = GetBool("boxing", FALSE);
    ddraw->maintas = GetBool("maintas", FALSE);
    ddraw->adjmouse = GetBool("adjmouse", FALSE);
    ddraw->devmode = GetBool("devmode", FALSE);
    ddraw->vsync = GetBool("vsync", FALSE);
    ddraw->noactivateapp = GetBool("noactivateapp", FALSE);
    ddraw->vhack = GetBool("vhack", FALSE);
    ddraw->accurateTimers = GetBool("accuratetimers", FALSE);
    ddraw->resizable = GetBool("resizable", TRUE);

#if RN_FIX
	WindowRect.right = 0;
	WindowRect.bottom = 0;
#else
    WindowRect.right = GetInt("width", 0);
    WindowRect.bottom = GetInt("height", 0);
#endif
    WindowRect.left = GetInt("posX", -32000);
    WindowRect.top = GetInt("posY", -32000);

#ifndef _DEBUG
    HookingMethod = GetInt("hook", 1);
#endif
    
    ddraw->render.maxfps = GetInt("maxfps", 125);

    if (ddraw->render.maxfps)
        ddraw->render.forcefps = GetBool("forcefps", FALSE);

    if (ddraw->accurateTimers || ddraw->vsync)
        ddraw->fpsLimiter.hTimer = CreateWaitableTimer(NULL, TRUE, NULL);
    //can't fully set it up here due to missing ddraw->mode.dmDisplayFrequency

    int maxTicks = GetInt("maxgameticks", 0);
    if (maxTicks > 0 && maxTicks <= 1000)
    {
        if (ddraw->accurateTimers)
            ddraw->ticksLimiter.hTimer = CreateWaitableTimer(NULL, TRUE, NULL);

        float len = 1000.0f / maxTicks;
        ddraw->ticksLimiter.tickLengthNs = len * 10000;
        ddraw->ticksLimiter.ticklength = len + 0.5f;
    }

    //always using 60 fps for flip...
    if (ddraw->accurateTimers)
        ddraw->flipLimiter.hTimer = CreateWaitableTimer(NULL, TRUE, NULL);

    float flipLen = 1000.0f / 60;
    ddraw->flipLimiter.tickLengthNs = flipLen * 10000;
    ddraw->flipLimiter.ticklength = flipLen + 0.5f;


    if ((ddraw->fullscreen = GetBool("fullscreen", FALSE)))
        WindowRect.left = WindowRect.top = -32000;

    if (!(ddraw->handlemouse = GetBool("handlemouse", TRUE)))
        ddraw->adjmouse = TRUE;

    if (GetBool("singlecpu", TRUE))
    {
        SetProcessAffinityMask(GetCurrentProcess(), 1);
    }
    else
    {
        DWORD systemAffinity;
        DWORD procAffinity;
        HANDLE proc = GetCurrentProcess();
        if (GetProcessAffinityMask(proc, &procAffinity, &systemAffinity))
            SetProcessAffinityMask(proc, systemAffinity);
    }

    ddraw->render.bpp = GetInt("bpp", 32);
    if (ddraw->render.bpp != 16 && ddraw->render.bpp != 24 && ddraw->render.bpp != 32)
        ddraw->render.bpp = 0;

    // to do: read .glslp config file instead of the shader and apply the correct settings
    GetString("shader", "", ddraw->shader, sizeof(ddraw->shader));

    GetString("renderer", "auto", tmp, sizeof(tmp));
    printf("Using %s renderer\n", tmp);

    if (tolower(tmp[0]) == 's' || tolower(tmp[0]) == 'g') //gdi
    {
        ddraw->renderer = render_soft_main;
    }
    else if (tolower(tmp[0]) == 'd') //direct3d9
    {
        ddraw->renderer = render_d3d9_main;
    }
    else if (tolower(tmp[0]) == 'o') //opengl
    {
        if (OpenGL_LoadDll())
        {
            ddraw->renderer = render_main;
        }
        else
        {
            ShowDriverWarning = TRUE;
            ddraw->renderer = render_soft_main;
        }
    }
    else //auto
    {
        LPDIRECT3D9 d3d = NULL;

        // Windows = Direct3D 9, Wine = OpenGL
        if (!ddraw->wine && (Direct3D9_hModule = LoadLibrary("d3d9.dll")))
        {
            IDirect3D9 *(WINAPI *D3DCreate9)(UINT) =
                (IDirect3D9 *(WINAPI *)(UINT))GetProcAddress(Direct3D9_hModule, "Direct3DCreate9");

            if (D3DCreate9 && (d3d = D3DCreate9(D3D_SDK_VERSION)))
                IDirect3D9_Release(d3d);
        }

        if (d3d)
        {
            ddraw->renderer = render_d3d9_main;
        }
        else if (OpenGL_LoadDll())
        {
            ddraw->renderer = render_main;
        }
        else
        {
            ShowDriverWarning = TRUE;
            ddraw->renderer = render_soft_main;
        }
    }
}

void Settings_Save(RECT *lpRect, int windowState)
{
    char buf[16];

    if (lpRect->right)
    {
        sprintf(buf, "%ld", lpRect->right);
        WritePrivateProfileString(ProcessFileName, "width", buf, SettingsIniPath);
    }
    
    if (lpRect->bottom)
    {
        sprintf(buf, "%ld", lpRect->bottom);
        WritePrivateProfileString(ProcessFileName, "height", buf, SettingsIniPath);
    }

    if (lpRect->left != -32000)
    {
        sprintf(buf, "%ld", lpRect->left);
        WritePrivateProfileString(ProcessFileName, "posX", buf, SettingsIniPath);
    }

    if (lpRect->top != -32000)
    {
        sprintf(buf, "%ld", lpRect->top);
        WritePrivateProfileString(ProcessFileName, "posY", buf, SettingsIniPath);
    }

    if (windowState != -1)
    {
        WritePrivateProfileString(ProcessFileName, "windowed", windowState ? "true" : "false", SettingsIniPath);
    }
}

static void CreateSettingsIni()
{
    FILE *fh = fopen(SettingsIniPath, "w");
    if (fh)
    {
        fputs(
            "; cnc-ddraw - https://github.com/CnCNet/cnc-ddraw - https://cncnet.org\n"
            "\n"
            "[ddraw]\n"
            "; ### Optional settings ###\n"
            "; Use the following settings to adjust the look and feel to your liking\n"
            "\n"
            "\n"
            "; Stretch to custom resolution, 0 = defaults to the size game requests\n"
            "width=0\n"
            "height=0\n"
            "\n"
            "; Override the width/height settings shown above and always stretch to fullscreen\n"
            "; Note: Can be combined with 'windowed=true' to get windowed-fullscreen aka borderless mode\n"
            "fullscreen=false\n"
            "\n"
            "; Run in windowed mode rather than going fullscreen\n"
            "windowed=false\n"
            "\n"
            "; Maintain aspect ratio - (Requires 'handlemouse=true')\n"
            "maintas=false\n"
            "\n"
            "; Windowboxing / Integer Scaling - (Requires 'handlemouse=true')\n"
            "boxing=false\n"
            "\n"
            "; Real rendering rate, -1 = screen rate, 0 = unlimited, n = cap\n"
            "; Note: Does not have an impact on the game speed, to limit your game speed use 'maxgameticks='\n"
            "maxfps=125\n"
            "\n"
            "; Vertical synchronization, enable if you get tearing - (Requires 'renderer=auto/opengl/direct3d9')\n"
            "vsync=false\n"
            "\n"
            "; Automatic mouse sensitivity scaling  - (Requires 'handlemouse=true')\n"
            "; Note: Only works if stretching is enabled. Sensitivity will be adjusted according to the size of the window\n"
            "adjmouse=false\n"
            "\n"
            "; Preliminary libretro shader support - (Requires 'renderer=opengl') https://github.com/libretro/glsl-shaders\n"
            "shader=Shaders\\bilinear.glsl\n"
            "\n"
            "; Window position, -32000 = center to screen\n"
            "posX=-32000\n"
            "posY=-32000\n"
            "\n"
            "; Renderer, possible values: auto, opengl, gdi, direct3d9 (auto = try direct3d9/opengl, fallback = gdi)\n"
            "renderer=auto\n"
            "\n"
            "; Developer mode (don't lock the cursor)\n"
            "devmode=true\n"
            "\n"
            "; Show window borders in windowed mode\n"
            "border=true\n"
            "\n"
            "; Bits per pixel, possible values: 16, 24 and 32, 0 = auto\n"
            "bpp=0\n"
            "\n"
            "; Enable C&C video resize hack - Stretches C&C cutscenes to fullscreen\n"
            "vhack=false\n"
            "\n"
            "\n"
            "\n"
            "; ### Compatibility settings ###\n"
            "; Use the following settings in case there are any issues with the game\n"
            "\n"
            "\n"
            "; Hide WM_ACTIVATEAPP messages to prevent problems on alt+tab\n"
            "noactivateapp=false\n"
            "\n"
            "; Max game ticks per second, possible values: 0-1000\n"
            "; Note: Can be used to slow down a too fast running game, fix flickering or too fast animations\n"
            "maxgameticks=0\n"
            "\n"
            "; Gives cnc-ddraw full control over the mouse cursor (required for adjmouse/boxing/maintas)\n"
            "; Note: This option only works for games that draw their own cursor and it must be disabled for all other games\n"
            "handlemouse=true\n"
            "\n"
            "; Use Waitable Timer Objects rather than timeGetTime+Sleep to limit FPS/Ticks/Flip\n"
            "; Note: To workaround tearing/stuttering problems, set maxfps 1 lower than screen refresh rate (59 for flip games)\n"
            "accuratetimers=false\n"
            "\n"
            "; Force CPU0 affinity, avoids crashes/freezing, *might* have a performance impact\n"
            "singlecpu=true\n"
            "\n"
            "; Windows API Hooking, Possible values: 0 = disabled, 1 = IAT Hooking, 2 = Microsoft Detours\n"
            "; Note: Can be used to fix issues related to new features added by cnc-ddraw such as windowed mode or stretching\n"
            "hook=1\n"
            "\n"
            "; Force consistent FPS (Requires 'maxfps=' to be set to a value other than 0)\n"
            "; Note: Fixes flickering cursor issues in C&C games\n"
            "forcefps=false\n"
            "\n"
            "\n"
            "\n"
            "; ### Game specific settings ###\n"
            "; The following settings override all settings shown above, section name = executable name\n"
            "\n"
            "\n"
            "; Carmageddon\n"
            "[CARMA95]\n"
            "renderer=opengl\n"
            "noactivateapp=true\n"
            "maxgameticks=30\n"
            "\n"
            "; Command & Conquer Gold\n"
            "[C&C95]\n"
            "maxgameticks=120\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Red Alert\n"
            "[ra95]\n"
            "maxgameticks=120\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Red Alert\n"
            "[ra95p]\n"
            "maxgameticks=120\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Age of Empires\n"
            "[empires]\n"
            "handlemouse=false\n"
            "\n"
            "; Age of Empires: The Rise of Rome\n"
            "[empiresx]\n"
            "handlemouse=false\n"
            "\n"
            "; Age of Empires II\n"
            "[EMPIRES2]\n"
            "handlemouse=false\n"
            "\n"
            "; Age of Empires II: The Conquerors\n"
            "[age2_x1]\n"
            "handlemouse=false\n"
            "\n"
            "; Outlaws\n"
            "[olwin]\n"
            "noactivateapp=true\n"
            "maxgameticks=60\n"
            "hook=2\n"
            "handlemouse=false\n"
            "renderer=gdi\n"
            "\n"
            "; Dark Reign: The Future of War\n"
            "[DKReign]\n"
            "maxgameticks=60\n"
            "\n"
            "; Star Wars: Galactic Battlegrounds\n"
            "[battlegrounds]\n"
            "handlemouse=false\n"
            "\n"
            "; Star Wars: Galactic Battlegrounds: Clone Campaigns\n"
            "[battlegrounds_x1]\n"
            "handlemouse=false\n"
            "\n"
            "; Carmageddon 2\n"
            "[Carma2_SW]\n"
            "renderer=opengl\n"
            "noactivateapp=true\n"
            "maxgameticks=60\n"
            "\n"
            "; Atomic Bomberman\n"
            "[BM]\n"
            "maxgameticks=60\n"
            "\n"
            "; Dune 2000\n"
            "[dune2000]\n"
            "maxfps=59\n"
            "accuratetimers=true\n"
            "\n"
            "; Dune 2000 - CnCNet\n"
            "[dune2000-spawn]\n"
            "maxfps=59\n"
            "accuratetimers=true\n"
            "\n"
            "; Command & Conquer: Tiberian Sun / Command & Conquer: Red Alert 2\n"
            "[game]\n"
            "checkfile=.\\blowfish.dll\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Tiberian Sun Demo\n"
            "[SUN]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Tiberian Sun - CnCNet\n"
            "[ts-spawn]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Red Alert 2 - XWIS\n"
            "[ra2]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Red Alert 2 - XWIS\n"
            "[Red Alert 2]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Red Alert 2: Yuri's Revenge/Revenge Now\n"
            "[gamemd]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
			"; Command & Conquer: Revenge Now\n"
			"[gamern]\n"
			"noactivateapp=true\n"
			"handlemouse=false\n"
			"maxfps=60\n"
			"forcefps=true\n"
			"\n"
            "; Command & Conquer: Red Alert 2: Yuri's Revenge - ?ModExe?\n"
            "[ra2md]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Red Alert 2: Yuri's Revenge - CnCNet\n"
            "[gamemd-spawn]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"
            "; Command & Conquer: Red Alert 2: Yuri's Revenge - XWIS\n"
            "[Yuri's Revenge]\n"
            "noactivateapp=true\n"
            "handlemouse=false\n"
            "maxfps=60\n"
            "forcefps=true\n"
            "\n"

            , fh);
        fclose(fh);
    }
}

static DWORD GetString(LPCSTR key, LPCSTR defaultValue, LPSTR outString, DWORD outSize)
{
    DWORD s = GetPrivateProfileStringA(ProcessFileName, key, "", outString, outSize, SettingsIniPath);
    if (s > 0)
    {
        char buf[MAX_PATH] = { 0 };

        if (GetPrivateProfileStringA(ProcessFileName, "checkfile", "", buf, sizeof(buf), SettingsIniPath) > 0)
        {
            if (GetFileAttributes(buf) != INVALID_FILE_ATTRIBUTES)
                return s;
        }
        else
            return s;
    }

    return GetPrivateProfileStringA("ddraw", key, defaultValue, outString, outSize, SettingsIniPath);
}

static BOOL GetBool(LPCSTR key, BOOL defaultValue)
{
    char value[8];
    GetString(key, defaultValue ? "Yes" : "No", value, sizeof(value));

    return (_stricmp(value, "yes") == 0 || _stricmp(value, "true") == 0 || _stricmp(value, "1") == 0);
}

static int GetInt(LPCSTR key, int defaultValue)
{
    char defvalue[16];
    _snprintf(defvalue, sizeof(defvalue), "%d", defaultValue);

    char value[16];
    GetString(key, defvalue, value, sizeof(value));

    return atoi(value);
}
