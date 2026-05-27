#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
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

    // Returns nullptr if not found
    CompiledShader*                          getShader(const std::string& name);
    std::vector<std::string>                 listShaders() const;

    bool                                     setParam(const std::string& shaderName, const std::string& paramName, const std::string& value);

    void                                     reloadAll();

    GLuint                                   noiseTexture() const { return m_noiseTex; }
    float                                    pluginTime() const;

    void                                     startInotify();

  private:
    std::unordered_map<std::string, CompiledShader> m_shaders;
    mutable std::mutex                              m_mutex;

    GLuint m_noiseTex = 0;

    std::string m_userShaderDir;
    std::string m_builtinShaderDir;

    int  m_inotifyFd  = -1;
    int  m_inotifyWd1 = -1;
    int  m_inotifyWd2 = -1;

    struct timespec m_startTime {};

    void        scanDirectory(const std::string& dir);
    bool        loadFile(const std::string& path);
    bool        compileShader(CompiledShader& cs, const std::string& fragSrc);
    void        parseMetadata(CompiledShader& cs, const std::string& src);
    void        cacheUniformLocations(CompiledShader& cs);
    void        buildQuadGeometry(CompiledShader& cs);
    void        generateNoise();
    static void parseParamValue(const std::string& valStr, ParamType type, float* out);
    static std::string stripVersion(const std::string& src);
    void        onInotifyReadable();
    static std::string readFile(const std::string& path);
};

extern ShaderSystem* g_pShaderSystem;
