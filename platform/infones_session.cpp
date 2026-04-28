#include "infones_bridge.h"

#include "core1_worker.h"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "runtime_log.h"

#include <cstdarg>
#include <cstdio>

extern "C" const char *rom_image_get_selected_path(void);
extern "C" void display_set_mode(int mode);
extern "C" void display_clear_rgb565(WORD color);
extern "C" void display_show_loading_screen(void);

enum {
    DISPLAY_MODE_FULLSCREEN = 0,
    DISPLAY_MODE_NES_VIEW = 1,
};

extern "C" void run_infones_session(void)
{
    InfoNES_Main();
    core1_set_services(0);
    (void)core1_wait_idle_ack(100);
}

int InfoNES_Menu()
{
    const char *selected_path = rom_image_get_selected_path();
    int load_result;

    if (!selected_path || selected_path[0] == '\0')
    {
        InfoNES_DebugPrint("[INFONES] no ROM selected\n");
        return 0;
    }

    display_show_loading_screen();
    load_result = InfoNES_Load(selected_path);
    if (load_result != 0)
    {
        return load_result;
    }

    display_set_mode(DISPLAY_MODE_NES_VIEW);
    core1_set_services(CORE1_SERVICE_KEYBOARD);

    return load_result;
}

void InfoNES_DebugPrint(const char *pszMsg)
{
    if (!pszMsg)
    {
        return;
    }

    NESCO_LOG_PUTS(pszMsg);
}

void InfoNES_MessageBox(const char *pszMsg, ...)
{
    std::va_list ap;

    va_start(ap, pszMsg);
#if defined(NESCO_RUNTIME_LOGS)
    std::fputs("[INFONES] ", stdout);
    std::vfprintf(stdout, pszMsg, ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);
#endif
    va_end(ap);
}

void InfoNES_Error(const char *pszMsg, ...)
{
    std::va_list ap;

    va_start(ap, pszMsg);
#if defined(NESCO_RUNTIME_LOGS)
    std::fputs("[INFONES_ERROR] ", stdout);
    std::vfprintf(stdout, pszMsg, ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);
#endif
    va_end(ap);
}
