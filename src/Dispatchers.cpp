#include "Dispatchers.hpp"
#include "ShaderSystem.hpp"
#include "ShaderManager.hpp"
#include "FullscreenPass.hpp"
#include "Config.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <sstream>
#include <hyprland/src/debug/log/Logger.hpp>

// Parse "key:value key:value ..." into a map
static std::unordered_map<std::string, std::string> parseArgs(const std::string& args) {
    std::unordered_map<std::string, std::string> result;
    std::istringstream ss(args);
    std::string token;
    while (ss >> token) {
        auto colon = token.find(':');
        if (colon == std::string::npos) continue;
        result[token.substr(0, colon)] = token.substr(colon + 1);
    }
    return result;
}

static PHLWINDOW getFocusedWindow() {
    for (auto& w : g_pCompositor->m_windows) {
        if (w && w->m_isMapped && g_pCompositor->isWindowActive(w))
            return w;
    }
    return nullptr;
}

static PHLWINDOW getWindowByAddress(const std::string& addrStr) {
    try {
        uint64_t addr = std::stoull(addrStr, nullptr, 16);
        for (auto& w : g_pCompositor->m_windows) {
            if (w && (uint64_t)w.get() == addr)
                return w;
        }
    } catch (...) {}
    return nullptr;
}

// glassfx_set "shader:liquid_glass"
// glassfx_set "address:0x55a shader:crt"
SDispatchResult dispatchSet(std::string args) {
    auto kv = parseArgs(args);

    std::string shaderName = kv.count("shader") ? kv["shader"] : "";
    if (shaderName.empty())
        return {false, false, "Usage: glassfx_set shader:<name> [address:<hex>]"};

    PHLWINDOW window;
    if (kv.count("address"))
        window = getWindowByAddress(kv["address"]);
    else
        window = getFocusedWindow();

    if (!window)
        return {false, false, "No window found"};

    if (!g_pShaderSystem || !g_pShaderSystem->getShader(shaderName))
        return {false, false, "Unknown shader: " + shaderName};

    g_pShaderManager->assign(window, shaderName);
    return {};
}

// glassfx_param "shader:liquid_glass param:refraction value:0.6"
SDispatchResult dispatchParam(std::string args) {
    auto kv = parseArgs(args);
    std::string shaderName = kv.count("shader") ? kv["shader"] : "";
    std::string paramName  = kv.count("param")  ? kv["param"]  : "";
    std::string value      = kv.count("value")  ? kv["value"]  : "";

    if (shaderName.empty() || paramName.empty() || value.empty())
        return {false, false, "Usage: glassfx_param shader:<name> param:<name> value:<val>"};

    if (!g_pShaderSystem->setParam(shaderName, paramName, value))
        return {false, false, "Param not found: " + paramName};

    return {};
}

// glassfx_reload
SDispatchResult dispatchReload(std::string) {
    if (g_pShaderSystem)
        g_pShaderSystem->reloadAll();
    return {};
}

// glassfx_clear [address:<hex>]
SDispatchResult dispatchClear(std::string args) {
    auto kv = parseArgs(args);

    PHLWINDOW window;
    if (kv.count("address"))
        window = getWindowByAddress(kv["address"]);
    else
        window = getFocusedWindow();

    if (!window)
        return {false, false, "No window found"};

    g_pShaderManager->remove(window);
    return {};
}

// glassfx_list
SDispatchResult dispatchList(std::string) {
    if (!g_pShaderSystem) return {false, false, "ShaderSystem not initialized"};

    auto shaders = g_pShaderSystem->listShaders();
    std::string out = "GlassFX loaded shaders:\n";
    for (auto& name : shaders) {
        CompiledShader* cs = g_pShaderSystem->getShader(name);
        out += "  " + name + (cs && cs->isFullscreen ? " [fullscreen]" : " [surface]") + "\n";
        if (cs) {
            for (auto& p : cs->params) {
                out += "    " + p.name + " = " + std::to_string(p.val[0]);
                if (p.type == ParamType::VEC2) out += "," + std::to_string(p.val[1]);
                if (p.type == ParamType::VEC3) out += "," + std::to_string(p.val[2]);
                if (p.type == ParamType::COLOR || p.type == ParamType::VEC4)
                    out += "," + std::to_string(p.val[3]);
                out += "\n";
            }
        }
    }
    Log::logger->log(Log::DEBUG, "[GlassFX] {}", out);
    return {true, false, out};
}
