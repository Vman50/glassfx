#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>
#include <vector>

struct WindowRule {
    std::string shaderName;
    std::string matchClass; // app_id / class
    std::string matchTitle;
};

class GlassFXConfig {
  public:
    void init(HANDLE handle);
    void shutdown();

    std::string fullscreenShader() const;
    bool        desaturateUnfocused() const;
    float       desaturateAmount() const;

    // Returns matching shader name for window, or ""
    std::string matchWindow(const std::string& cls, const std::string& title) const;

    void reloadRules();

  private:
    HANDLE m_handle = nullptr;

    // Raw config string "shader,class;shader,class;..."
    std::string parseWindowrules() const;
    void        rebuildRules();

    std::vector<WindowRule> m_rules;
    mutable std::mutex      m_mutex;
};

extern GlassFXConfig* g_pConfig;
