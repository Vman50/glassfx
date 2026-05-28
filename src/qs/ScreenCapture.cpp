#include "ScreenCapture.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <qguiapplication_platform.h>
#include <qscreen_platform.h>

#include <wayland-client.h>

// Generated Qt Wayland protocol headers (from ext-image-copy-capture-v1.xml
// and ext-image-capture-source-v1.xml via qt6_generate_wayland_protocol_client_sources)
#include "wayland-ext-image-copy-capture-v1-client-protocol.h"
#include "wayland-ext-image-capture-source-v1-client-protocol.h"

#include <GLES3/gl32.h>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

static const wl_registry_listener s_registryListener = {
    ScreenCapture::onRegistryGlobal,
    ScreenCapture::onRegistryGlobalRemove,
};

static const ext_image_copy_capture_session_v1_listener s_sessionListener = {
    ScreenCapture::onSessionBufferSize,
    ScreenCapture::onSessionShmFormat,
    ScreenCapture::onSessionDmabufDevice,
    ScreenCapture::onSessionDmabufFormat,
    ScreenCapture::onSessionDone,
    ScreenCapture::onSessionStopped,
};

static const ext_image_copy_capture_frame_v1_listener s_frameListener = {
    ScreenCapture::onFrameTransform,
    ScreenCapture::onFrameDamage,
    ScreenCapture::onFramePresentationTime,
    ScreenCapture::onFrameReady,
    ScreenCapture::onFrameFailed,
};

ScreenCapture* ScreenCapture::instance() {
    static ScreenCapture* s = new ScreenCapture();
    return s;
}

ScreenCapture::ScreenCapture(QObject* parent) : QObject(parent) {
    // Defer init to first event-loop tick so Qt Wayland platform is fully up.
    QTimer::singleShot(0, this, [this]() { init(); });
}

ScreenCapture::~ScreenCapture() {
    destroySession();
    if (m_registry)  { wl_registry_destroy(m_registry); m_registry = nullptr; }
}

void ScreenCapture::init() {
    auto* waylandApp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!waylandApp) {
        fprintf(stderr, "[GlassFX/QS] Not running on Wayland — screen capture unavailable\n");
        return;
    }
    m_display = waylandApp->display();
    if (!m_display) return;

    // Get wl_output for the primary screen
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        auto* ws = screen->nativeInterface<QNativeInterface::QWaylandScreen>();
        if (ws) m_output = ws->output();
    }

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &s_registryListener, this);
    wl_display_roundtrip(m_display);

    if (!m_captureManager || !m_sourceManager) {
        fprintf(stderr, "[GlassFX/QS] ext-image-copy-capture protocol not available\n");
        return;
    }
    if (!m_shm) {
        fprintf(stderr, "[GlassFX/QS] wl_shm not available\n");
        return;
    }
    if (!m_output) {
        fprintf(stderr, "[GlassFX/QS] No Wayland output found\n");
        return;
    }

    startSession();
}

void ScreenCapture::startSession() {
    m_source = ext_output_image_capture_source_manager_v1_create_source(m_sourceManager, m_output);
    m_session = ext_image_copy_capture_manager_v1_create_session(m_captureManager, m_source, 0);
    ext_image_copy_capture_session_v1_add_listener(m_session, &s_sessionListener, this);
    wl_display_roundtrip(m_display); // get buffer_size + shm_format + done
}

void ScreenCapture::createShmBuffer() {
    if (m_shmBuffer) {
        wl_buffer_destroy(m_shmBuffer);
        m_shmBuffer = nullptr;
    }
    if (m_shmPool) {
        wl_shm_pool_destroy(m_shmPool);
        m_shmPool = nullptr;
    }
    if (m_shmData) {
        munmap(m_shmData, m_shmSize);
        m_shmData = nullptr;
    }

    m_shmSize = (size_t)m_captureW * m_captureH * 4;

    int fd = memfd_create("glassfx-screencopy", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        fprintf(stderr, "[GlassFX/QS] memfd_create failed\n");
        return;
    }
    if (ftruncate(fd, (off_t)m_shmSize) < 0) { close(fd); return; }

    m_shmData = mmap(nullptr, m_shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m_shmData == MAP_FAILED) { m_shmData = nullptr; close(fd); return; }

    m_shmPool   = wl_shm_create_pool(m_shm, fd, (int32_t)m_shmSize);
    m_shmBuffer = wl_shm_pool_create_buffer(m_shmPool, 0,
                                             m_captureW, m_captureH,
                                             m_captureW * 4,
                                             m_shmFormat ? m_shmFormat : WL_SHM_FORMAT_XRGB8888);
    close(fd); // pool keeps its own reference
}

void ScreenCapture::requestFrame() {
    if (m_frameInFlight || !m_session || !m_shmBuffer) return;

    m_frame = ext_image_copy_capture_session_v1_create_frame(m_session);
    ext_image_copy_capture_frame_v1_add_listener(m_frame, &s_frameListener, this);
    ext_image_copy_capture_frame_v1_attach_buffer(m_frame, m_shmBuffer);
    ext_image_copy_capture_frame_v1_damage_buffer(m_frame, 0, 0, m_captureW, m_captureH);
    ext_image_copy_capture_frame_v1_capture(m_frame);
    wl_display_flush(m_display);
    m_frameInFlight = true;
}

void ScreenCapture::destroySession() {
    if (m_frame) {
        ext_image_copy_capture_frame_v1_destroy(m_frame);
        m_frame = nullptr;
    }
    if (m_session) {
        ext_image_copy_capture_session_v1_destroy(m_session);
        m_session = nullptr;
    }
    if (m_source) {
        ext_image_capture_source_v1_destroy(m_source);
        m_source = nullptr;
    }
    if (m_shmBuffer) { wl_buffer_destroy(m_shmBuffer); m_shmBuffer = nullptr; }
    if (m_shmPool)   { wl_shm_pool_destroy(m_shmPool); m_shmPool = nullptr; }
    if (m_shmData)   { munmap(m_shmData, m_shmSize); m_shmData = nullptr; }
}

// ── Registry ─────────────────────────────────────────────────────────────────

void ScreenCapture::onRegistryGlobal(void* data, wl_registry* registry,
                                     uint32_t name, const char* iface, uint32_t version) {
    auto* self = static_cast<ScreenCapture*>(data);
    if (strcmp(iface, "wl_shm") == 0) {
        self->m_shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(iface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
        self->m_registryNameCaptureMgr = name;
        self->m_captureManager = static_cast<ext_image_copy_capture_manager_v1*>(
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface,
                             std::min(version, 1u)));
    } else if (strcmp(iface, ext_output_image_capture_source_manager_v1_interface.name) == 0) {
        self->m_registryNameSourceMgr = name;
        self->m_sourceManager = static_cast<ext_output_image_capture_source_manager_v1*>(
            wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface,
                             std::min(version, 1u)));
    }
}

void ScreenCapture::onRegistryGlobalRemove(void* data, wl_registry*, uint32_t name) {
    auto* self = static_cast<ScreenCapture*>(data);
    if (name == self->m_registryNameCaptureMgr) self->m_captureManager = nullptr;
    if (name == self->m_registryNameSourceMgr)  self->m_sourceManager  = nullptr;
}

// ── Session events ───────────────────────────────────────────────────────────

void ScreenCapture::onSessionBufferSize(void* data, ext_image_copy_capture_session_v1*,
                                        uint32_t w, uint32_t h) {
    auto* self = static_cast<ScreenCapture*>(data);
    self->m_captureW = (int)w;
    self->m_captureH = (int)h;
}

void ScreenCapture::onSessionShmFormat(void* data, ext_image_copy_capture_session_v1*,
                                       uint32_t fmt) {
    auto* self = static_cast<ScreenCapture*>(data);
    // Prefer XRGB8888 (0x00000001) or ARGB8888 (0x00000000); accept first advertised.
    if (!self->m_gotFormat ||
        fmt == WL_SHM_FORMAT_XRGB8888 || fmt == WL_SHM_FORMAT_ARGB8888) {
        self->m_shmFormat = fmt;
        self->m_gotFormat = true;
    }
}

void ScreenCapture::onSessionDmabufDevice(void*, ext_image_copy_capture_session_v1*, wl_array*) {}
void ScreenCapture::onSessionDmabufFormat(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {}

void ScreenCapture::onSessionDone(void* data, ext_image_copy_capture_session_v1*) {
    auto* self = static_cast<ScreenCapture*>(data);
    if (self->m_captureW <= 0 || self->m_captureH <= 0) return;

    self->createShmBuffer();
    self->m_sessionReady = true;

    // Start a 30fps capture timer
    self->m_frameTimer = new QTimer(self);
    self->m_frameTimer->setInterval(33); // ~30fps
    QObject::connect(self->m_frameTimer, &QTimer::timeout, self, [self]() {
        self->requestFrame();
    });
    self->m_frameTimer->start();
    self->requestFrame(); // immediate first frame
}

void ScreenCapture::onSessionStopped(void* data, ext_image_copy_capture_session_v1*) {
    auto* self = static_cast<ScreenCapture*>(data);
    if (self->m_frameTimer) { self->m_frameTimer->stop(); }
    self->m_sessionReady = false;
}

// ── Frame events ─────────────────────────────────────────────────────────────

void ScreenCapture::onFrameTransform(void*, ext_image_copy_capture_frame_v1*, uint32_t) {}
void ScreenCapture::onFrameDamage(void*, ext_image_copy_capture_frame_v1*,
                                  int32_t, int32_t, int32_t, int32_t) {}
void ScreenCapture::onFramePresentationTime(void*, ext_image_copy_capture_frame_v1*,
                                            uint32_t, uint32_t, uint32_t) {}

void ScreenCapture::onFrameReady(void* data, ext_image_copy_capture_frame_v1* frame) {
    auto* self = static_cast<ScreenCapture*>(data);
    self->m_frameInFlight = false;

    if (self->m_shmData && self->m_captureW > 0 && self->m_captureH > 0) {
        std::lock_guard<std::mutex> lock(self->m_mutex);
        size_t sz = (size_t)self->m_captureW * self->m_captureH * 4;
        self->m_pendingPixels.resize(sz);
        memcpy(self->m_pendingPixels.data(), self->m_shmData, sz);
        self->m_pendingW = self->m_captureW;
        self->m_pendingH = self->m_captureH;
        self->m_hasPending.store(true, std::memory_order_release);
    }

    ext_image_copy_capture_frame_v1_destroy(frame);
    if (self->m_frame == frame) self->m_frame = nullptr;
}

void ScreenCapture::onFrameFailed(void* data, ext_image_copy_capture_frame_v1* frame,
                                  uint32_t reason) {
    auto* self = static_cast<ScreenCapture*>(data);
    self->m_frameInFlight = false;
    fprintf(stderr, "[GlassFX/QS] Frame capture failed (reason %u)\n", reason);
    ext_image_copy_capture_frame_v1_destroy(frame);
    if (self->m_frame == frame) self->m_frame = nullptr;
}

// ── Render-thread upload ─────────────────────────────────────────────────────

void ScreenCapture::uploadIfPending() {
    if (!m_hasPending.load(std::memory_order_acquire)) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasPending.load(std::memory_order_relaxed)) return;

    int w = m_pendingW;
    int h = m_pendingH;
    if (w <= 0 || h <= 0) { m_hasPending.store(false); return; }

    if (m_glTex == 0 || m_texW != w || m_texH != h) {
        if (m_glTex) glDeleteTextures(1, &m_glTex);
        glGenTextures(1, &m_glTex);
        glBindTexture(GL_TEXTURE_2D, m_glTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_texW = w;
        m_texH = h;
    } else {
        glBindTexture(GL_TEXTURE_2D, m_glTex);
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                    GL_BGRA_EXT, GL_UNSIGNED_BYTE, m_pendingPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_hasPending.store(false, std::memory_order_release);
}

QSize ScreenCapture::captureSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_texW, m_texH};
}
