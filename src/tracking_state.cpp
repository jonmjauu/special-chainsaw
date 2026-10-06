#include "tracking_state.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {

constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;

struct ControllerAxisCache {
    vr::TrackedDeviceIndex_t device_index = vr::k_unTrackedDeviceIndexInvalid;
    int trigger_axis = -1;
    bool resolved = false;
};

void append_pose(std::ostringstream &out, const vr::TrackedDevicePose_t *pose) {
    if (!pose || !pose->bPoseIsValid) {
        out << "null";
        return;
    }

    const auto &m = pose->mDeviceToAbsoluteTracking.m;
    for (const auto &row : m) {
        for (float value : row) {
            if (!std::isfinite(value)) {
                out << "null";
                return;
            }
        }
    }

    // Yaw about +Y, pitch about +X, roll about +Z (Y-X-Z decomposition).
    const double pitch = std::asin(std::clamp(-static_cast<double>(m[1][2]), -1.0, 1.0));
    const double cos_pitch = std::cos(pitch);
    const double yaw = std::abs(cos_pitch) > 1e-5
        ? std::atan2(m[0][2], m[2][2])
        : std::atan2(-m[2][0], m[0][0]);
    const double roll = std::abs(cos_pitch) > 1e-5
        ? std::atan2(m[1][0], m[1][1]) : 0.0;

    out << "{\"position_m\":{\"x\":" << m[0][3]
        << ",\"y\":" << m[1][3] << ",\"z\":" << m[2][3]
        << "},\"angles_deg\":{\"yaw\":" << yaw * kRadiansToDegrees
        << ",\"pitch\":" << pitch * kRadiansToDegrees
        << ",\"roll\":" << roll * kRadiansToDegrees << "}}";
}

void append_headset(std::ostringstream &out, const vr::TrackedDevicePose_t *poses,
                    std::size_t pose_count) {
    constexpr auto index = vr::k_unTrackedDeviceIndex_Hmd;
    const bool connected = vr::VRSystem()->IsTrackedDeviceConnected(index);
    out << "{\"connected\":" << (connected ? "true" : "false") << ",\"pose\":";
    append_pose(out, connected && index < pose_count ? &poses[index] : nullptr);
    out << '}';
}

void append_controller(std::ostringstream &out, vr::ETrackedControllerRole role,
                       const vr::TrackedDevicePose_t *poses, std::size_t pose_count,
                       ControllerAxisCache &axis_cache) {
    const auto index = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(role);
    const bool connected = index != vr::k_unTrackedDeviceIndexInvalid &&
        vr::VRSystem()->IsTrackedDeviceConnected(index);
    vr::VRControllerState_t state{};
    const bool has_state = connected &&
        vr::VRSystem()->GetControllerState(index, &state, sizeof(state));

    out << "{\"connected\":" << (connected ? "true" : "false")
        << ",\"device_index\":";
    if (connected) out << index;
    else out << "null";
    out << ",\"pose\":";
    append_pose(out, connected && index < pose_count ? &poses[index] : nullptr);

    if (axis_cache.device_index != (connected ? index : vr::k_unTrackedDeviceIndexInvalid)) {
        axis_cache.device_index = connected ? index : vr::k_unTrackedDeviceIndexInvalid;
        axis_cache.trigger_axis = -1;
        axis_cache.resolved = false;
    }
    if (connected && !axis_cache.resolved) {
        axis_cache.resolved = true;
        for (unsigned axis = 0; axis < vr::k_unControllerStateAxisCount; ++axis) {
            const auto property = static_cast<vr::ETrackedDeviceProperty>(
                vr::Prop_Axis0Type_Int32 + axis);
            if (vr::VRSystem()->GetInt32TrackedDeviceProperty(index, property) ==
                vr::k_eControllerAxis_Trigger) {
                axis_cache.trigger_axis = static_cast<int>(axis);
                break;
            }
        }
    }
    const float trigger_value = has_state && axis_cache.trigger_axis >= 0
        ? state.rAxis[axis_cache.trigger_axis].x : 0.0f;
    const bool has_trigger_axis = has_state && axis_cache.trigger_axis >= 0 &&
        std::isfinite(trigger_value);
    const auto pressed = has_state ? state.ulButtonPressed : 0;
    const auto touched = has_state ? state.ulButtonTouched : 0;
    out << ",\"trigger\":{\"value\":";
    if (has_trigger_axis) out << trigger_value;
    else out << "null";
    out << ",\"pressed\":"
        << ((pressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) ? "true" : "false")
        << "},\"grip\":{\"pressed\":"
        << ((pressed & vr::ButtonMaskFromId(vr::k_EButton_Grip)) ? "true" : "false")
        << ",\"touched\":"
        << ((touched & vr::ButtonMaskFromId(vr::k_EButton_Grip)) ? "true" : "false")
        << "}}";
}

} // namespace

std::string tracking_state_json(const vr::TrackedDevicePose_t *poses,
                                std::size_t pose_count, int capture_width,
                                int capture_height) {
    static ControllerAxisCache left_axis;
    static ControllerAxisCache right_axis;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6);
    const auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    out << "{\"timestamp_ms\":" << timestamp_ms
        << ",\"capture\":{\"width_px\":" << capture_width
        << ",\"height_px\":" << capture_height
        << ",\"left_eye_width_px\":" << capture_width / 2
        << ",\"right_eye_width_px\":" << capture_width - capture_width / 2
        << "},\"headset\":";
    append_headset(out, poses, pose_count);
    out << ",\"controllers\":{\"left\":";
    append_controller(out, vr::TrackedControllerRole_LeftHand, poses, pose_count,
                      left_axis);
    out << ",\"right\":";
    append_controller(out, vr::TrackedControllerRole_RightHand, poses, pose_count,
                      right_axis);
    out << "}}";
    return out.str();
}
