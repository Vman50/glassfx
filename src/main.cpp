#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprutils/os/FileDescriptor.hpp>
#include <unistd.h>

#include "ShaderSystem.hpp"
#include "ShaderManager.hpp"
#include "Config.hpp"
#include "FullscreenPass.hpp"
#include "Dispatchers.hpp"

// Force export of the API version hash symbol required by hyprpm
extern "C" {
    EXPORT const char* __hyprland_api_get_client_hash_ref = __hyprland_api_get_client_hash();
}

ShaderSystem*   g_pShaderSystem   = nullptr;
ShaderManager*  g_pShaderManager  = nullptr;
GlassFXConfig*  g_pConfig         = nullptr;
FullscreenPass* g_pFullscreenPass = nullptr;

static HANDLE g_pHandle = nullptr;

static CHyprSignalListener s_openListener;
static CHyprSignalListener s_closeListener;
static CHyprSignalListener s_focusListener;
static CHyprSignalListener s_configListener;

APICALL EXPORT std::string pluginAPIVersion() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO pluginInit(HANDLE handle) {
    g_pHandle = handle;

    g_pConfig = new GlassFXConfig();
    g_pConfig->init(handle);

    g_pShaderSystem = new ShaderSystem();
    g_pShaderSystem->init();

    g_pShaderManager = new ShaderManager();

    g_pFullscreenPass = new FullscreenPass();
    g_pFullscreenPass->init();

    // Register dispatchers
    HyprlandAPI::addDispatcherV2(handle, "glassfx_set",    dispatchSet);
    HyprlandAPI::addDispatcherV2(handle, "glassfx_param",  dispatchParam);
    HyprlandAPI::addDispatcherV2(handle, "glassfx_reload", dispatchReload);
    HyprlandAPI::addDispatcherV2(handle, "glassfx_clear",  dispatchClear);
    HyprlandAPI::addDispatcherV2(handle, "glassfx_list",   dispatchList);
    HyprlandAPI::addDispatcherV2(handle, "glassfx_tune",   dispatchShorthand);

    // Subscribe to events
    s_openListener = Event::bus()->m_events.window.open.listen([](PHLWINDOW w) {
        if (g_pShaderManager)
            g_pShaderManager->onWindowOpen(w);
    });

    s_closeListener = Event::bus()->m_events.window.destroy.listen([](PHLWINDOW w) {
        if (g_pShaderManager)
            g_pShaderManager->onWindowClose(w);
    });

    s_focusListener = Event::bus()->m_events.window.active.listen([](PHLWINDOW w, Desktop::eFocusReason) {
        if (g_pShaderManager)
            g_pShaderManager->onFocusChange(w);
    });

    s_configListener = Event::bus()->m_events.config.reloaded.listen([]() {
        if (g_pConfig)
            g_pConfig->reloadRules();
    });

    g_pShaderSystem->startInotify();
    {
        int rawFd = g_pShaderSystem->inotifyFd();
        if (rawFd >= 0) {
            int dupFd = dup(rawFd);
            if (dupFd >= 0) {
                Hyprutils::OS::CFileDescriptor cfd(dupFd);
                g_pEventLoopManager->doOnReadable(std::move(cfd), []() {
                    if (g_pShaderSystem) g_pShaderSystem->onInotifyReadable();
                });
            }
        }
    }

    HyprlandAPI::addNotification(handle, "[GlassFX] Plugin loaded", CHyprColor{0.2f, 0.8f, 0.2f, 1.0f}, 3000);

    return {"GlassFX", "Per-window GLSL shader system", "glassfx", "1.0"};
}

APICALL EXPORT void pluginExit() {
    s_openListener   = nullptr;
    s_closeListener  = nullptr;
    s_focusListener  = nullptr;
    s_configListener = nullptr;

    if (g_pShaderManager) {
        g_pShaderManager->removeAll();
        delete g_pShaderManager;
        g_pShaderManager = nullptr;
    }

    if (g_pFullscreenPass) {
        g_pFullscreenPass->shutdown();
        delete g_pFullscreenPass;
        g_pFullscreenPass = nullptr;
    }

    if (g_pShaderSystem) {
        g_pShaderSystem->shutdown();
        delete g_pShaderSystem;
        g_pShaderSystem = nullptr;
    }

    if (g_pConfig) {
        g_pConfig->shutdown();
        delete g_pConfig;
        g_pConfig = nullptr;
    }

    HyprlandAPI::removeDispatcher(g_pHandle, "glassfx_set");
    HyprlandAPI::removeDispatcher(g_pHandle, "glassfx_param");
    HyprlandAPI::removeDispatcher(g_pHandle, "glassfx_reload");
    HyprlandAPI::removeDispatcher(g_pHandle, "glassfx_clear");
    HyprlandAPI::removeDispatcher(g_pHandle, "glassfx_list");
    HyprlandAPI::removeDispatcher(g_pHandle, "glassfx_tune");
}
