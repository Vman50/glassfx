#include "ShaderSystem.hpp"
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprutils/os/FileDescriptor.hpp>

#include <GLES3/gl32.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <sys/inotify.h>
#include <ctime>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unistd.h>
#include <hyprland/src/debug/log/Logger.hpp>

namespace fs = std::filesystem;

static const char* VERT_SRC = R"GLSL(
#version 300 es
precision highp float;
in vec2 a_pos;
in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)GLSL";

static const char* PASSTHROUGH_FRAG = R"GLSL(
#version 300 es
precision highp float;
uniform sampler2D u_tex;
in vec2 v_uv;
out vec4 fragColor;
void main() {
    fragColor = texture(u_tex, v_uv);
}
)GLSL";

ShaderSystem::ShaderSystem() {
    clock_gettime(CLOCK_MONOTONIC, &m_startTime);

    const char* home = getenv("HOME");
    m_userShaderDir   = home ? std::string(home) + "/.config/hypr/shaders/glassfx" : "";
}

ShaderSystem::~ShaderSystem() {
    shutdown();
}

void ShaderSystem::init() {
    generateNoise();
    compileBlurProgram();

    if (!m_userShaderDir.empty())
        fs::create_directories(m_userShaderDir);

    if (!m_userShaderDir.empty())
        scanDirectory(m_userShaderDir);
}

void ShaderSystem::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& [name, cs] : m_shaders) {
        if (cs.program) { glDeleteProgram(cs.program); cs.program = 0; }
        if (cs.vao)     { glDeleteVertexArrays(1, &cs.vao); cs.vao = 0; }
        if (cs.vbo)     { glDeleteBuffers(1, &cs.vbo); cs.vbo = 0; }
    }
    m_shaders.clear();

    if (m_noiseTex) { glDeleteTextures(1, &m_noiseTex); m_noiseTex = 0; }

    if (m_blurProgram) { glDeleteProgram(m_blurProgram); m_blurProgram = 0; }
    if (m_blurVao)     { glDeleteVertexArrays(1, &m_blurVao); m_blurVao = 0; }
    if (m_blurVbo)     { glDeleteBuffers(1, &m_blurVbo); m_blurVbo = 0; }

    // m_inotifyFd is owned by the EventLoop's CFileDescriptor after startInotify();
    // do not close() it here or we'll double-close.
    m_inotifyFd = -1;
}

void ShaderSystem::startInotify() {
    if (m_userShaderDir.empty()) return;

    m_inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotifyFd < 0) return;

    m_inotifyWd1 = inotify_add_watch(m_inotifyFd, m_userShaderDir.c_str(),
                                      IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);

    Hyprutils::OS::CFileDescriptor fd(m_inotifyFd);
    g_pEventLoopManager->doOnReadable(std::move(fd), [this]() { onInotifyReadable(); });
}

void ShaderSystem::onInotifyReadable() {
    char buf[4096];
    while (true) {
        ssize_t n = read(m_inotifyFd, buf, sizeof(buf));
        if (n <= 0) break;
        const inotify_event* ev = reinterpret_cast<const inotify_event*>(buf);
        for (ssize_t i = 0; i < n; ) {
            ev = reinterpret_cast<const inotify_event*>(buf + i);
            if (ev->len > 0) {
                std::string name(ev->name);
                if (name.size() > 5 && name.substr(name.size()-5) == ".frag") {
                    std::string path = m_userShaderDir + "/" + name;
                    Log::logger->log(Log::DEBUG, "[GlassFX] Hot-reloading: {}", path);
                    loadFile(path);
                }
            }
            i += sizeof(inotify_event) + ev->len;
        }
    }
}

void ShaderSystem::scanDirectory(const std::string& dir) {
    if (!fs::exists(dir)) return;
    for (auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".frag")
            loadFile(entry.path().string());
    }
}

std::string ShaderSystem::readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ShaderSystem::loadFile(const std::string& path) {
    std::string src = readFile(path);
    if (src.empty()) {
        Log::logger->log(Log::ERR, "[GlassFX] Could not read: {}", path);
        return false;
    }

    CompiledShader cs;
    cs.path = path;
    parseMetadata(cs, src);

    if (cs.name.empty()) {
        cs.name = fs::path(path).stem().string();
    }

    if (!compileShader(cs, src)) {
        Log::logger->log(Log::ERR, "[GlassFX] Shader compile failed: {}", path);
        // Install passthrough fallback
        CompiledShader fb;
        fb.path = path;
        fb.name = cs.name;
        fb.isFullscreen = cs.isFullscreen;
        fb.params = cs.params;
        if (!compileShader(fb, PASSTHROUGH_FRAG)) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        releaseShader(fb.name);
        m_shaders[fb.name] = std::move(fb);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    releaseShader(cs.name);
    m_shaders[cs.name] = std::move(cs);
    Log::logger->log(Log::DEBUG, "[GlassFX] Loaded shader: {}", m_shaders[cs.name].name);
    return true;
}

void ShaderSystem::releaseShader(const std::string& name) {
    auto it = m_shaders.find(name);
    if (it == m_shaders.end()) return;
    auto& cs = it->second;
    if (cs.program) { glDeleteProgram(cs.program); cs.program = 0; }
    if (cs.vao)     { glDeleteVertexArrays(1, &cs.vao); cs.vao = 0; }
    if (cs.vbo)     { glDeleteBuffers(1, &cs.vbo); cs.vbo = 0; }
}

void ShaderSystem::parseMetadata(CompiledShader& cs, const std::string& src) {
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) {
        // Stop parsing metadata after the first non-comment, non-empty line
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
            trimmed = trimmed.substr(1);
        if (!trimmed.empty() && trimmed[0] != '/' && trimmed[0] != '#')
            break;

        auto extract = [&](const std::string& tag) -> std::string {
            auto pos = line.find(tag);
            if (pos == std::string::npos) return "";
            std::string val = line.substr(pos + tag.size());
            // trim
            while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val = val.substr(1);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r'))
                val.pop_back();
            return val;
        };

        if (line.find("@name") != std::string::npos)
            cs.name = extract("@name");
        else if (line.find("@pass") != std::string::npos) {
            std::string pass = extract("@pass");
            cs.isFullscreen = (pass == "fullscreen");
        } else if (line.find("@params") != std::string::npos) {
            std::string paramsStr = extract("@params");
            // parse "key=val key=val ..."
            std::istringstream ps(paramsStr);
            std::string token;
            while (ps >> token) {
                auto eq = token.find('=');
                if (eq == std::string::npos) continue;
                ShaderParam p;
                p.name = token.substr(0, eq);
                std::string valStr = token.substr(eq + 1);

                // Determine type by value format
                if (valStr[0] == '#') {
                    p.type = ParamType::COLOR;
                } else {
                    // count commas
                    int commas = (int)std::count(valStr.begin(), valStr.end(), ',');
                    if (commas == 0) p.type = ParamType::FLOAT;
                    else if (commas == 1) p.type = ParamType::VEC2;
                    else if (commas == 2) p.type = ParamType::VEC3;
                    else p.type = ParamType::VEC4;
                }
                parseParamValue(valStr, p.type, p.def);
                std::copy(p.def, p.def + 4, p.val);
                cs.params.push_back(p);
            }
        }
    }
}

void ShaderSystem::parseParamValue(const std::string& v, ParamType type, float* out) {
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (type == ParamType::COLOR) {
        // Accept #rgb, #rgba, #rrggbb, #rrggbbaa
        if (v.empty() || v[0] != '#') return;
        std::string hex = v.substr(1);
        std::string norm;
        if (hex.size() == 3 || hex.size() == 4) {
            for (char c : hex) { norm += c; norm += c; }
        } else {
            norm = hex;
        }
        if (norm.size() < 8) norm.append(8 - norm.size(), 'f'); // default alpha=0xff
        try {
            unsigned long val = std::stoul(norm.substr(0, 8), nullptr, 16);
            out[0] = ((val >> 24) & 0xff) / 255.0f;
            out[1] = ((val >> 16) & 0xff) / 255.0f;
            out[2] = ((val >>  8) & 0xff) / 255.0f;
            out[3] = ((val >>  0) & 0xff) / 255.0f;
        } catch (...) {}
        return;
    }
    std::istringstream ss(v);
    std::string token;
    int i = 0;
    while (std::getline(ss, token, ',') && i < 4) {
        try { out[i++] = std::stof(token); }
        catch (...) {}
    }
}

std::string ShaderSystem::stripVersion(const std::string& src) {
    std::istringstream ss(src);
    std::string result, line;
    while (std::getline(ss, line)) {
        std::string t = line;
        while (!t.empty() && (t[0]==' '||t[0]=='\t')) t=t.substr(1);
        if (t.substr(0, 8) == "#version") continue;
        result += line + "\n";
    }
    return result;
}

bool ShaderSystem::compileShader(CompiledShader& cs, const std::string& fragSrc) {
    // Compile vertex
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    const char* vSrc = VERT_SRC;
    glShaderSource(vert, 1, &vSrc, nullptr);
    glCompileShader(vert);
    GLint ok = 0;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(vert, sizeof(log), nullptr, log);
        Log::logger->log(Log::ERR, "[GlassFX] Vertex compile error: {}", log);
        glDeleteShader(vert);
        return false;
    }

    // Inject standard uniforms preamble into fragment shader
    std::string preamble = R"GLSL(
#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform sampler2D u_background;
uniform sampler2D u_noise;
uniform vec2 u_resolution;
uniform vec2 u_surface_pos;
uniform vec2 u_surface_size;
uniform float u_time;
uniform float u_alpha;
uniform int u_focused;
uniform vec2 u_mouse;
uniform int u_state_pass;
in vec2 v_uv;
out vec4 fragColor;
)GLSL";

    // Add user @param uniforms
    for (auto& p : cs.params) {
        switch (p.type) {
            case ParamType::FLOAT:  preamble += "uniform float u_" + p.name + ";\n"; break;
            case ParamType::VEC2:   preamble += "uniform vec2 u_" + p.name + ";\n"; break;
            case ParamType::VEC3:   preamble += "uniform vec3 u_" + p.name + ";\n"; break;
            case ParamType::VEC4:
            case ParamType::COLOR:  preamble += "uniform vec4 u_" + p.name + ";\n"; break;
        }
    }

    std::string cleanFrag = stripVersion(fragSrc);
    std::string fullFrag = preamble + cleanFrag;

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fSrc = fullFrag.c_str();
    glShaderSource(frag, 1, &fSrc, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(frag, sizeof(log), nullptr, log);
        Log::logger->log(Log::ERR, "[GlassFX] Fragment compile error in {}: {}", cs.path, log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vert);
    glDeleteShader(frag);

    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        Log::logger->log(Log::ERR, "[GlassFX] Link error: {}", log);
        glDeleteProgram(prog);
        return false;
    }

    if (cs.program) {
        glDeleteProgram(cs.program);
        if (cs.vao) { glDeleteVertexArrays(1, &cs.vao); cs.vao = 0; }
        if (cs.vbo) { glDeleteBuffers(1, &cs.vbo); cs.vbo = 0; }
    }

    cs.program = prog;
    cacheUniformLocations(cs);
    buildQuadGeometry(cs);
    return true;
}

void ShaderSystem::cacheUniformLocations(CompiledShader& cs) {
    cs.u_tex          = glGetUniformLocation(cs.program, "u_tex");
    cs.u_background   = glGetUniformLocation(cs.program, "u_background");
    cs.u_noise        = glGetUniformLocation(cs.program, "u_noise");
    cs.u_resolution   = glGetUniformLocation(cs.program, "u_resolution");
    cs.u_surface_pos  = glGetUniformLocation(cs.program, "u_surface_pos");
    cs.u_surface_size = glGetUniformLocation(cs.program, "u_surface_size");
    cs.u_time         = glGetUniformLocation(cs.program, "u_time");
    cs.u_alpha        = glGetUniformLocation(cs.program, "u_alpha");
    cs.u_focused      = glGetUniformLocation(cs.program, "u_focused");
    cs.u_mouse        = glGetUniformLocation(cs.program, "u_mouse");

    for (auto& p : cs.params) {
        std::string uname = "u_" + p.name;
        // store location in a temporary map via name lookup
        // We'll just do runtime lookup in render – already cheap
        (void)glGetUniformLocation(cs.program, uname.c_str());
    }
}

void ShaderSystem::buildQuadGeometry(CompiledShader& cs) {
    // NDC quad: positions in [-1,1], uvs in [0,1]
    float verts[] = {
        -1.0f,  1.0f,  0.0f, 0.0f,
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
    };
    glGenVertexArrays(1, &cs.vao);
    glGenBuffers(1, &cs.vbo);
    glBindVertexArray(cs.vao);
    glBindBuffer(GL_ARRAY_BUFFER, cs.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ShaderSystem::compileBlurProgram() {
    static const char* BLUR_FRAG = R"GLSL(
#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform vec2 u_resolution;
in vec2 v_uv;
out vec4 fragColor;
void main() {
    vec2 px = 1.0 / u_resolution;
    vec3 sum = vec3(0.0);
    // 13-tap separable-equivalent Gaussian via dual-bilinear taps
    const float w0 = 0.227027;
    const float w1 = 0.316216;
    const float w2 = 0.070270;
    sum += texture(u_tex, v_uv).rgb * w0;
    sum += texture(u_tex, v_uv + vec2( 1.3846 * px.x, 0.0)).rgb * w1;
    sum += texture(u_tex, v_uv + vec2(-1.3846 * px.x, 0.0)).rgb * w1;
    sum += texture(u_tex, v_uv + vec2( 3.2308 * px.x, 0.0)).rgb * w2;
    sum += texture(u_tex, v_uv + vec2(-3.2308 * px.x, 0.0)).rgb * w2;
    sum += texture(u_tex, v_uv + vec2(0.0,  1.3846 * px.y)).rgb * w1;
    sum += texture(u_tex, v_uv + vec2(0.0, -1.3846 * px.y)).rgb * w1;
    sum += texture(u_tex, v_uv + vec2(0.0,  3.2308 * px.y)).rgb * w2;
    sum += texture(u_tex, v_uv + vec2(0.0, -3.2308 * px.y)).rgb * w2;
    // weights above sum to ~1.76; normalize
    fragColor = vec4(sum / 1.760216, 1.0);
}
)GLSL";

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &VERT_SRC, nullptr);
    glCompileShader(vert);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &BLUR_FRAG, nullptr);
    glCompileShader(frag);

    m_blurProgram = glCreateProgram();
    glAttachShader(m_blurProgram, vert);
    glAttachShader(m_blurProgram, frag);
    glBindAttribLocation(m_blurProgram, 0, "a_pos");
    glBindAttribLocation(m_blurProgram, 1, "a_uv");
    glLinkProgram(m_blurProgram);

    GLint ok = 0;
    glGetProgramiv(m_blurProgram, GL_LINK_STATUS, &ok);
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(m_blurProgram, sizeof(log), nullptr, log);
        Log::logger->log(Log::ERR, "[GlassFX] Blur program link error: {}", log);
        glDeleteProgram(m_blurProgram);
        m_blurProgram = 0;
        return;
    }

    m_blurULoc_tex        = glGetUniformLocation(m_blurProgram, "u_tex");
    m_blurULoc_resolution = glGetUniformLocation(m_blurProgram, "u_resolution");

    float verts[] = {
        -1.0f,  1.0f,  0.0f, 0.0f,
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
    };
    glGenVertexArrays(1, &m_blurVao);
    glGenBuffers(1, &m_blurVbo);
    glBindVertexArray(m_blurVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_blurVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ShaderSystem::blurInto(GLuint srcTex, GLuint dstFbo, int dstW, int dstH) {
    if (!m_blurProgram || !srcTex || !dstFbo) return;
    glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
    glViewport(0, 0, dstW, dstH);
    glDisable(GL_BLEND);
    glUseProgram(m_blurProgram);
    glBindVertexArray(m_blurVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    if (m_blurULoc_tex >= 0)        glUniform1i(m_blurULoc_tex, 0);
    if (m_blurULoc_resolution >= 0) glUniform2f(m_blurULoc_resolution, (float)dstW, (float)dstH);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

void ShaderSystem::generateNoise() {
    // Void-and-cluster-inspired blue noise: use interleaved gradient noise as fallback
    constexpr int SZ = 256;
    std::vector<uint8_t> data(SZ * SZ);
    for (int y = 0; y < SZ; y++) {
        for (int x = 0; x < SZ; x++) {
            // Interleaved gradient noise
            float v = std::fmod(52.9829189f * std::fmod(0.06711056f * float(x) + 0.00583715f * float(y), 1.0f), 1.0f);
            data[y * SZ + x] = (uint8_t)(v * 255.0f);
        }
    }
    glGenTextures(1, &m_noiseTex);
    glBindTexture(GL_TEXTURE_2D, m_noiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SZ, SZ, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float ShaderSystem::pluginTime() const {
    struct timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    double sec = (now.tv_sec - m_startTime.tv_sec) + (now.tv_nsec - m_startTime.tv_nsec) * 1e-9;
    return (float)sec;
}

CompiledShader* ShaderSystem::getShader(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_shaders.find(name);
    if (it == m_shaders.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> ShaderSystem::listShaders() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    for (auto& [name, _] : m_shaders)
        result.push_back(name);
    return result;
}

bool ShaderSystem::setParam(const std::string& shaderName, const std::string& paramName, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_shaders.find(shaderName);
    if (it == m_shaders.end()) return false;
    for (auto& p : it->second.params) {
        if (p.name == paramName) {
            parseParamValue(value, p.type, p.val);
            return true;
        }
    }
    return false;
}

void ShaderSystem::reloadAll() {
    std::vector<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [name, cs] : m_shaders)
            paths.push_back(cs.path);
    }
    for (auto& path : paths)
        loadFile(path);
}
