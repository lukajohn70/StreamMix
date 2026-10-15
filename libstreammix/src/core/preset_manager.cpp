/**
 * sm::PresetManager — Implementation
 *
 * Implements robust JSON serialization & deserialization for .stmix
 * project preset files without external heavy library dependencies.
 */

#include "streammix/preset_manager.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace sm {

static std::string escapeJson(const std::string& str) {
    std::ostringstream ss;
    for (char c : str) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\n') ss << "\\n";
        else if (c == '\r') ss << "\\r";
        else if (c == '\t') ss << "\\t";
        else ss << c;
    }
    return ss.str();
}

static std::string extractJsonString(const std::string& block, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    size_t pos = block.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t' || block[pos] == '\n')) pos++;
    if (pos < block.size() && block[pos] == '"') {
        pos++;
        size_t endPos = block.find('"', pos);
        if (endPos != std::string::npos) {
            return block.substr(pos, endPos - pos);
        }
    }
    return "";
}

static double extractJsonNumber(const std::string& block, const std::string& key, double defaultVal = 0.0) {
    std::string needle = "\"" + key + "\":";
    size_t pos = block.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos += needle.size();
    while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t' || block[pos] == '\n')) pos++;
    size_t endPos = pos;
    while (endPos < block.size() && (std::isdigit(block[endPos]) || block[endPos] == '.' || block[endPos] == '-')) {
        endPos++;
    }
    if (endPos > pos) {
        try {
            return std::stod(block.substr(pos, endPos - pos));
        } catch (...) {}
    }
    return defaultVal;
}

static bool extractJsonBool(const std::string& block, const std::string& key, bool defaultVal = false) {
    std::string needle = "\"" + key + "\":";
    size_t pos = block.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos += needle.size();
    while (pos < block.size() && (block[pos] == ' ' || block[pos] == '\t' || block[pos] == '\n')) pos++;
    if (block.substr(pos, 4) == "true") return true;
    if (block.substr(pos, 5) == "false") return false;
    return defaultVal;
}

std::string PresetManager::serializeToJson(const ProductionPreset& preset) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"version\": \"1.0\",\n";
    ss << "  \"name\": \"" << escapeJson(preset.name) << "\",\n";
    ss << "  \"width\": " << preset.width << ",\n";
    ss << "  \"height\": " << preset.height << ",\n";
    ss << "  \"fps\": " << preset.fps << ",\n";
    ss << "  \"programSourceId\": " << preset.programSourceId << ",\n";
    ss << "  \"previewSourceId\": " << preset.previewSourceId << ",\n";
    ss << "  \"masterVolume\": " << std::fixed << std::setprecision(3) << preset.masterVolume << ",\n";

    // Inputs array
    ss << "  \"inputs\": [\n";
    for (size_t i = 0; i < preset.inputs.size(); ++i) {
        const auto& inp = preset.inputs[i];
        ss << "    {\n";
        ss << "      \"id\": " << inp.id << ",\n";
        ss << "      \"name\": \"" << escapeJson(inp.name) << "\",\n";
        ss << "      \"type\": \"" << escapeJson(inp.type) << "\",\n";
        ss << "      \"pathOrAddress\": \"" << escapeJson(inp.pathOrAddress) << "\",\n";
        ss << "      \"volume\": " << inp.volume << ",\n";
        ss << "      \"pan\": " << inp.pan << ",\n";
        ss << "      \"muted\": " << (inp.muted ? "true" : "false") << ",\n";
        ss << "      \"solo\": " << (inp.solo ? "true" : "false") << ",\n";
        ss << "      \"afv\": " << (inp.afv ? "true" : "false") << ",\n";
        ss << "      \"busRouting\": " << inp.busRouting << "\n";
        ss << "    }" << (i + 1 < preset.inputs.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // Overlays array
    ss << "  \"overlays\": [\n";
    for (size_t i = 0; i < preset.overlays.size(); ++i) {
        const auto& ov = preset.overlays[i];
        ss << "    {\n";
        ss << "      \"channel\": " << ov.channel << ",\n";
        ss << "      \"sourceId\": " << ov.sourceId << ",\n";
        ss << "      \"active\": " << (ov.active ? "true" : "false") << "\n";
        ss << "    }" << (i + 1 < preset.overlays.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    // PTZ cameras array
    ss << "  \"ptzCameras\": [\n";
    for (size_t i = 0; i < preset.ptzCameras.size(); ++i) {
        const auto& ptz = preset.ptzCameras[i];
        ss << "    {\n";
        ss << "      \"sourceId\": " << ptz.sourceId << ",\n";
        ss << "      \"name\": \"" << escapeJson(ptz.name) << "\",\n";
        ss << "      \"ip\": \"" << escapeJson(ptz.ip) << "\",\n";
        ss << "      \"port\": " << ptz.port << ",\n";
        ss << "      \"protocol\": " << ptz.protocol << "\n";
        ss << "    }" << (i + 1 < preset.ptzCameras.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";

    ss << "}\n";
    return ss.str();
}

bool PresetManager::deserializeFromJson(const std::string& jsonStr, ProductionPreset& outPreset) {
    if (jsonStr.empty()) return false;

    outPreset.name = extractJsonString(jsonStr, "name");
    if (outPreset.name.empty()) outPreset.name = "Untitled";

    outPreset.width = static_cast<int>(extractJsonNumber(jsonStr, "width", 1920));
    outPreset.height = static_cast<int>(extractJsonNumber(jsonStr, "height", 1080));
    outPreset.fps = extractJsonNumber(jsonStr, "fps", 60.0);
    outPreset.programSourceId = static_cast<uint64_t>(extractJsonNumber(jsonStr, "programSourceId", 0));
    outPreset.previewSourceId = static_cast<uint64_t>(extractJsonNumber(jsonStr, "previewSourceId", 0));
    outPreset.masterVolume = static_cast<float>(extractJsonNumber(jsonStr, "masterVolume", 1.0));

    // Parse inputs
    outPreset.inputs.clear();
    size_t inpsStart = jsonStr.find("\"inputs\":");
    if (inpsStart != std::string::npos) {
        size_t arrayOpen = jsonStr.find('[', inpsStart);
        size_t arrayClose = jsonStr.find(']', arrayOpen);
        if (arrayOpen != std::string::npos && arrayClose != std::string::npos) {
            std::string arrayContent = jsonStr.substr(arrayOpen + 1, arrayClose - arrayOpen - 1);
            size_t objStart = 0;
            while ((objStart = arrayContent.find('{', objStart)) != std::string::npos) {
                size_t objEnd = arrayContent.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string block = arrayContent.substr(objStart, objEnd - objStart + 1);

                PresetInputData inp;
                inp.id = static_cast<uint64_t>(extractJsonNumber(block, "id", 0));
                inp.name = extractJsonString(block, "name");
                inp.type = extractJsonString(block, "type");
                inp.pathOrAddress = extractJsonString(block, "pathOrAddress");
                inp.volume = static_cast<float>(extractJsonNumber(block, "volume", 1.0));
                inp.pan = static_cast<float>(extractJsonNumber(block, "pan", 0.0));
                inp.muted = extractJsonBool(block, "muted", false);
                inp.solo = extractJsonBool(block, "solo", false);
                inp.afv = extractJsonBool(block, "afv", false);
                inp.busRouting = static_cast<uint32_t>(extractJsonNumber(block, "busRouting", 1));

                outPreset.inputs.push_back(inp);
                objStart = objEnd + 1;
            }
        }
    }

    // Parse overlays
    outPreset.overlays.clear();
    size_t ovsStart = jsonStr.find("\"overlays\":");
    if (ovsStart != std::string::npos) {
        size_t arrayOpen = jsonStr.find('[', ovsStart);
        size_t arrayClose = jsonStr.find(']', arrayOpen);
        if (arrayOpen != std::string::npos && arrayClose != std::string::npos) {
            std::string arrayContent = jsonStr.substr(arrayOpen + 1, arrayClose - arrayOpen - 1);
            size_t objStart = 0;
            while ((objStart = arrayContent.find('{', objStart)) != std::string::npos) {
                size_t objEnd = arrayContent.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string block = arrayContent.substr(objStart, objEnd - objStart + 1);

                PresetOverlayData ov;
                ov.channel = static_cast<int>(extractJsonNumber(block, "channel", 1));
                ov.sourceId = static_cast<uint64_t>(extractJsonNumber(block, "sourceId", 0));
                ov.active = extractJsonBool(block, "active", false);

                outPreset.overlays.push_back(ov);
                objStart = objEnd + 1;
            }
        }
    }

    // Parse PTZ cameras
    outPreset.ptzCameras.clear();
    size_t ptzStart = jsonStr.find("\"ptzCameras\":");
    if (ptzStart != std::string::npos) {
        size_t arrayOpen = jsonStr.find('[', ptzStart);
        size_t arrayClose = jsonStr.find(']', arrayOpen);
        if (arrayOpen != std::string::npos && arrayClose != std::string::npos) {
            std::string arrayContent = jsonStr.substr(arrayOpen + 1, arrayClose - arrayOpen - 1);
            size_t objStart = 0;
            while ((objStart = arrayContent.find('{', objStart)) != std::string::npos) {
                size_t objEnd = arrayContent.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string block = arrayContent.substr(objStart, objEnd - objStart + 1);

                PresetPTZData ptz;
                ptz.sourceId = static_cast<uint64_t>(extractJsonNumber(block, "sourceId", 0));
                ptz.name = extractJsonString(block, "name");
                ptz.ip = extractJsonString(block, "ip");
                ptz.port = static_cast<uint16_t>(extractJsonNumber(block, "port", 52381));
                ptz.protocol = static_cast<int>(extractJsonNumber(block, "protocol", 0));

                outPreset.ptzCameras.push_back(ptz);
                objStart = objEnd + 1;
            }
        }
    }

    return true;
}

bool PresetManager::saveToFile(const std::string& filePath, const ProductionPreset& preset) {
    std::ofstream out(filePath);
    if (!out.is_open()) return false;
    out << serializeToJson(preset);
    return true;
}

bool PresetManager::loadFromFile(const std::string& filePath, ProductionPreset& outPreset) {
    std::ifstream in(filePath);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    return deserializeFromJson(ss.str(), outPreset);
}

} // namespace sm
