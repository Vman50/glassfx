#pragma once

#include <hyprland/src/render/Transformer.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <string>
#include <GLES3/gl32.h>

struct CompiledShader;

class GlassFXTransformer : public IWindowTransformer {
  public:
    GlassFXTransformer(PHLWINDOW window, const std::string& shaderName);
    ~GlassFXTransformer() override;

    SP<Render::IFramebuffer> transform(SP<Render::IFramebuffer> in) override;
    void preWindowRender(CSurfacePassElement::SRenderData* pRenderData) override;

    const std::string& shaderName() const { return m_shaderName; }

    // per-window stateful textures (reaction_diffusion, fluid_sim)
    GLuint stateTex[2] = {0, 0};
    GLuint stateFbo[2] = {0, 0};
    int    stateW = 0, stateH = 0;
    int    pingpong = 0;
    bool   stateInitialized = false;
    bool   isFocused = false;

  private:
    WP<Desktop::View::CWindow> m_window;
    std::string                m_shaderName;

    // output framebuffer (reused, resized as needed)
    GLuint m_outFbo = 0;
    GLuint m_outTex = 0;
    int    m_outW = 0, m_outH = 0;

    // half-resolution blur of input texture, exposed as u_background
    GLuint m_bgFbo  = 0;
    GLuint m_bgTex  = 0;
    int    m_bgW    = 0, m_bgH = 0;

    void ensureOutput(int w, int h);
    void ensureBackground(int w, int h);
    void ensureStateTextures(int w, int h, const std::string& shader);
    void renderQuad(CompiledShader* cs, GLuint inputTexId, GLuint bgTexId, int w, int h);
};
