#include "ShaderManager.hpp"
#include "WindowTransformer.hpp"
#include "Config.hpp"
#include "ShaderSystem.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/debug/log/Logger.hpp>

ShaderManager::ShaderManager() {}
ShaderManager::~ShaderManager() { removeAll(); }

void ShaderManager::assign(PHLWINDOW window, const std::string& shaderName) {
    if (!window) return;

    // Remove existing transformer first
    remove(window);

    if (shaderName.empty() || shaderName == "none") return;

    if (!g_pShaderSystem->getShader(shaderName)) {
        Log::logger->log(Log::WARN, "[GlassFX] Unknown shader: {}", shaderName);
        return;
    }

    UP<GlassFXTransformer> transformer = makeUnique<GlassFXTransformer>(window, shaderName);
    GlassFXTransformer* rawPtr = transformer.get();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_states[window.get()] = {shaderName, rawPtr};
    }

    window->m_transformers.emplace_back(std::move(transformer));
    Log::logger->log(Log::DEBUG, "[GlassFX] Assigned shader '{}' to window '{}'", shaderName, window->m_class);
}

void ShaderManager::remove(PHLWINDOW window) {
    if (!window) return;

    GlassFXTransformer* transformer = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_states.find(window.get());
        if (it == m_states.end()) return;
        transformer = it->second.transformer;
        m_states.erase(it);
    }

    if (!transformer) return;

    // Remove from window's transformer list
    auto& tvec = window->m_transformers;
    tvec.erase(
        std::remove_if(tvec.begin(), tvec.end(),
            [transformer](const UP<IWindowTransformer>& t) {
                return t.get() == transformer;
            }),
        tvec.end()
    );
}

void ShaderManager::removeAll() {
    // Collect all windows
    std::vector<Desktop::View::CWindow*> wins;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [w, _] : m_states)
            wins.push_back(w);
    }

    for (auto* w : wins) {
        // Find the PHLWINDOW
        for (auto& win : g_pCompositor->m_windows) {
            if (win.get() == w) {
                remove(win);
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.clear();
}

GlassFXTransformer* ShaderManager::getTransformer(PHLWINDOW window) const {
    if (!window) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(window.get());
    if (it == m_states.end()) return nullptr;
    return it->second.transformer;
}

std::string ShaderManager::getShaderName(PHLWINDOW window) const {
    if (!window) return "";
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(window.get());
    if (it == m_states.end()) return "";
    return it->second.shaderName;
}

void ShaderManager::onWindowOpen(PHLWINDOW window) {
    applyRules(window);
}

void ShaderManager::onWindowClose(PHLWINDOW window) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.erase(window.get());
    // Transformer is owned by the window and will be destroyed with it
}

void ShaderManager::onFocusChange(PHLWINDOW window) {
    updateFocusedState();

    // Handle desaturate_unfocused
    if (!g_pConfig || !g_pConfig->desaturateUnfocused()) return;

    float amount = g_pConfig->desaturateAmount();
    for (auto& win : g_pCompositor->m_windows) {
        if (!win || !win->m_isMapped) continue;
        bool focused = (win == window);
        GlassFXTransformer* t = getTransformer(win);

        if (!focused) {
            // Check if already has desaturate
            std::string cur = getShaderName(win);
            if (cur.empty() || cur == "none") {
                // Assign desaturate with current amount
                if (g_pShaderSystem->getShader("desaturate")) {
                    assign(win, "desaturate");
                    g_pShaderSystem->setParam("desaturate", "amount", std::to_string(amount));
                }
            }
        } else {
            // Remove desaturate if it was auto-added (not from rules)
            std::string cur = getShaderName(win);
            if (cur == "desaturate") {
                std::string ruleShader = g_pConfig->matchWindow(win->m_class, win->m_title);
                if (ruleShader.empty() || ruleShader == "desaturate") {
                    assign(win, ruleShader);
                }
            }
        }
    }
}

void ShaderManager::updateFocusedState() {
    // Collect (transformer, window) pairs without holding the lock while calling compositor
    std::vector<std::pair<GlassFXTransformer*, Desktop::View::CWindow*>> pairs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [wptr, state] : m_states)
            if (state.transformer) pairs.emplace_back(state.transformer, wptr);
    }
    for (auto& [tf, wptr] : pairs) {
        for (auto& w : g_pCompositor->m_windows) {
            if (w.get() == wptr) {
                tf->isFocused = g_pCompositor->isWindowActive(w);
                break;
            }
        }
    }
}

void ShaderManager::applyRules(PHLWINDOW window) {
    if (!window || !g_pConfig) return;
    std::string shader = g_pConfig->matchWindow(window->m_class, window->m_title);
    if (!shader.empty() && shader != "none")
        assign(window, shader);
}
