#pragma once

#include <openvr.h>

#include <cstddef>
#include <string>

// One complete tracking sample, suitable for one WebSocket text message.
std::string tracking_state_json(const vr::TrackedDevicePose_t *poses,
                                std::size_t pose_count);
