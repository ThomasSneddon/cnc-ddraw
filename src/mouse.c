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
#include <stdio.h>
#include "main.h"
#include "surface.h"
#include "hook.h"
#include "render_d3d9.h"

int yAdjust = 0;

BOOL WINAPI fake_GetCursorPos(LPPOINT lpPoint)
{
    POINT pt, realpt;
    
    if (!real_GetCursorPos(&pt) || !ddraw)
        return FALSE;
    
    realpt.x = pt.x;
    realpt.y = pt.y;
    
    if(ddraw->locked && (!ddraw->windowed || real_ScreenToClient(ddraw->hWnd, &pt)))
    {
        //fallback solution for possible ClipCursor failure
        int diffx = 0, diffy = 0;
        int maxWidth = ddraw->adjmouse ? ddraw->render.viewport.width : ddraw->width;
        int maxHeight = ddraw->adjmouse ? ddraw->render.viewport.height : ddraw->height;

        if (pt.x < 0)
        {
            diffx = pt.x;
            pt.x = 0;
        }

        if (pt.y < 0)
        {
            diffy = pt.y;
            pt.y = 0;
        }

        if (pt.x > maxWidth)
        {
            diffx = pt.x - maxWidth;
            pt.x = maxWidth;
        }

        if (pt.y > maxHeight)
        {
            diffy = pt.y - maxHeight;
            pt.y = maxHeight;
        }

        if (diffx || diffy)
            real_SetCursorPos(realpt.x - diffx, realpt.y - diffy);


        if(ddraw->adjmouse)
        {
            ddraw->cursor.x = pt.x * ddraw->render.unScaleW;
            ddraw->cursor.y = pt.y * ddraw->render.unScaleH;
        }
        else
        {
            ddraw->cursor.x = pt.x;
            ddraw->cursor.y = pt.y;
        }

        if (ddraw->vhack && InterlockedExchangeAdd(&ddraw->incutscene, 0))
        {
            diffx = 0;
            diffy = 0;

            if (ddraw->cursor.x > CUTSCENE_WIDTH)
            {
                diffx = ddraw->cursor.x - CUTSCENE_WIDTH;
                ddraw->cursor.x = CUTSCENE_WIDTH;
            }
                
            if (ddraw->cursor.y > CUTSCENE_HEIGHT)
            {
                diffy = ddraw->cursor.y - CUTSCENE_HEIGHT;
                ddraw->cursor.y = CUTSCENE_HEIGHT;
            }

            if (diffx || diffy)
                real_SetCursorPos(realpt.x - diffx, realpt.y - diffy);
        }
    }

    if (lpPoint)
    {
        lpPoint->x = (int)ddraw->cursor.x;
        lpPoint->y = (int)ddraw->cursor.y;
    }
    
    return TRUE;
}

BOOL WINAPI fake_ClipCursor(const RECT *lpRect)
{
    if(lpRect)
    {
        /* hack for 640x480 mode */
        if (lpRect->bottom == 400 && ddraw->height == 480)
            yAdjust = 40;
    }
    return TRUE;
}

int WINAPI fake_ShowCursor(BOOL bShow)
{
    static int count;

    if (ddraw && !ddraw->handlemouse)
        return real_ShowCursor(bShow);

    return bShow ? ++count : --count;
}

HCURSOR WINAPI fake_SetCursor(HCURSOR hCursor)
{
    if (ddraw && !ddraw->handlemouse)
        return real_SetCursor(hCursor); 
    
    return NULL;
}

void mouse_lock()
{
    RECT rc;

    if (ddraw->bnetActive)
        return;

    if (ddraw->devmode)
    {
        if (ddraw->handlemouse)
            while(real_ShowCursor(FALSE) > 0);

        return;
    }

    if (Hook_Active && !ddraw->locked)
    {
        // Get the window client area.
        real_GetClientRect(ddraw->hWnd, &rc);
        
        if(ddraw->adjmouse)
        {
            rc.right = ddraw->render.viewport.width;
            rc.bottom = ddraw->render.viewport.height;
        }
        else
        {
            rc.right = ddraw->width;
            rc.bottom = ddraw->height;
        }

        // Convert the client area to screen coordinates.
        POINT pt = { rc.left, rc.top };
        POINT pt2 = { rc.right, rc.bottom };
        real_ClientToScreen(ddraw->hWnd, &pt);
        real_ClientToScreen(ddraw->hWnd, &pt2);
        
        SetRect(&rc, pt.x, pt.y, pt2.x, pt2.y);
        
        rc.bottom -= (yAdjust * 2) * ddraw->render.scaleH;

        if(ddraw->adjmouse)
        {
            real_SetCursorPos(
                rc.left + (ddraw->cursor.x * ddraw->render.scaleW), 
                rc.top + ((ddraw->cursor.y - yAdjust) * ddraw->render.scaleH));
        }
        else
        {
            real_SetCursorPos(rc.left + ddraw->cursor.x, rc.top + ddraw->cursor.y - yAdjust);
        }

        if (ddraw->handlemouse)
        {
            SetCapture(ddraw->hWnd);
            real_ClipCursor(&rc);
            while (real_ShowCursor(FALSE) > 0);
        }
        else
        {
            if (ddraw->hidecursor)
            {
                ddraw->hidecursor = FALSE;
                real_ShowCursor(FALSE);
            }
            real_ClipCursor(&rc);
        }

        ddraw->locked = TRUE;
    }
}

void mouse_unlock()
{
    RECT rc;

    if (ddraw->devmode)
    {
        if (ddraw->handlemouse)
            while(real_ShowCursor(TRUE) < 0);

        return;
    }

    if(!Hook_Active)
    {
        return;
    }

    if(ddraw->locked)
    {
        ddraw->locked = FALSE;

        // Get the window client area.
        real_GetClientRect(ddraw->hWnd, &rc);
        
        // Convert the client area to screen coordinates.
        POINT pt = { rc.left, rc.top };
        POINT pt2 = { rc.right, rc.bottom };
        real_ClientToScreen(ddraw->hWnd, &pt);
        real_ClientToScreen(ddraw->hWnd, &pt2);
        SetRect(&rc, pt.x, pt.y, pt2.x, pt2.y);
       
        if (ddraw->handlemouse)
        {
            while (real_ShowCursor(TRUE) < 0);
            real_SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        else
        {
            CURSORINFO ci = { .cbSize = sizeof(CURSORINFO) };
            if (real_GetCursorInfo(&ci) && ci.flags == 0)
            {
                ddraw->hidecursor = TRUE;
                while (real_ShowCursor(TRUE) < 0);
            }
        }

        real_ClipCursor(NULL);
        ReleaseCapture();
        
        real_SetCursorPos(
            rc.left + ddraw->render.viewport.x + (ddraw->cursor.x * ddraw->render.scaleW), 
            rc.top + ddraw->render.viewport.y + ((ddraw->cursor.y + yAdjust) * ddraw->render.scaleH));
    }
}

BOOL WINAPI fake_GetWindowRect(HWND hWnd, LPRECT lpRect)
{
    if (lpRect && ddraw)
    {
        if (ddraw->hWnd == hWnd)
        {
            lpRect->bottom = ddraw->height;
            lpRect->left = 0;
            lpRect->right = ddraw->width;
            lpRect->top = 0;

            return TRUE;
        }
        else
        {
            if (real_GetWindowRect(hWnd, lpRect))
            {
                MapWindowPoints(HWND_DESKTOP, ddraw->hWnd, (LPPOINT)lpRect, 2);

                return TRUE;
            }

            return FALSE;
        }
    }

    return real_GetWindowRect(hWnd, lpRect);
}

BOOL WINAPI fake_GetClientRect(HWND hWnd, LPRECT lpRect)
{
    if (lpRect && ddraw && ddraw->hWnd == hWnd)
    {
        lpRect->bottom = ddraw->height;
        lpRect->left = 0;
        lpRect->right = ddraw->width;
        lpRect->top = 0;

        return TRUE;
    }

    return real_GetClientRect(hWnd, lpRect);
}

BOOL WINAPI fake_ClientToScreen(HWND hWnd, LPPOINT lpPoint)
{
    if (ddraw && ddraw->hWnd != hWnd)
        return real_ClientToScreen(hWnd, lpPoint) && real_ScreenToClient(ddraw->hWnd, lpPoint);

    return TRUE;
}

BOOL WINAPI fake_ScreenToClient(HWND hWnd, LPPOINT lpPoint)
{
    if (ddraw && ddraw->hWnd != hWnd)
        return real_ClientToScreen(ddraw->hWnd, lpPoint) && real_ScreenToClient(hWnd, lpPoint);

    return TRUE;
}

BOOL WINAPI fake_SetCursorPos(int X, int Y)
{
    POINT pt = { X, Y };
    return ddraw && real_ClientToScreen(ddraw->hWnd, &pt) && real_SetCursorPos(pt.x, pt.y);
}

HWND WINAPI fake_WindowFromPoint(POINT Point)
{
    POINT pt = { Point.x, Point.y };
    return ddraw && real_ClientToScreen(ddraw->hWnd, &pt) ? real_WindowFromPoint(pt) : NULL;
}

BOOL WINAPI fake_GetClipCursor(LPRECT lpRect)
{
    if (lpRect && ddraw)
    {
        lpRect->bottom = ddraw->height;
        lpRect->left = 0;
        lpRect->right = ddraw->width;
        lpRect->top = 0;

        return TRUE;
    }

    return FALSE;
}

BOOL WINAPI fake_GetCursorInfo(PCURSORINFO pci)
{
    return pci && ddraw && real_GetCursorInfo(pci) && real_ScreenToClient(ddraw->hWnd, &pci->ptScreenPos);
}

int WINAPI fake_GetSystemMetrics(int nIndex)
{
    if (ddraw)
    {
        if (nIndex == SM_CXSCREEN)
            return ddraw->width;

        if (nIndex == SM_CYSCREEN)
            return ddraw->height;
    }

    return real_GetSystemMetrics(nIndex);
}

BOOL WINAPI fake_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    UINT reqFlags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER;

    if (ddraw && ddraw->hWnd == hWnd && (uFlags & reqFlags) != reqFlags)
        return TRUE;

    return real_SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

BOOL WINAPI fake_MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint)
{
	RECT wndRect;
	DWORD dwReturnSpace;

	_asm {
		push eax;
		mov eax, dword ptr ds : [ebp + 4];
		mov dwReturnSpace, eax;
		pop eax;
	}

    if (ddraw && ddraw->hWnd == hWnd)
        return TRUE;

#ifdef RN_FIX
	if (ddraw->windowed && dwReturnSpace == 0x60B872) {
		GetClientRect(ddraw->hWnd, &wndRect);
		ClientToScreen(ddraw->hWnd, (LPPOINT)&wndRect);
		printf_s("coords = %d, %d, %d, %d.\n", wndRect);
		X += wndRect.left;
		Y += wndRect.top;
	}
#endif
    return real_MoveWindow(hWnd, X, Y, nWidth, nHeight, bRepaint);
}

LRESULT WINAPI fake_SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = real_SendMessageA(hWnd, Msg, wParam, lParam);

    if (result && ddraw && Msg == CB_GETDROPPEDCONTROLRECT)
    {
        RECT *rc = (RECT *)lParam;
        if (rc)
            MapWindowPoints(HWND_DESKTOP, ddraw->hWnd, (LPPOINT)rc, 2);
    }

    return result;
}

LONG WINAPI fake_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
{
    if (ddraw && ddraw->hWnd == hWnd && nIndex == GWL_STYLE)
        return 0;

    return real_SetWindowLongA(hWnd, nIndex, dwNewLong);
}

BOOL WINAPI fake_EnableWindow(HWND hWnd, BOOL bEnable)
{
    if (ddraw && ddraw->hWnd == hWnd)
    {
        return FALSE;
    }

    return real_EnableWindow(hWnd, bEnable);
}

BOOL WINAPI fake_DestroyWindow(HWND hWnd)
{
    BOOL result = real_DestroyWindow(hWnd);

    if (ddraw && ddraw->hWnd != hWnd && ddraw->bnetActive)
    {
        RedrawWindow(NULL, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);

        if (!FindWindowEx(HWND_DESKTOP, NULL, "SDlgDialog", NULL))
        {
            ddraw->bnetActive = FALSE;
            SetFocus(ddraw->hWnd);
            mouse_lock();

            if (ddraw->windowed)
            {
                ddraw->bnetPos.x = ddraw->bnetPos.y = 0;
                real_ClientToScreen(ddraw->hWnd, &ddraw->bnetPos);

                if (!ddraw->bnetWasUpscaled)
                {
                    int width = ddraw->bnetWinRect.right - ddraw->bnetWinRect.left;
                    int height = ddraw->bnetWinRect.bottom - ddraw->bnetWinRect.top;
                    UINT flags = width != ddraw->width || height != ddraw->height ? 0 : SWP_NOMOVE;

                    SetWindowRect(ddraw->bnetWinRect.left, ddraw->bnetWinRect.top, width, height, flags);
                }

                ddraw->fullscreen = ddraw->bnetWasUpscaled;

                SetTimer(ddraw->hWnd, IDT_TIMER_LEAVE_BNET, 1000, (TIMERPROC)NULL);

                ddraw->resizable = TRUE;
            }
        }
    }

    return result;
}

HWND WINAPI fake_CreateWindowExA(
    DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y,
    int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (lpClassName && _strcmpi(lpClassName, "SDlgDialog") == 0 && ddraw)
    {
        if (!ddraw->bnetActive)
        {
            ddraw->bnetWasUpscaled = ddraw->fullscreen;
            ddraw->fullscreen = FALSE;

            if (!ddraw->windowed && !ddraw->bnetWasFullscreen)
            {
                int ws = WindowState;
                ToggleFullscreen();
                WindowState = ws;
                ddraw->bnetWasFullscreen = TRUE;
            }

            real_GetClientRect(ddraw->hWnd, &ddraw->bnetWinRect);
            MapWindowPoints(ddraw->hWnd, HWND_DESKTOP, (LPPOINT)&ddraw->bnetWinRect, 2);

            int width = ddraw->bnetWinRect.right - ddraw->bnetWinRect.left;
            int height = ddraw->bnetWinRect.bottom - ddraw->bnetWinRect.top;
            int x = ddraw->bnetPos.x || ddraw->bnetPos.y ? ddraw->bnetPos.x : -32000;
            int y = ddraw->bnetPos.x || ddraw->bnetPos.y ? ddraw->bnetPos.y : -32000;
            UINT flags = width != ddraw->width || height != ddraw->height ? 0 : SWP_NOMOVE;

            SetWindowRect(x, y, ddraw->width, ddraw->height, flags);
            ddraw->resizable = FALSE;

            ddraw->bnetActive = TRUE;
            mouse_unlock();
        }

        POINT pt = { 0, 0 };
        real_ClientToScreen(ddraw->hWnd, &pt);

        X += pt.x;
        Y += pt.y;

        dwStyle |= WS_CLIPCHILDREN;
    }

    return real_CreateWindowExA(
        dwExStyle,
        lpClassName,
        lpWindowName,
        dwStyle,
        X,
        Y,
        nWidth,
        nHeight,
        hWndParent,
        hMenu,
        hInstance,
        lpParam);
}
