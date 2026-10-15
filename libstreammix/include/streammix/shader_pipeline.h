#pragma once
/**
 * sm::ShaderPipeline — GPU Shader Compositing, Color Grading & 3D LUT Engine (vMix 29)
 *
 * Implements:
 *   - Professional Broadcast Color Grading:
 *       * Lift (Shadows), Gamma (Midtones), Gain (Highlights)
 *       * Saturation, Contrast, Brightness, and Hue Shift
 *   - 3D LUT Engine (.cube format):
 *       * Parses industry standard DaVinci Resolve / Adobe .cube 3D LUT files
 *       * Trilinear 3D interpolation with boundary clamping
 *   - Cross-Platform Shader Code Generation:
 *       * GLSL fragment shaders (Windows, Linux, macOS OpenGL)
 *       * Metal Shading Language (MSL) kernels (macOS Apple Silicon zero-copy)
 *   - Fast SIMD/vectorized frame processing for high-performance software fallback
 */

#include "streammix/frame.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace sm {

struct ColorGradingConfig {
    bool enabled = true;

    // Lift (Shadows): -1.0 to +1.0 (default 0.0)
    float liftR = 0.0f;
    float liftG = 0.0f;
    float liftB = 0.0f;

    // Gamma (Midtones): 0.1 to 3.0 (default 1.0)
    float gammaR = 1.0f;
    float gammaG = 1.0f;
    float gammaB = 1.0f;

    // Gain (Highlights): 0.0 to 4.0 (default 1.0)
    float gainR = 1.0f;
    float gainG = 1.0f;
    float gainB = 1.0f;

    // Master Corrections
    float saturation = 1.0f;  ///< 0.0 = Grayscale, 1.0 = Normal, 2.0 = Double Saturation
    float contrast = 1.0f;    ///< 0.5 = Low contrast, 1.0 = Normal, 1.5 = High contrast
    float brightness = 0.0f;  ///< -1.0 to +1.0 (Offset)
    float hueShiftDeg = 0.0f; ///< -180.0 to +180.0 degrees
};

// ─── 3D Lookup Table (LUT) ───────────────────────────────────────
class LUT3D {
public:
    LUT3D();
    explicit LUT3D(int size);

    bool loadFromFile(const std::string& cubeFilePath);
    bool loadFromString(const std::string& cubeContent);

    bool isValid() const { return m_size >= 2 && !m_table.empty(); }
    int size() const { return m_size; }
    std::string title() const { return m_title; }

    /// Trilinear interpolation sample in normalized [0.0, 1.0] range
    void sample(float r, float g, float b, float& outR, float& outG, float& outB) const;

    /// Direct access to raw table data
    const std::vector<float>& tableData() const { return m_table; }

private:
    int m_size = 0;
    std::string m_title;
    std::vector<float> m_table; // size * size * size * 3 floats
};

// ─── Shader Pipeline Manager ─────────────────────────────────────
class ShaderPipeline {
public:
    ShaderPipeline();
    ~ShaderPipeline() = default;

    // ─── Color Grading Management ────────────────────────────────
    void setColorGrading(uint64_t sourceId, const ColorGradingConfig& config);
    ColorGradingConfig getColorGrading(uint64_t sourceId) const;
    void resetColorGrading(uint64_t sourceId);

    // ─── 3D LUT Management ───────────────────────────────────────
    bool loadLUT(uint64_t sourceId, const std::string& cubeFilePath);
    bool setLUTFromString(uint64_t sourceId, const std::string& cubeContent, const std::string& title = "");
    void removeLUT(uint64_t sourceId);
    bool hasLUT(uint64_t sourceId) const;
    std::shared_ptr<LUT3D> getLUT(uint64_t sourceId) const;

    // ─── GPU Shader Code Generation ──────────────────────────────
    /// Generates fully specialized GLSL fragment shader code for the channel
    std::string generateGLSLFragmentShader(uint64_t sourceId) const;

    /// Generates fully specialized Apple Metal MSL kernel code for the channel
    std::string generateMetalShader(uint64_t sourceId) const;

    // ─── Frame Processing (Software / SIMD fallback) ─────────────
    /// Applies color grading and 3D LUT to an RGBA video frame in place
    void processFrame(uint64_t sourceId, VideoFramePtr frame);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, ColorGradingConfig> m_colorGrades;
    std::unordered_map<uint64_t, std::shared_ptr<LUT3D>> m_luts;

    static void applyGradingAndLUT(const ColorGradingConfig& grade,
                                  const LUT3D* lut,
                                  uint8_t* rgba,
                                  int width,
                                  int height,
                                  int linesize);
};

} // namespace sm
