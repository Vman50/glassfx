#include "FullscreenPass.hpp"
#include "ShaderSystem.hpp"
#include "Config.hpp"

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>

#include <GLES3/gl32.h>
#include <hyprland/src/debug/log/Logger.hpp>

FullscreenPass::FullscreenPass() {}

FullscreenPass::~FullscreenPass() {
    shutdown();
}

void FullscreenPass::init() {
    m_renderStageListener = Event::bus()->m_events.render.stage.listen([this](eRenderStage stage) {
        onRenderStage(stage);
    });
}

void FullscreenPass::shutdown() {
    m_renderStageListener = nullptr;
    if (m_captureFbo) { glDeleteFramebuffers(1, &m_captureFbo); m_captureFbo = 0; }
    if (m_captureTex) { glDeleteTextures(1, &m_captureTex); m_captureTex = 0; }
}

void FullscreenPass::setShader(const std::string& name) {
    m_shaderName = name;
}

void FullscreenPass::ensureCapture(int w, int h) {
    if (m_capW == w && m_capH == h && m_captureFbo != 0) return;

    if (m_captureFbo) glDeleteFramebuffers(1, &m_captureFbo);
    if (m_captureTex) glDeleteTextures(1, &m_captureTex);

    glGenTextures(1, &m_captureTex);
    glBindTexture(GL_TEXTURE_2D, m_captureTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_captureFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_captureTex, 0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_capW = w;
    m_capH = h;
}

void FullscreenPass::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_POST_WINDOWS) return;

    // Get shader from config if not set
    if (g_pConfig) {
        std::string cfgShader = g_pConfig->fullscreenShader();
        if (!cfgShader.empty() && cfgShader != "none")
            m_shaderName = cfgShader;
    }

    if (m_shaderName.empty() || m_shaderName == "none") return;
    if (!g_pShaderSystem) return;

    CompiledShader* cs = g_pShaderSystem->getShader(m_shaderName);
    if (!cs || !cs->program) return;
    if (!cs->isFullscreen) return;

    // Get current monitor
    if (!g_pHyprRenderer) return;
    auto& rd = g_pHyprRenderer->m_renderData;
    if (!rd.pMonitor) return;

    auto mon = rd.pMonitor.lock();
    if (!mon) return;

    int w = (int)mon->m_size.x;
    int h = (int)mon->m_size.y;
    if (w <= 0 || h <= 0) return;

    try {
        ensureCapture(w, h);

        // Snapshot whatever framebuffer Hyprland currently has bound,
        // not FB 0 (which is rarely what's in use during the render path).
        GLint prevFb = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFb);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevFb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_captureFbo);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        // Render the shader back into the framebuffer we captured from.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFb);
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);

        glUseProgram(cs->program);
        glBindVertexArray(cs->vao);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_captureTex);
        if (cs->u_tex >= 0) glUniform1i(cs->u_tex, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_captureTex);
        if (cs->u_background >= 0) glUniform1i(cs->u_background, 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, g_pShaderSystem->noiseTexture());
        if (cs->u_noise >= 0) glUniform1i(cs->u_noise, 2);

        if (cs->u_time >= 0)
            glUniform1f(cs->u_time, g_pShaderSystem->pluginTime());
        if (cs->u_alpha >= 0) glUniform1f(cs->u_alpha, 1.0f);
        if (cs->u_focused >= 0) glUniform1i(cs->u_focused, 1);
        if (cs->u_resolution >= 0) glUniform2f(cs->u_resolution, (float)w, (float)h);
        if (cs->u_surface_pos >= 0) glUniform2f(cs->u_surface_pos, 0.0f, 0.0f);
        if (cs->u_surface_size >= 0) glUniform2f(cs->u_surface_size, (float)w, (float)h);

        for (auto& p : cs->params) {
            GLint loc = glGetUniformLocation(cs->program, ("u_" + p.name).c_str());
            if (loc < 0) continue;
            switch (p.type) {
                case ParamType::FLOAT: glUniform1f(loc, p.val[0]); break;
                case ParamType::VEC2:  glUniform2f(loc, p.val[0], p.val[1]); break;
                case ParamType::VEC3:  glUniform3f(loc, p.val[0], p.val[1], p.val[2]); break;
                case ParamType::VEC4:
                case ParamType::COLOR: glUniform4f(loc, p.val[0], p.val[1], p.val[2], p.val[3]); break;
            }
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glBindVertexArray(0);
        glUseProgram(0);
        glEnable(GL_BLEND);

    } catch (...) {
        Log::logger->log(Log::ERR, "[GlassFX] Exception in fullscreen pass for shader: {}", m_shaderName);
    }
}
