#ifndef DEBUG_H
#define DEBUG_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int dbg_exception_handler(EXCEPTION_POINTERS* exception);
void dbg_counter_start();
double dbg_counter_stop();
void dbg_debug_string(const char* format, ...);
void dbg_draw_frame_info_start();
void dbg_draw_frame_info_end();
int dbg_printf(const char* fmt, ...);
void dbg_init();
void dbg_dump_dds_blt_flags(DWORD flags);
void dbg_dump_dds_blt_fx_flags(DWORD flags);
void dbg_dump_dds_caps(DWORD caps);
void dbg_dump_dds_flags(DWORD flags);
void dbg_dump_dds_blt_fast_flags(DWORD flags);
void dbg_dump_dds_lock_flags(DWORD flags);
char* dbg_mes_to_str(int id);

extern double g_dbg_frame_time;
extern DWORD g_dbg_frame_count;

#if defined(_DEBUG)
    #define LOG_ENABLED // set this in compiler option to enable log
#endif

//#define _DEBUG 1

/* use OutputDebugStringA rather than printf */
//#define _DEBUG_S 1

/* log everything (slow) */
//#define _DEBUG_X 1



#ifdef LOG_ENABLED

#ifdef _DEBUG_S

#define TRACE(format, ...) dbg_debug_string("xDBG " format, ##__VA_ARGS__)

#ifdef _DEBUG_X
#define TRACE_EXT(format, ...) dbg_debug_string("xDBG " format, ##__VA_ARGS__)
#else
#define TRACE_EXT(format, ...)
#endif

#else

#define TRACE(format, ...) dbg_printf(format, ##__VA_ARGS__) 

#ifdef _DEBUG_X
#define TRACE_EXT(format, ...) dbg_printf(format, ##__VA_ARGS__) 
#else
#define TRACE_EXT(format, ...)
#endif

#endif 

#else 
#define TRACE(format, ...)
#define TRACE_EXT(format, ...)
#endif

#endif
