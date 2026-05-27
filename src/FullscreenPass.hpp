#pragma once

#include <hyprland/src/helpers/signal/Signal.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <GLES3/gl32.h>
#include <string>

class FullscreenPass {
  public:
    FullscreenPass();
    ~FullscreenPass();

    void init();
    void shutdown();
    void setShader(const std::string& name);

    // Called on RENDER_POST_WINDOWS stage
    void onRenderStage(eRenderStage stage);

  private:
    std::string m_shaderName;

    GLuint m_captureFbo = 0;
    GLuint m_captureTex = 0;
    int    m_capW = 0, m_capH = 0;

    CHyprSignalListener m_renderStageListener;

    void ensureCapture(int w, int h);
    void renderFullscreen(PHLMONITOR mon);
};

extern FullscreenPass* g_pFullscreenPass;
