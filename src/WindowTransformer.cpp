#include "WindowTransformer.hpp"
#include "ShaderSystem.hpp"
#include "ShaderManager.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include <GLES3/gl32.h>
#include <cstring>
#include <hyprland/src/debug/log/Logger.hpp>

GlassFXTransformer::GlassFXTransformer(PHLWINDOW window, const std::string& shaderName)
    : m_window(window), m_shaderName(shaderName) {}

GlassFXTransformer::~GlassFXTransformer() {
    if (m_outFbo) { glDeleteFramebuffers(1, &m_outFbo); m_outFbo = 0; }
    if (m_outTex) { glDeleteTextures(1, &m_outTex); m_outTex = 0; }

    for (int i = 0; i < 2; i++) {
        if (stateTex[i]) { glDeleteTextures(1, &stateTex[i]); stateTex[i] = 0; }
        if (stateFbo[i]) { glDeleteFramebuffers(1, &stateFbo[i]); stateFbo[i] = 0; }
    }
}

void GlassFXTransformer::preWindowRender(CSurfacePassElement::SRenderData* pRenderData) {
    if (!m_window.lock()) return;
    auto win = m_window.lock();
    if (!win) return;
    // Keep focus state updated
    isFocused = g_pCompositor->isWindowActive(win);
}

void GlassFXTransformer::ensureOutput(int w, int h) {
    if (m_outW == w && m_outH == h && m_outFbo != 0) return;
    if (m_outFbo) glDeleteFramebuffers(1, &m_outFbo);
    if (m_outTex) glDeleteTextures(1, &m_outTex);

    glGenTextures(1, &m_outTex);
    glBindTexture(GL_TEXTURE_2D, m_outTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_outFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_outFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_outTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_outW = w;
    m_outH = h;
}

void GlassFXTransformer::ensureStateTextures(int w, int h, const std::string& shader) {
    if (stateInitialized && stateW == w && stateH == h) return;

    for (int i = 0; i < 2; i++) {
        if (stateTex[i]) { glDeleteTextures(1, &stateTex[i]); stateTex[i] = 0; }
        if (stateFbo[i]) { glDeleteFramebuffers(1, &stateFbo[i]); stateFbo[i] = 0; }

        glGenTextures(1, &stateTex[i]);
        glBindTexture(GL_TEXTURE_2D, stateTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &stateFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, stateFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stateTex[i], 0);
    }

    // Seed reaction_diffusion: A=1, B=0 everywhere, small spot of B in center
    if (shader == "reaction_diffusion") {
        std::vector<float> seed(w * h * 4, 0.0f);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int idx = (y * w + x) * 4;
                seed[idx + 0] = 1.0f; // A
                seed[idx + 1] = 0.0f; // B
            }
        // seed center
        int cx = w/2, cy = h/2, r = std::min(w, h)/20;
        for (int y = cy-r; y <= cy+r; y++)
            for (int x = cx-r; x <= cx+r; x++)
                if (x>=0 && x<w && y>=0 && y<h) {
                    int idx = (y*w+x)*4;
                    seed[idx+1] = 1.0f;
                }
        glBindTexture(GL_TEXTURE_2D, stateTex[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, seed.data());
        glBindTexture(GL_TEXTURE_2D, stateTex[1]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, seed.data());
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    stateW = w;
    stateH = h;
    stateInitialized = true;
    pingpong = 0;
}

void GlassFXTransformer::renderQuad(CompiledShader* cs, GLuint inputTexId, GLuint bgTexId, int w, int h) {
    if (!cs || !cs->program) return;

    glUseProgram(cs->program);
    glBindVertexArray(cs->vao);

    // Bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexId);
    if (cs->u_tex >= 0) glUniform1i(cs->u_tex, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bgTexId ? bgTexId : inputTexId);
    if (cs->u_background >= 0) glUniform1i(cs->u_background, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_pShaderSystem ? g_pShaderSystem->noiseTexture() : 0);
    if (cs->u_noise >= 0) glUniform1i(cs->u_noise, 2);

    // Standard uniforms
    if (cs->u_time >= 0)
        glUniform1f(cs->u_time, g_pShaderSystem ? g_pShaderSystem->pluginTime() : 0.0f);
    if (cs->u_alpha >= 0)
        glUniform1f(cs->u_alpha, 1.0f);
    if (cs->u_focused >= 0)
        glUniform1i(cs->u_focused, isFocused ? 1 : 0);
    if (cs->u_resolution >= 0)
        glUniform2f(cs->u_resolution, (float)w, (float)h);
    if (cs->u_surface_size >= 0)
        glUniform2f(cs->u_surface_size, (float)w, (float)h);
    if (cs->u_surface_pos >= 0) {
        auto win = m_window.lock();
        if (win) glUniform2f(cs->u_surface_pos, (float)win->m_position.x, (float)win->m_position.y);
        else     glUniform2f(cs->u_surface_pos, 0.0f, 0.0f);
    }
    if (cs->u_mouse >= 0) {
        auto pos = g_pInputManager->getMouseCoordsInternal();
        glUniform2f(cs->u_mouse, (float)pos.x, (float)pos.y);
    }

    // User params
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
}

SP<Render::IFramebuffer> GlassFXTransformer::transform(SP<Render::IFramebuffer> in) {
    if (!g_pShaderSystem) return in;

    CompiledShader* cs = g_pShaderSystem->getShader(m_shaderName);
    if (!cs || !cs->program) return in;

    auto tex = in->getTexture();
    if (!tex) return in;

    int w = (int)in->m_size.x;
    int h = (int)in->m_size.y;
    if (w <= 0 || h <= 0) return in;

    try {
        ensureOutput(w, h);

        // For stateful shaders, ensure ping-pong textures
        bool isStateful = (m_shaderName == "reaction_diffusion" || m_shaderName == "fluid_sim");
        if (isStateful)
            ensureStateTextures(w, h, m_shaderName);

        glBindFramebuffer(GL_FRAMEBUFFER, m_outFbo);
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);

        GLuint inputTexId = tex->m_texID;
        GLuint bgTexId = 0;

        // Bind state textures for stateful shaders
        if (isStateful) {
            int src = pingpong;

            // Bind state tex to slot 3
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, stateTex[src]);
            GLint stateLoc = glGetUniformLocation(cs->program, "u_state");
            if (stateLoc >= 0) {
                glUseProgram(cs->program);
                glUniform1i(stateLoc, 3);
            }

            // Render to state output
            glBindFramebuffer(GL_FRAMEBUFFER, stateFbo[1 - src]);
            glViewport(0, 0, w, h);
            renderQuad(cs, inputTexId, bgTexId, w, h);

            pingpong = 1 - src;

            // Render visible output to outFbo
            glBindFramebuffer(GL_FRAMEBUFFER, m_outFbo);
            glViewport(0, 0, w, h);

            // For display, use the updated state as "tex"
            renderQuad(cs, stateTex[pingpong], bgTexId, w, h);
        } else {
            renderQuad(cs, inputTexId, bgTexId, w, h);
        }

        // Blit result from m_outFbo back into in's framebuffer
        in->bind(); // sets in's FBO as the current draw framebuffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_outFbo);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glEnable(GL_BLEND);

    } catch (...) {
        Log::logger->log(Log::ERR, "[GlassFX] Exception in transformer for shader: {}", m_shaderName);
    }

    return in;
}
