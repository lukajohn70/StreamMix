#pragma once
/**
 * sm::PresetManager — Project Session Persistence (.stmix / .vmix XML/JSON)
 *
 * Serializes and restores:
 *   - Production project metadata (resolution, frame rate)
 *   - Inputs (cameras, media files, titles, remote streams, OMT)
 *   - Program & Preview bus assignments
 *   - 8-Channel Overlay state and input bindings
 *   - Audio matrix mixer state (gain, pan, mute, solo, AFV, buses A-G)
 *   - PTZ camera list, addresses, and saved position presets
 */

#include "streammix/context.h"
#include "streammix/ptz_controller.h"
#include <string>
#include <vector>
#include <memory>

namespace sm {

struct PresetInputData {
    uint64_t id = 0;
    std::string name;
    std::string type;
    std::string pathOrAddress;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    bool afv = false;
    uint32_t busRouting = 1; // 1 = Master
};

struct PresetOverlayData {
    int channel = 1;
    uint64_t sourceId = 0;
    bool active = false;
};

struct PresetPTZData {
    uint64_t sourceId = 0;
    std::string name;
    std::string ip;
    uint16_t port = 52381;
    int protocol = 0;
};

struct ProductionPreset {
    std::string name = "Untitled Production";
    int width = 1920;
    int height = 1080;
    double fps = 60.0;
    uint64_t programSourceId = 0;
    uint64_t previewSourceId = 0;
    float masterVolume = 1.0f;
    std::vector<PresetInputData> inputs;
    std::vector<PresetOverlayData> overlays;
    std::vector<PresetPTZData> ptzCameras;
};

class PresetManager {
public:
    static bool saveToFile(const std::string& filePath,
                           const ProductionPreset& preset);

    static bool loadFromFile(const std::string& filePath,
                             ProductionPreset& outPreset);

    static std::string serializeToJson(const ProductionPreset& preset);
    static bool deserializeFromJson(const std::string& jsonStr, ProductionPreset& outPreset);
};

} // namespace sm
