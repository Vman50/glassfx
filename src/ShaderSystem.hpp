#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <GLES3/gl32.h>

enum class ParamType { FLOAT, VEC2, VEC3, VEC4, COLOR };

struct ShaderParam {
    std::string name;
    ParamType   type;
    float       val[4] = {};
    float       def[4] = {};
};

struct CompiledShader {
    std::string              name;
    std::string              path;
    bool                     isFullscreen = false;
    GLuint                   program      = 0;
    std::vector<ShaderParam> params;

    // standard uniform locations
    GLint u_tex          = -1;
    GLint u_background   = -1;
    GLint u_noise        = -1;
    GLint u_resolution   = -1;
    GLint u_surface_pos  = -1;
    GLint u_surface_size = -1;
    GLint u_time         = -1;
    GLint u_alpha        = -1;
    GLint u_focused      = -1;
    GLint u_mouse        = -1;

    // quad geometry
    GLuint vao = 0;
    GLuint vbo = 0;
};

class ShaderSystem {
  public:
    ShaderSystem();
    ~ShaderSystem();

    void                                     init();
    void                                     shutdown();

    void                                     setShaderDir(const std::string& dir);
    void                                     setLogCallback(std::function<void(const std::string&)> fn);

    // Returns nullptr if not found
    CompiledShader*                          getShader(const std::string& name);
    std::vector<std::string>                 listShaders() const;

    bool                                     setParam(const std::string& shaderName, const std::string& paramName, const std::string& value);

    void                                     reloadAll();

    GLuint                                   noiseTexture() const { return m_noiseTex; }
    float                                    pluginTime() const;

    // Renders a blurred copy of srcTex into dstFbo at (dstW x dstH).
    void                                     blurInto(GLuint srcTex, GLuint dstFbo, int dstW, int dstH);

    // Sets up the inotify watch; call inotifyFd() afterwards to integrate with an event loop.
    void                                     startInotify();
    int                                      inotifyFd() const { return m_inotifyFd; }
    void                                     onInotifyReadable();

  private:
    std::unordered_map<std::string, CompiledShader> m_shaders;
    mutable std::mutex                              m_mutex;

    GLuint m_noiseTex = 0;

    std::string m_userShaderDir;
    std::function<void(const std::string&)> m_logFn;

    int  m_inotifyFd  = -1;
    int  m_inotifyWd1 = -1;

    // Internal blur program used to populate u_background.
    GLuint m_blurProgram = 0;
    GLuint m_blurVao     = 0;
    GLuint m_blurVbo     = 0;
    GLint  m_blurULoc_tex        = -1;
    GLint  m_blurULoc_resolution = -1;

    struct timespec m_startTime {};

    void        scanDirectory(const std::string& dir);
    bool        loadFile(const std::string& path);
    bool        compileShader(CompiledShader& cs, const std::string& fragSrc);
    void        parseMetadata(CompiledShader& cs, const std::string& src);
    void        cacheUniformLocations(CompiledShader& cs);
    void        buildQuadGeometry(CompiledShader& cs);
    void        generateNoise();
    void        compileBlurProgram();
    void        releaseShader(const std::string& name);
    static void parseParamValue(const std::string& valStr, ParamType type, float* out);
    static std::string stripVersion(const std::string& src);
    static std::string readFile(const std::string& path);
};

extern ShaderSystem* g_pShaderSystem;
