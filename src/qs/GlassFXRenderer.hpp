#pragma once

#include <QQuickFramebufferObject>
#include <QSizeF>
#include <QPointF>
#include <QString>

#include <GLES3/gl32.h>
#include <string>

#include "GlassFXItem.hpp"

class ShaderSystem;
struct CompiledShader;

class GlassFXRenderer : public QQuickFramebufferObject::Renderer {
public:
    GlassFXRenderer();
    ~GlassFXRenderer() override;

    void render() override;
    void synchronize(QQuickFramebufferObject* item) override;
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override;

private:
    void ensureShaderSystem();
    void ensureBackground(int w, int h);
    void ensureStateTextures(int w, int h, const std::string& shader);
    void renderQuad(CompiledShader* cs, GLuint inputTex, GLuint bgTex, int w, int h);

    ShaderSystem* m_shaderSystem = nullptr;

    // Blur FBO for u_background
    GLuint m_bgFbo = 0, m_bgTex = 0;
    int    m_bgW   = 0, m_bgH   = 0;

    // Ping-pong state for reaction_diffusion / fluid_sim
    GLuint m_stateTex[2] = {0, 0};
    GLuint m_stateFbo[2] = {0, 0};
    int    m_stateW = 0, m_stateH = 0;
    bool   m_stateInit = false;
    int    m_pingpong  = 0;

    // Synced from GlassFXItem
    std::string m_shaderName;
    float       m_alpha    = 1.0f;
    bool        m_focused  = false;
    float       m_mouseX   = 0.0f;
    float       m_mouseY   = 0.0f;
    float       m_itemX    = 0.0f;
    float       m_itemY    = 0.0f;
    float       m_itemW    = 0.0f;
    float       m_itemH    = 0.0f;
    QList<GlassFXItem::ParamChange> m_pendingParams;
};
