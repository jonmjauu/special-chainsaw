#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <openvr.h>

#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t interrupted = 0;

void stop_on_signal(int) { interrupted = 1; }

constexpr const char *kPortalName = "org.freedesktop.portal.Desktop";
constexpr const char *kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char *kScreenCast = "org.freedesktop.portal.ScreenCast";

std::string error_message(const char *operation, GError *error) {
    std::string message = std::string(operation) + ": " +
        (error ? error->message : "unknown error");
    if (error) g_error_free(error);
    return message;
}

struct RequestState {
    GMainLoop *loop = nullptr;
    std::string path;
    guint status = 2;
    GVariant *results = nullptr;
};

void on_response(GDBusConnection *, const gchar *, const gchar *object_path,
                 const gchar *, const gchar *, GVariant *parameters, gpointer data) {
    auto &state = *static_cast<RequestState *>(data);
    if (state.path != object_path) return;
    g_variant_get(parameters, "(u@a{sv})", &state.status, &state.results);
    g_main_loop_quit(state.loop);
}

GVariant *portal_request(GDBusConnection *bus, const char *method, GVariant *arguments) {
    RequestState state;
    state.loop = g_main_loop_new(nullptr, FALSE);
    const guint subscription = g_dbus_connection_signal_subscribe(
        bus, kPortalName, "org.freedesktop.portal.Request", "Response", nullptr,
        nullptr, G_DBUS_SIGNAL_FLAGS_NONE, on_response, &state, nullptr);

    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(
        bus, kPortalName, kPortalPath, kScreenCast, method, arguments,
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (!reply) {
        g_dbus_connection_signal_unsubscribe(bus, subscription);
        g_main_loop_unref(state.loop);
        throw std::runtime_error(error_message(method, error));
    }

    const gchar *path = nullptr;
    g_variant_get(reply, "(&o)", &path);
    state.path = path;
    g_variant_unref(reply);
    g_main_loop_run(state.loop);
    g_dbus_connection_signal_unsubscribe(bus, subscription);
    g_main_loop_unref(state.loop);

    if (state.status != 0) {
        if (state.results) g_variant_unref(state.results);
        throw std::runtime_error(std::string(method) +
            (state.status == 1 ? ": window selection cancelled" : ": portal request failed"));
    }
    return state.results; // owned reference
}

GVariant *options_with_token(const char *token) {
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token));
    return g_variant_new("(a{sv})", &options);
}

struct PortalCapture {
    GDBusConnection *bus = nullptr;
    std::string session;
    guint32 node = 0;
    int fd = -1;

    PortalCapture() {
        GError *error = nullptr;
        bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!bus) throw std::runtime_error(error_message("Session bus", error));

        GVariant *results = portal_request(bus, "CreateSession", options_with_token("vr_create"));
        gchar *handle = nullptr;
        if (!g_variant_lookup(results, "session_handle", "o", &handle)) {
            g_variant_unref(results);
            throw std::runtime_error("Portal did not return a session handle");
        }
        session = handle;
        g_free(handle);
        g_variant_unref(results);

        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string("vr_select"));
        g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(2)); // windows
        g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
        results = portal_request(bus, "SelectSources", g_variant_new("(oa{sv})", session.c_str(), &options));
        g_variant_unref(results);

        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string("vr_start"));
        results = portal_request(bus, "Start", g_variant_new("(osa{sv})", session.c_str(), "", &options));
        GVariant *streams = g_variant_lookup_value(results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
        if (!streams || g_variant_n_children(streams) == 0) {
            if (streams) g_variant_unref(streams);
            g_variant_unref(results);
            throw std::runtime_error("Portal returned no video stream");
        }
        GVariant *stream = g_variant_get_child_value(streams, 0);
        GVariant *properties = nullptr;
        g_variant_get(stream, "(u@a{sv})", &node, &properties);
        g_variant_unref(properties);
        g_variant_unref(stream);
        g_variant_unref(streams);
        g_variant_unref(results);

        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        GUnixFDList *fd_list = nullptr;
        GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(
            bus, kPortalName, kPortalPath, kScreenCast, "OpenPipeWireRemote",
            g_variant_new("(oa{sv})", session.c_str(), &options), G_VARIANT_TYPE("(h)"),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &fd_list, nullptr, &error);
        if (!reply) throw std::runtime_error(error_message("OpenPipeWireRemote", error));
        gint fd_index = -1;
        g_variant_get(reply, "(h)", &fd_index);
        g_variant_unref(reply);
        if (!fd_list) throw std::runtime_error("Portal returned no PipeWire file descriptor list");
        fd = g_unix_fd_list_get(fd_list, fd_index, &error);
        g_object_unref(fd_list);
        if (fd < 0) throw std::runtime_error(error_message("PipeWire file descriptor", error));
    }

    ~PortalCapture() {
        if (fd >= 0) close(fd);
        if (bus && !session.empty()) {
            g_dbus_connection_call_sync(bus, kPortalName, session.c_str(),
                "org.freedesktop.portal.Session", "Close", nullptr, nullptr,
                G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        }
        if (bus) g_object_unref(bus);
    }
};

struct VideoPipeline {
    GstElement *pipeline = nullptr;
    GstAppSink *sink = nullptr;

    VideoPipeline(int fd, guint32 node) {
        const std::string description = "pipewiresrc fd=" + std::to_string(fd) +
            " path=" + std::to_string(node) +
            " do-timestamp=true ! videoconvert ! video/x-raw,format=RGBA "
            "! appsink name=frames max-buffers=1 drop=true sync=false";
        GError *error = nullptr;
        pipeline = gst_parse_launch(description.c_str(), &error);
        if (error) {
            const std::string message = error_message("GStreamer pipeline", error);
            if (pipeline) gst_object_unref(pipeline);
            pipeline = nullptr;
            throw std::runtime_error(message);
        }
        if (!pipeline) throw std::runtime_error("Could not create GStreamer pipeline");
        sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline), "frames"));
        if (!sink) throw std::runtime_error("Could not find GStreamer appsink");
        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            throw std::runtime_error("Could not start PipeWire capture");
    }

    ~VideoPipeline() {
        if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
        if (sink) gst_object_unref(sink);
        if (pipeline) gst_object_unref(pipeline);
    }
};

struct Graphics {
    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    GLuint texture = 0;
    int width = 0;
    int height = 0;

    Graphics() {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
            throw std::runtime_error(std::string("SDL: ") + SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        window = SDL_CreateWindow("Window to SteamVR", SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, 320, 180, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error(std::string("SDL window: ") + SDL_GetError());
        context = SDL_GL_CreateContext(window);
        if (!context) throw std::runtime_error(std::string("OpenGL context: ") + SDL_GetError());
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void upload(GstSample *sample) {
        GstCaps *caps = gst_sample_get_caps(sample);
        GstVideoInfo info;
        if (!caps || !gst_video_info_from_caps(&info, caps)) return;
        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) return;

        const int next_width = static_cast<int>(GST_VIDEO_INFO_WIDTH(&info));
        const int next_height = static_cast<int>(GST_VIDEO_INFO_HEIGHT(&info));
        const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
        if (next_width > 0 && next_height > 0 && stride >= next_width * 4) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
            if (width != next_width || height != next_height) {
                width = next_width;
                height = next_height;
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
                std::cerr << "Capture: " << width << "x" << height << "\n";
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                                GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
            }
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
        gst_video_frame_unmap(&frame);
    }

    ~Graphics() {
        if (texture) glDeleteTextures(1, &texture);
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

struct VRSystem {
    VRSystem() {
        vr::EVRInitError error = vr::VRInitError_None;
        vr::VR_Init(&error, vr::VRApplication_Scene);
        if (error != vr::VRInitError_None)
            throw std::runtime_error(std::string("SteamVR: ") + vr::VR_GetVRInitErrorAsEnglishDescription(error));
        if (!vr::VRCompositor()) {
            vr::VR_Shutdown();
            throw std::runtime_error("SteamVR compositor unavailable");
        }
    }
    ~VRSystem() { vr::VR_Shutdown(); }
};

void run() {
    std::cerr << "Choose the stereo window in the portal dialog. Press Ctrl+C to stop.\n";
    PortalCapture capture;
    VideoPipeline video(capture.fd, capture.node);
    Graphics graphics;
    VRSystem vr_system;

    bool running = true;
    while (running && !interrupted) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                running = false;
        }

        GstBus *bus = gst_element_get_bus(video.pipeline);
        GstMessage *message = gst_bus_pop_filtered(bus,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (message) {
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                GError *error = nullptr;
                gchar *debug = nullptr;
                gst_message_parse_error(message, &error, &debug);
                const std::string reason = error_message("Capture", error);
                g_free(debug);
                gst_message_unref(message);
                gst_object_unref(bus);
                throw std::runtime_error(reason);
            }
            gst_message_unref(message);
            gst_object_unref(bus);
            break;
        }
        gst_object_unref(bus);

        // Keep only the newest captured frame to minimize latency.
        GstSample *newest = nullptr;
        while (GstSample *sample = gst_app_sink_try_pull_sample(video.sink, 0)) {
            if (newest) gst_sample_unref(newest);
            newest = sample;
        }
        if (newest) {
            graphics.upload(newest);
            gst_sample_unref(newest);
        }
        if (graphics.width == 0) {
            SDL_Delay(10);
            continue;
        }

        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        const auto pose_error = vr::VRCompositor()->WaitGetPoses(
            poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
        if (pose_error != vr::VRCompositorError_None)
            throw std::runtime_error("SteamVR pose wait failed: " + std::to_string(pose_error));
        glFinish(); // Ensure the compositor sees the completed texture upload.
        vr::Texture_t texture = {
            reinterpret_cast<void *>(static_cast<uintptr_t>(graphics.texture)),
            vr::TextureType_OpenGL, vr::ColorSpace_Gamma};
        // Portal frames are top-down; OpenGL uploads their first row at v=0.
        // Reverse V so the image is upright in the compositor.
        const vr::VRTextureBounds_t left = {0.0f, 1.0f, 0.5f, 0.0f};
        const vr::VRTextureBounds_t right = {0.5f, 1.0f, 1.0f, 0.0f};
        const auto left_error = vr::VRCompositor()->Submit(vr::Eye_Left, &texture, &left);
        const auto right_error = vr::VRCompositor()->Submit(vr::Eye_Right, &texture, &right);
        if (left_error != vr::VRCompositorError_None || right_error != vr::VRCompositorError_None)
            throw std::runtime_error("SteamVR rejected a submitted frame (errors " +
                std::to_string(left_error) + ", " + std::to_string(right_error) + ")");
        vr::VRCompositor()->PostPresentHandoff();
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << "\n";
        return 2;
    }
    std::signal(SIGINT, stop_on_signal);
    std::signal(SIGTERM, stop_on_signal);
    gst_init(&argc, &argv);
    try {
        run();
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
