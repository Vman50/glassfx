#include "Config.hpp"
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <sstream>
#include <algorithm>
#include <hyprland/src/debug/log/Logger.hpp>

// We store config values as raw SP<> to keep them alive
static SP<Config::Values::CStringValue> s_fullscreenShader;
static SP<Config::Values::CIntValue>    s_desaturateUnfocused;
static SP<Config::Values::CFloatValue>  s_desaturateAmount;
// windowrules stored as a semicolon-separated string: "shader,class;shader,class;..."
static SP<Config::Values::CStringValue> s_windowrules;

void GlassFXConfig::init(HANDLE handle) {
    m_handle = handle;

    s_fullscreenShader = makeShared<Config::Values::CStringValue>(
        "plugin:glassfx:fullscreen_shader",
        "Fullscreen post-process shader (none to disable)",
        "none"
    );
    HyprlandAPI::addConfigValueV2(handle, s_fullscreenShader);

    s_desaturateUnfocused = makeShared<Config::Values::CIntValue>(
        "plugin:glassfx:desaturate_unfocused",
        "Desaturate unfocused windows (0/1)",
        0
    );
    HyprlandAPI::addConfigValueV2(handle, s_desaturateUnfocused);

    s_desaturateAmount = makeShared<Config::Values::CFloatValue>(
        "plugin:glassfx:desaturate_amount",
        "Desaturation amount for unfocused windows (0.0-1.0)",
        0.6f
    );
    HyprlandAPI::addConfigValueV2(handle, s_desaturateAmount);

    s_windowrules = makeShared<Config::Values::CStringValue>(
        "plugin:glassfx:windowrules",
        "Window shader rules: semicolon-separated 'shader,class_pattern' entries",
        ""
    );
    HyprlandAPI::addConfigValueV2(handle, s_windowrules);

    rebuildRules();
}

void GlassFXConfig::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rules.clear();
}

void GlassFXConfig::reloadRules() {
    rebuildRules();
}

std::string GlassFXConfig::fullscreenShader() const {
    if (!s_fullscreenShader) return "none";
    return std::string(s_fullscreenShader->value());
}

bool GlassFXConfig::desaturateUnfocused() const {
    if (!s_desaturateUnfocused) return false;
    return s_desaturateUnfocused->value() != 0;
}

float GlassFXConfig::desaturateAmount() const {
    if (!s_desaturateAmount) return 0.6f;
    return s_desaturateAmount->value();
}

std::string GlassFXConfig::matchWindow(const std::string& cls, const std::string& title) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& rule : m_rules) {
        bool classMatch = rule.matchClass.empty() ||
                          cls.find(rule.matchClass) != std::string::npos;
        bool titleMatch = rule.matchTitle.empty() ||
                          title.find(rule.matchTitle) != std::string::npos;
        if (classMatch && titleMatch)
            return rule.shaderName;
    }
    return "";
}

void GlassFXConfig::rebuildRules() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rules.clear();

    if (!s_windowrules) return;
    std::string raw = std::string(s_windowrules->value());
    if (raw.empty()) return;

    // Format: "shader,class_pattern;shader,class_pattern;..."
    std::istringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ';')) {
        auto comma = token.find(',');
        if (comma == std::string::npos) continue;
        WindowRule rule;
        rule.shaderName  = token.substr(0, comma);
        rule.matchClass  = token.substr(comma + 1);
        // trim whitespace
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(rule.shaderName);
        trim(rule.matchClass);
        if (!rule.shaderName.empty())
            m_rules.push_back(rule);
    }
}
