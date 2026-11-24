// ============================================================================
//  MyMCLauncher plugin development interface (plugin_api.h)
//  ----------------------------------------------------------------------------
//  A plugin is a Windows DLL placed under the launcher plugins\ subdirectory,
//  The launcher auto-loads *.dll and calls plugin_init() on startup.
//
//  Compile plugin example (use the same compiler as the launcher):
//    D:\Dev-Cpp\MinGW64\bin\g++.exe -O2 -std=c++14 -shared -o my_plugin.dll my_plugin.cpp
// ============================================================================

#ifndef MYMCLAUNCHER_PLUGIN_API_H
#define MYMCLAUNCHER_PLUGIN_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define MLAUNCHER_PLUGIN_API_VERSION 1

/* Event types (used with register_hook) */
typedef enum PluginEventType {
    EVENT_LAUNCHER_START = 1,        /* Launcher initialization done */
    EVENT_VERSION_INSTALL_START,     /* A version install starts */
    EVENT_VERSION_INSTALL_DONE,      /* A version install done */
    EVENT_DOWNLOAD_START,            /* A file download starts (message = file name) */
    EVENT_DOWNLOAD_DONE,             /* A file download done (result=0 success) */
    EVENT_GAME_LAUNCHING,            /* Game about to launch (message = version id) */
    EVENT_GAME_EXITED                /* Game exited (result=exit code or -1) */
} PluginEventType;

/* Event object, created by launcher, read-only to plugins */
typedef struct PluginEvent {
    int type;
    const char* message;   /* UTF-8 string, event description */
    void* data;            /* Event data pointer, may be NULL */
    int result;            /* 0 = success */
} PluginEvent;

/* Plugin-registered callback: return 0 to continue, non-0 to block */
typedef int (*PluginHookFn)(const PluginEvent* ev);

/* Host services the launcher provides to plugins */
typedef struct PluginHost {
    int api_version;

    /* Write log to launcher log panel and file */
    void (*log)(const char* level, const char* message);

    /* Read a launcher setting (UTF-8); returned pointer is launcher-owned, do not free */
    const char* (*get_setting)(const char* key);

    /* Write a launcher setting */
    void (*set_setting)(const char* key, const char* value);

    /* Register a hook for an event type, return 0 on success */
    int (*register_hook)(int event_type, PluginHookFn fn);

    /* Set the launcher window/taskbar icon from an .ico file (UTF-8 path).
     * Returns 0 on success. The custom icon is persisted to config.json. */
    int (*set_app_icon)(const char* icon_file_path);

    /* Register a toolbar button on the main window (UTF-8 label).
     * Returns the button id (>=0) on success, -1 on failure.
     * The callback fn is invoked (with ev->message = label) when clicked. */
    int (*add_button)(const char* label, PluginHookFn fn);

    /* Launch a game version (UTF-8 version id). Empty version id launches the
     * currently selected version. Returns 0 on success, non-0 on failure. */
    int (*launch_game)(const char* version_id);

    /* List loaded plugins, comma-separated UTF-8 string. Launcher-owned. */
    const char* (*list_plugins)(void);

    /* Return the launcher data directory (UTF-8, contains config.json). */
    const char* (*get_launcher_dir)(void);
} PluginHost;

/*
 * Every plugin must export the following two functions:
 *
 *   int  plugin_init(const PluginHost* host);
 *        - return 0 = init success, non-0 = unloaded
 *   void plugin_shutdown(void);
 *        - called before the launcher exits
 *
 * Optional exports:
 *   const char* plugin_name(void);
 *   const char* plugin_version(void);
 */

#ifdef _WIN32
#ifdef __cplusplus
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT __declspec(dllexport)
#endif
#else
#ifdef __cplusplus
#define PLUGIN_EXPORT extern "C"
#else
#define PLUGIN_EXPORT
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* MYMCLAUNCHER_PLUGIN_API_H */
