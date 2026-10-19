/**
 * sm::Context — Implementation
 *
 * Implements the master controller interconnecting Video Compositing,
 * Audio Mixing, and multi-source lifecycle.
 */

#include "streammix/context.h"
#include "streammix/record_output.h"
#include "streammix/virtual_cam.h"
#include "streammix/multicorder.h"
#include "streammix/stream_manager.h"
#include <ctime>

namespace sm {

Context::Context(ContextConfig config)
    : m_config(config)
{
    CompositorConfig compConfig;
    compConfig.width = config.videoWidth;
    compConfig.height = config.videoHeight;
    compConfig.fps = config.videoFps;
    m_compositor = std::make_unique<Compositor>(compConfig);

    m_audioMixer = std::make_unique<AudioMixer>(config.audioSampleRate, config.audioChannels);

    m_recordOutput = std::make_unique<RecordOutput>("Master Recorder");
    m_virtualCamOutput = std::make_unique<VirtualCamOutput>("StreamMix Virtual Camera");
    m_multiCorder = std::make_unique<MultiCorder>();
    m_streamManager = std::make_unique<MultiStreamManager>();
}

Context::~Context() = default;

bool Context::initialize() {
    return true;
}

void Context::setMasterResolution(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.videoWidth = width;
    m_config.videoHeight = height;
    if (m_compositor) {
        m_compositor->setResolution(width, height);
    }
}

void Context::shutdown() {
    stopRecording();
    stopExternal();
    stopStreaming();
    stopMultiCorder();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_sources) {
        if (pair.second) {
            pair.second->stop();
        }
    }
    m_sources.clear();
}

void Context::tick(double deltaSeconds) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_sources) {
            if (pair.second && pair.second->isRunning()) {
                pair.second->tick(deltaSeconds);
            }
        }
    }
    if (m_compositor) {
        m_compositor->tick(deltaSeconds);
    }
    if (m_audioMixer && m_compositor) {
        m_audioMixer->updateAFVState(m_compositor->programSourceId(), deltaSeconds);
    }
}

bool Context::addSource(std::shared_ptr<Source> source) {
    if (!source) return false;

    uint64_t id = source->id();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sources[id] = source;
    }

    // Register audio channel
    m_audioMixer->addChannel(id, source->name());

    // Hook video delivery callback directly to Compositor
    source->setVideoCallback([this, id](VideoFramePtr frame) {
        if (m_compositor) {
            m_compositor->updateInputFrame(id, std::move(frame));
        }
    });

    // Hook audio delivery callback directly to AudioMixer
    source->setAudioCallback([this, id](AudioFramePtr frame) {
        if (m_audioMixer) {
            m_audioMixer->pushAudio(id, std::move(frame));
        }
    });

    return true;
}

bool Context::removeSource(uint64_t sourceId) {
    std::shared_ptr<Source> src;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sources.find(sourceId);
        if (it != m_sources.end()) {
            src = it->second;
            m_sources.erase(it);
        }
    }

    if (src) {
        src->stop();
    }

    if (m_compositor) {
        m_compositor->removeInput(sourceId);
    }
    if (m_audioMixer) {
        m_audioMixer->removeChannel(sourceId);
    }

    return src != nullptr;
}

std::shared_ptr<Source> Context::getSource(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sources.find(sourceId);
    if (it != m_sources.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Source>> Context::getAllSources() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<Source>> list;
    list.reserve(m_sources.size());
    for (const auto& pair : m_sources) {
        list.push_back(pair.second);
    }
    return list;
}

void Context::cut() {
    if (m_compositor) {
        m_compositor->cut();
        if (m_audioMixer) {
            m_audioMixer->updateAFVState(m_compositor->programSourceId());
        }
    }
}

void Context::quickPlay(uint64_t sourceId) {
    if (m_compositor) {
        m_compositor->quickPlay(sourceId);
        if (m_audioMixer) {
            m_audioMixer->updateAFVState(sourceId);
        }
    }
}

void Context::startTransition(TransitionType type, int durationMs) {
    if (m_compositor) {
        m_compositor->startTransition(type, durationMs);
    }
}

void Context::setTBarPosition(float position) {
    if (m_compositor) {
        m_compositor->setTBarPosition(position);
    }
}

void Context::setPreviewSource(uint64_t sourceId) {
    if (m_compositor) {
        m_compositor->setPreviewSource(sourceId);
    }
}

void Context::setProgramSource(uint64_t sourceId) {
    if (m_compositor) {
        m_compositor->setProgramSource(sourceId);
        if (m_audioMixer) {
            m_audioMixer->updateAFVState(sourceId);
        }
    }
}

void Context::toggleOverlay(int channelIndex, OverlayTransition trans, int durationMs) {
    if (m_compositor) {
        m_compositor->toggleOverlay(channelIndex, trans, durationMs);
    }
}

void Context::showOverlay(int channelIndex, OverlayTransition trans, int durationMs) {
    if (m_compositor) {
        m_compositor->showOverlay(channelIndex, trans, durationMs);
    }
}

void Context::hideOverlay(int channelIndex, OverlayTransition trans, int durationMs) {
    if (m_compositor) {
        m_compositor->hideOverlay(channelIndex, trans, durationMs);
    }
}

void Context::toggleFTB(int durationMs) {
    if (m_compositor) {
        m_compositor->toggleFTB(durationMs);
    }
}

bool Context::isFTBActive() const {
    return m_compositor ? m_compositor->isFTBActive() : false;
}

void Context::setOverlaySource(int channelIndex, uint64_t sourceId) {
    if (m_compositor) {
        m_compositor->setOverlaySource(channelIndex, sourceId);
    }
}

bool Context::startRecording(const std::string& path, int bitrateKbps) {
    if (m_recording.load()) return true;
    std::string finalPath = path;
    if (finalPath.empty()) {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        char buf[128];
        std::strftime(buf, sizeof(buf), "/Users/john/Movies/StreamMix/StreamMix_Rec_%Y%m%d_%H%M%S.mp4", &tm);
        finalPath = buf;
    }
    OutputSettings s;
    s.url = finalPath;
    s.width = m_config.videoWidth;
    s.height = m_config.videoHeight;
    s.fps = m_config.videoFps;
    s.videoBitrateKbps = bitrateKbps;
    if (m_recordOutput && m_recordOutput->start(s)) {
        m_recording.store(true);
        return true;
    }
    return false;
}

void Context::stopRecording() {
    if (!m_recording.load()) return;
    if (m_recordOutput) m_recordOutput->stop();
    m_recording.store(false);
}

bool Context::isRecording() const {
    return m_recording.load();
}

bool Context::startExternal() {
    if (m_externalActive.load()) return true;
    OutputSettings s;
    s.width = m_config.videoWidth;
    s.height = m_config.videoHeight;
    s.fps = m_config.videoFps;
    if (m_virtualCamOutput && m_virtualCamOutput->start(s)) {
        m_externalActive.store(true);
        return true;
    }
    return false;
}

void Context::stopExternal() {
    if (!m_externalActive.load()) return;
    if (m_virtualCamOutput) m_virtualCamOutput->stop();
    m_externalActive.store(false);
}

bool Context::isExternalActive() const {
    return m_externalActive.load();
}

bool Context::startStreaming() {
    if (m_streaming.load()) return true;
    if (m_streamManager && m_streamManager->startAll()) {
        m_streaming.store(true);
        return true;
    }
    return false;
}

void Context::stopStreaming() {
    if (!m_streaming.load()) return;
    if (m_streamManager) m_streamManager->stopAll();
    m_streaming.store(false);
}

bool Context::isStreaming() const {
    return m_streaming.load();
}

bool Context::startMultiCorder(const std::string& dir) {
    std::string d = dir.empty() ? "/Users/john/Movies/StreamMix/MultiCorder" : dir;
    return m_multiCorder ? m_multiCorder->startRecording(d) : false;
}

void Context::stopMultiCorder() {
    if (m_multiCorder) m_multiCorder->stopRecording();
}

bool Context::isMultiCorderActive() const {
    return m_multiCorder ? m_multiCorder->isRecording() : false;
}

AudioFramePtr Context::getMixMinusForCaller(uint64_t callerSourceId, int frameCount, int64_t pts_us) {
    if (m_audioMixer) {
        return m_audioMixer->renderMixMinus(callerSourceId, frameCount, pts_us);
    }
    return nullptr;
}

std::string Context::executeVmixFunction(const std::string& function,
                                        const std::unordered_map<std::string, std::string>& params)
{
    auto getParam = [&](const std::string& key) -> std::string {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : "";
    };

    auto parseInputId = [&](const std::string& inputStr) -> uint64_t {
        if (inputStr.empty()) return 0;
        try {
            return std::stoull(inputStr);
        } catch (...) {
            return 0;
        }
    };

    int duration = 500;
    std::string durStr = getParam("Duration");
    if (!durStr.empty()) {
        try { duration = std::stoi(durStr); } catch (...) {}
    }

    if (function == "Cut") {
        cut();
    } else if (function == "Fade") {
        startTransition(TransitionType::Fade, duration);
    } else if (function == "Zoom") {
        startTransition(TransitionType::Zoom, duration);
    } else if (function == "Wipe") {
        startTransition(TransitionType::WipeLeft, duration);
    } else if (function == "Slide") {
        startTransition(TransitionType::SlideLeft, duration);
    } else if (function == "Fly") {
        startTransition(TransitionType::Fly, duration);
    } else if (function == "QuickPlay") {
        uint64_t inp = parseInputId(getParam("Input"));
        if (inp == 0) inp = m_compositor ? m_compositor->previewSourceId() : 0;
        quickPlay(inp);
    } else if (function == "PreviewInput") {
        uint64_t inp = parseInputId(getParam("Input"));
        setPreviewSource(inp);
    } else if (function == "ActiveInput") {
        uint64_t inp = parseInputId(getParam("Input"));
        setProgramSource(inp);
    } else if (function.rfind("OverlayInput", 0) == 0) {
        // e.g. OverlayInput1, OverlayInput2In, OverlayInput3Out, OverlayInput4Off
        std::string sub = function.substr(12); // after "OverlayInput"
        if (!sub.empty() && std::isdigit(sub[0])) {
            int ch = sub[0] - '0';
            std::string action = sub.substr(1);
            uint64_t inp = parseInputId(getParam("Input"));
            if (inp > 0) {
                setOverlaySource(ch, inp);
            }
            if (action == "In") {
                showOverlay(ch, OverlayTransition::Fade, duration);
            } else if (action == "Out") {
                hideOverlay(ch, OverlayTransition::Fade, duration);
            } else if (action == "Off") {
                hideOverlay(ch, OverlayTransition::Cut, 0);
            } else {
                toggleOverlay(ch, OverlayTransition::Fade, duration);
            }
        }
    } else if (function == "SetFader") {
        std::string valStr = getParam("Value");
        try {
            float val = std::stof(valStr);
            if (val > 1.0f) val /= 255.0f; // Scale 0-255 down to 0.0-1.0
            setTBarPosition(std::clamp(val, 0.0f, 1.0f));
        } catch (...) {}
    } else if (function == "FadeToBlack") {
        toggleFTB(duration);
    } else if (function == "Audio") {
        uint64_t inp = parseInputId(getParam("Input"));
        if (m_audioMixer) {
            bool muted = m_audioMixer->isMuted(inp);
            m_audioMixer->setMute(inp, !muted);
        }
    } else if (function == "AudioOn") {
        uint64_t inp = parseInputId(getParam("Input"));
        if (m_audioMixer) m_audioMixer->setMute(inp, false);
    } else if (function == "AudioOff") {
        uint64_t inp = parseInputId(getParam("Input"));
        if (m_audioMixer) m_audioMixer->setMute(inp, true);
    } else if (function == "SetVolume") {
        uint64_t inp = parseInputId(getParam("Input"));
        std::string valStr = getParam("Value");
        try {
            float val = std::stof(valStr);
            if (val > 1.0f) val /= 100.0f;
            if (m_audioMixer) m_audioMixer->setVolume(inp, std::clamp(val, 0.0f, 1.0f));
        } catch (...) {}
    } else if (function == "SetMasterVolume") {
        std::string valStr = getParam("Value");
        try {
            float val = std::stof(valStr);
            if (val > 1.0f) val /= 100.0f;
            if (m_audioMixer) m_audioMixer->setMasterVolume(std::clamp(val, 0.0f, 1.0f));
        } catch (...) {}
    } else if (function == "StartStreaming") {
        startStreaming();
    } else if (function == "StopStreaming") {
        stopStreaming();
    } else if (function == "StartStopStreaming") {
        if (isStreaming()) stopStreaming(); else startStreaming();
    } else if (function == "StartRecording") {
        startRecording();
    } else if (function == "StopRecording") {
        stopRecording();
    } else if (function == "StartStopRecording") {
        if (isRecording()) stopRecording(); else startRecording();
    }

    return "Function completed successfully.";
}

std::string Context::generateVmixXml() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    uint64_t pgmId = m_compositor ? m_compositor->programSourceId() : 0;
    uint64_t pvwId = m_compositor ? m_compositor->previewSourceId() : 0;
    bool ftb = m_compositor ? m_compositor->isFTBActive() : false;
    bool streaming = m_streaming.load();
    bool recording = m_recording.load();

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    xml << "<vmix>\n";
    xml << "  <version>29.0.0.1</version>\n";
    xml << "  <edition>Pro</edition>\n";
    xml << "  <preset>StreamMix Production</preset>\n";

    // Inputs
    xml << "  <inputs>\n";
    for (const auto& pair : m_sources) {
        uint64_t id = pair.first;
        const auto& src = pair.second;
        std::string name = src ? src->name() : ("Input " + std::to_string(id));
        float vol = m_audioMixer ? (m_audioMixer->getVolume(id) * 100.0f) : 100.0f;
        bool muted = m_audioMixer ? m_audioMixer->isMuted(id) : false;

        xml << "    <input key=\"" << id << "\" number=\"" << id
            << "\" type=\"Colour\" title=\"" << name << "\" state=\"Running\""
            << " position=\"0\" duration=\"0\" loop=\"False\""
            << " muted=\"" << (muted ? "True" : "False") << "\""
            << " volume=\"" << static_cast<int>(vol) << "\" balance=\"0\" solo=\"False\" audiobusses=\"M\">"
            << name << "</input>\n";
    }
    xml << "  </inputs>\n";

    // Overlays 1 to 8
    xml << "  <overlays>\n";
    for (int i = 1; i <= 8; ++i) {
        uint64_t srcId = 0;
        if (m_compositor) {
            const auto& ov = m_compositor->overlay(i);
            if (ov.state == OverlayState::Active || ov.state == OverlayState::In) {
                srcId = ov.sourceId;
            }
        }
        xml << "    <overlay number=\"" << i << "\">" << (srcId > 0 ? std::to_string(srcId) : "") << "</overlay>\n";
    }
    xml << "  </overlays>\n";

    // Preview / Active
    xml << "  <preview>" << pvwId << "</preview>\n";
    xml << "  <active>" << pgmId << "</active>\n";
    xml << "  <fadeToBlack>" << (ftb ? "True" : "False") << "</fadeToBlack>\n";

    // Standard vMix 29 transition buttons
    xml << "  <transitions>\n";
    xml << "    <transition number=\"1\" name=\"Fade\" duration=\"500\" />\n";
    xml << "    <transition number=\"2\" name=\"Wipe\" duration=\"500\" />\n";
    xml << "    <transition number=\"3\" name=\"Fly\" duration=\"500\" />\n";
    xml << "    <transition number=\"4\" name=\"Cut\" duration=\"0\" />\n";
    xml << "  </transitions>\n";

    xml << "  <recording>" << (recording ? "True" : "False") << "</recording>\n";
    xml << "  <external>True</external>\n";
    xml << "  <streaming>" << (streaming ? "True" : "False") << "</streaming>\n";
    xml << "  <playList>False</playList>\n";
    xml << "  <multiCorder>False</multiCorder>\n";
    xml << "  <fullscreen>False</fullscreen>\n";

    // Master Audio
    float mVol = m_audioMixer ? (m_audioMixer->masterVolume() * 100.0f) : 100.0f;
    bool mMute = m_audioMixer ? m_audioMixer->isMasterMuted() : false;
    float hpVol = m_audioMixer ? (m_audioMixer->headphonesVolume() * 100.0f) : 100.0f;

    xml << "  <audio>\n";
    xml << "    <master volume=\"" << static_cast<int>(mVol)
        << "\" muted=\"" << (mMute ? "True" : "False")
        << "\" meterF1=\"0.0\" meterF2=\"0.0\" headphonesVolume=\"" << static_cast<int>(hpVol) << "\" />\n";
    xml << "  </audio>\n";

    xml << "</vmix>\n";
    return xml.str();
}

} // namespace sm
