#include "GlassFXRenderer.hpp"
#include "ScreenCapture.hpp"
#include "../ShaderSystem.hpp"

#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QSocketNotifier>
#include <QQuickWindow>

#include <GLES3/gl32.h>
#include <algorithm>
#include <vector>
#include <cstring>

// Singleton ShaderSystem for the QS plugin.
static ShaderSystem* s_shaderSystem = nullptr;
static QSocketNotifier* s_inotifyNotifier = nullptr;

GlassFXRenderer::GlassFXRenderer() {}

GlassFXRenderer::~GlassFXRenderer() {
    if (m_bgFbo) { glDeleteFramebuffers(1, &m_bgFbo); }
    if (m_bgTex) { glDeleteTextures(1, &m_bgTex); }
    for (int i = 0; i < 2; i++) {
        if (m_stateTex[i]) glDeleteTextures(1, &m_stateTex[i]);
        if (m_stateFbo[i]) glDeleteFramebuffers(1, &m_stateFbo[i]);
    }
}

void GlassFXRenderer::ensureShaderSystem() {
    if (s_shaderSystem) return;

    s_shaderSystem = new ShaderSystem();
    const char* home = getenv("HOME");
    if (home)
        s_shaderSystem->setShaderDir(std::string(home) + "/.config/hypr/shaders/glassfx");

    s_shaderSystem->init();
    s_shaderSystem->startInotify();

    int fd = s_shaderSystem->inotifyFd();
    if (fd >= 0) {
        s_inotifyNotifier = new QSocketNotifier(fd, QSocketNotifier::Read);
        QObject::connect(s_inotifyNotifier, &QSocketNotifier::activated,
            [](QSocketDescriptor, QSocketNotifier::Type) {
                if (s_shaderSystem) s_shaderSystem->onInotifyReadable();
            });
    }

    m_shaderSystem = s_shaderSystem;
}

QOpenGLFramebufferObject* GlassFXRenderer::createFramebufferObject(const QSize& size) {
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    return new QOpenGLFramebufferObject(size, fmt);
}

void GlassFXRenderer::synchronize(QQuickFramebufferObject* fboItem) {
    auto* item = static_cast<GlassFXItem*>(fboItem);
    m_shaderName    = item->shaderName().toStdString();
    m_alpha         = (float)item->alpha();
    m_focused       = item->focused();
    m_mouseX        = (float)item->mousePos().x();
    m_mouseY        = (float)item->mousePos().y();

    // Map item position to screen coordinates
    QPointF screenPos = item->mapToScene({0, 0});
    m_itemX = (float)screenPos.x();
    m_itemY = (float)screenPos.y();
    m_itemW = (float)item->width();
    m_itemH = (float)item->height();

    m_pendingParams = item->takeParamChanges();
}

void GlassFXRenderer::render() {
    ensureShaderSystem();
    if (!s_shaderSystem) return;

    // Apply any pending param changes
    for (auto& pc : m_pendingParams)
        s_shaderSystem->setParam(m_shaderName, pc.name.toStdString(), pc.value.toStdString());
    m_pendingParams.clear();

    // Upload any new screencapture frame
    ScreenCapture::instance()->uploadIfPending();

    GLuint screenTex = ScreenCapture::instance()->glTexture();
    QSize  capSize   = ScreenCapture::instance()->captureSize();
    int    capW      = capSize.width();
    int    capH      = capSize.height();

    // Fall back gracefully if no capture yet
    if (screenTex == 0 || capW <= 0 || capH <= 0) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        update();
        return;
    }

    CompiledShader* cs = s_shaderSystem->getShader(m_shaderName);
    if (!cs || !cs->program) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        update();
        return;
    }

    int w = (int)m_itemW;
    int h = (int)m_itemH;
    if (w <= 0 || h <= 0) { update(); return; }

    ensureBackground(capW, capH);
    s_shaderSystem->blurInto(screenTex, m_bgFbo, m_bgW, m_bgH);

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    bool isStateful = (m_shaderName == "reaction_diffusion" || m_shaderName == "fluid_sim");
    GLint statePassLoc = glGetUniformLocation(cs->program, "u_state_pass");

    if (isStateful) {
        ensureStateTextures(w, h, m_shaderName);
        int src = m_pingpong;

        glUseProgram(cs->program);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_stateTex[src]);
        GLint stateLoc = glGetUniformLocation(cs->program, "u_state");
        if (stateLoc >= 0) glUniform1i(stateLoc, 3);
        if (statePassLoc >= 0) glUniform1i(statePassLoc, 1);

        glBindFramebuffer(GL_FRAMEBUFFER, m_stateFbo[1 - src]);
        glViewport(0, 0, w, h);
        renderQuad(cs, screenTex, m_bgTex, w, h);

        m_pingpong = 1 - src;

        glUseProgram(cs->program);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_stateTex[m_pingpong]);
        if (stateLoc >= 0) glUniform1i(stateLoc, 3);
        if (statePassLoc >= 0) glUniform1i(statePassLoc, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, framebufferObject()->handle());
        glViewport(0, 0, w, h);
        renderQuad(cs, screenTex, m_bgTex, w, h);
    } else {
        if (statePassLoc >= 0) {
            glUseProgram(cs->program);
            glUniform1i(statePassLoc, 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, framebufferObject()->handle());
        glViewport(0, 0, w, h);
        renderQuad(cs, screenTex, m_bgTex, w, h);
    }

    glEnable(GL_BLEND);
    update(); // request next frame
}

void GlassFXRenderer::renderQuad(CompiledShader* cs, GLuint inputTex, GLuint bgTex, int w, int h) {
    if (!cs || !cs->program) return;

    glUseProgram(cs->program);
    glBindVertexArray(cs->vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTex);
    if (cs->u_tex >= 0) glUniform1i(cs->u_tex, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bgTex ? bgTex : inputTex);
    if (cs->u_background >= 0) glUniform1i(cs->u_background, 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, s_shaderSystem->noiseTexture());
    if (cs->u_noise >= 0) glUniform1i(cs->u_noise, 2);

    if (cs->u_time >= 0)
        glUniform1f(cs->u_time, s_shaderSystem->pluginTime());
    if (cs->u_alpha >= 0)
        glUniform1f(cs->u_alpha, m_alpha);
    if (cs->u_focused >= 0)
        glUniform1i(cs->u_focused, m_focused ? 1 : 0);
    if (cs->u_resolution >= 0)
        glUniform2f(cs->u_resolution, (float)w, (float)h);
    if (cs->u_surface_size >= 0)
        glUniform2f(cs->u_surface_size, m_itemW, m_itemH);
    if (cs->u_surface_pos >= 0)
        glUniform2f(cs->u_surface_pos, m_itemX, m_itemY);
    if (cs->u_mouse >= 0)
        glUniform2f(cs->u_mouse, m_mouseX, m_mouseY);

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

void GlassFXRenderer::ensureBackground(int w, int h) {
    int bw = std::max(1, w / 2);
    int bh = std::max(1, h / 2);
    if (m_bgW == bw && m_bgH == bh && m_bgFbo) return;

    if (m_bgFbo) glDeleteFramebuffers(1, &m_bgFbo);
    if (m_bgTex) glDeleteTextures(1, &m_bgTex);

    glGenTextures(1, &m_bgTex);
    glBindTexture(GL_TEXTURE_2D, m_bgTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bw, bh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &m_bgFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bgFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bgTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_bgW = bw;
    m_bgH = bh;
}

void GlassFXRenderer::ensureStateTextures(int w, int h, const std::string& shader) {
    if (m_stateInit && m_stateW == w && m_stateH == h) return;

    for (int i = 0; i < 2; i++) {
        if (m_stateTex[i]) { glDeleteTextures(1, &m_stateTex[i]); m_stateTex[i] = 0; }
        if (m_stateFbo[i]) { glDeleteFramebuffers(1, &m_stateFbo[i]); m_stateFbo[i] = 0; }

        glGenTextures(1, &m_stateTex[i]);
        glBindTexture(GL_TEXTURE_2D, m_stateTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &m_stateFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_stateFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_stateTex[i], 0);
    }

    if (shader == "reaction_diffusion") {
        std::vector<float> seed((size_t)w * h * 4, 0.0f);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                seed[((size_t)y * w + x) * 4 + 0] = 1.0f;
            }
        int cx = w/2, cy = h/2, r = std::min(w, h)/20;
        for (int y = cy-r; y <= cy+r; y++)
            for (int x = cx-r; x <= cx+r; x++)
                if (x >= 0 && x < w && y >= 0 && y < h)
                    seed[((size_t)y * w + x) * 4 + 1] = 1.0f;

        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, m_stateTex[i]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, seed.data());
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_stateW    = w;
    m_stateH    = h;
    m_stateInit = true;
    m_pingpong  = 0;
}
