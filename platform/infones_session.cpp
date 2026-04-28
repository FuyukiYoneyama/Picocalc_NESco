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

static const char *rom_log_basename(const char *path)
{
    const char *base = path;

    if (!path)
    {
        return "";
    }

    for (const char *p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            base = p + 1;
        }
    }

    return base;
}

static void log_rom_start_once(const char *path)
{
#if defined(NESCO_CORE1_BASELINE_LOG)
    std::printf("[ROM_START] name=%s path=%s mapper=%u prg16=%u chr8=%u battery=%u trainer=%u\n",
                rom_log_basename(path),
                path ? path : "",
                static_cast<unsigned>(MapperNo),
                static_cast<unsigned>(NesHeader.byRomSize),
                static_cast<unsigned>(NesHeader.byVRomSize),
                (NesHeader.byInfo1 & 0x02u) ? 1u : 0u,
                (NesHeader.byInfo1 & 0x04u) ? 1u : 0u);
    std::fflush(stdout);
#else
    (void)path;
#endif
}

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
    log_rom_start_once(selected_path);
    core1_set_services(CORE1_SERVICE_KEYBOARD | CORE1_SERVICE_LCD);

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
