#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <string>
#include <unordered_map>
#include <mutex>

class GlassFXTransformer;

struct WindowShaderState {
    std::string shaderName;
    GlassFXTransformer* transformer = nullptr; // non-owning; owned by window
};

class ShaderManager {
  public:
    ShaderManager();
    ~ShaderManager();

    void assign(PHLWINDOW window, const std::string& shaderName);
    void remove(PHLWINDOW window);
    void removeAll();

    // Returns the transformer for a window, or nullptr
    GlassFXTransformer* getTransformer(PHLWINDOW window) const;
    std::string         getShaderName(PHLWINDOW window) const;

    void onWindowOpen(PHLWINDOW window);
    void onWindowClose(PHLWINDOW window);
    void onFocusChange(PHLWINDOW window);

    // Apply windowrules from config to a newly-opened window
    void applyRules(PHLWINDOW window);

    void updateFocusedState();

  private:
    mutable std::mutex                                           m_mutex;
    std::unordered_map<Desktop::View::CWindow*, WindowShaderState> m_states;
};

extern ShaderManager* g_pShaderManager;
