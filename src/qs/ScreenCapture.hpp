#pragma once

#include <QObject>
#include <QTimer>
#include <QSize>

#include <wayland-client.h>
#include <GLES3/gl32.h>

#include <mutex>
#include <vector>
#include <atomic>
#include <cstdint>

struct ext_image_copy_capture_manager_v1;
struct ext_image_copy_capture_session_v1;
struct ext_image_copy_capture_frame_v1;
struct ext_output_image_capture_source_manager_v1;
struct ext_image_capture_source_v1;

class ScreenCapture : public QObject {
    Q_OBJECT
public:
    static ScreenCapture* instance();

    // Upload any pending frame data to the GL texture. Call from the render thread.
    void uploadIfPending();

    // GL texture handle. Valid after the first successful capture. Render thread only.
    GLuint glTexture() const { return m_glTex; }

    QSize captureSize() const;

    // C-linkage Wayland callbacks must be public for the static listener structs
    static void onRegistryGlobal(void* data, wl_registry*, uint32_t name,
                                 const char* iface, uint32_t version);
    static void onRegistryGlobalRemove(void* data, wl_registry*, uint32_t name);

    static void onSessionBufferSize(void*, ext_image_copy_capture_session_v1*, uint32_t w, uint32_t h);
    static void onSessionShmFormat(void*, ext_image_copy_capture_session_v1*, uint32_t fmt);
    static void onSessionDmabufDevice(void*, ext_image_copy_capture_session_v1*, wl_array*);
    static void onSessionDmabufFormat(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*);
    static void onSessionDone(void*, ext_image_copy_capture_session_v1*);
    static void onSessionStopped(void*, ext_image_copy_capture_session_v1*);

    static void onFrameTransform(void*, ext_image_copy_capture_frame_v1*, uint32_t);
    static void onFrameDamage(void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t);
    static void onFramePresentationTime(void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t);
    static void onFrameReady(void*, ext_image_copy_capture_frame_v1*);
    static void onFrameFailed(void*, ext_image_copy_capture_frame_v1*, uint32_t);

private:
    explicit ScreenCapture(QObject* parent = nullptr);
    ~ScreenCapture() override;

    void init();
    void startSession();
    void createShmBuffer();
    void requestFrame();
    void destroySession();

    // Wayland objects
    wl_display*    m_display     = nullptr;
    wl_registry*   m_registry    = nullptr;
    wl_shm*        m_shm         = nullptr;
    wl_output*     m_output      = nullptr;

    ext_output_image_capture_source_manager_v1* m_sourceManager  = nullptr;
    ext_image_copy_capture_manager_v1*           m_captureManager = nullptr;
    ext_image_capture_source_v1*                 m_source         = nullptr;
    ext_image_copy_capture_session_v1*           m_session        = nullptr;
    ext_image_copy_capture_frame_v1*             m_frame          = nullptr;

    uint32_t m_registryNameSourceMgr  = 0;
    uint32_t m_registryNameCaptureMgr = 0;

    // SHM buffer
    wl_shm_pool* m_shmPool    = nullptr;
    wl_buffer*   m_shmBuffer  = nullptr;
    void*        m_shmData    = nullptr;
    size_t       m_shmSize    = 0;
    int          m_captureW   = 0;
    int          m_captureH   = 0;
    uint32_t     m_shmFormat  = 0;      // WL_SHM_FORMAT_* advertised by session
    bool         m_gotFormat  = false;
    bool         m_sessionReady = false;
    bool         m_frameInFlight = false;

    // Shared between capture thread (main) and render thread
    mutable std::mutex       m_mutex;
    std::vector<uint8_t>     m_pendingPixels;
    int                      m_pendingW = 0;
    int                      m_pendingH = 0;
    std::atomic<bool>        m_hasPending{false};

    // GL resources (render thread only)
    GLuint m_glTex  = 0;
    int    m_texW   = 0;
    int    m_texH   = 0;

    QTimer* m_frameTimer = nullptr;
};
