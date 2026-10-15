#include "streammix/shader_pipeline.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iomanip>

namespace sm {

static inline float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────
//  LUT3D Implementation
// ─────────────────────────────────────────────────────────────────

LUT3D::LUT3D() = default;

LUT3D::LUT3D(int size) : m_size(size) {
    if (size >= 2) {
        m_table.resize(static_cast<size_t>(size * size * size * 3), 0.0f);
    }
}

bool LUT3D::loadFromFile(const std::string& cubeFilePath) {
    std::ifstream file(cubeFilePath);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str());
}

bool LUT3D::loadFromString(const std::string& cubeContent) {
    std::istringstream stream(cubeContent);
    std::string line;

    int parsedSize = 0;
    std::vector<float> data;
    std::string parsedTitle = "Unnamed LUT";

    while (std::getline(stream, line)) {
        // Strip whitespace and carriage return
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("TITLE", 0) == 0) {
            auto pos = line.find('"');
            if (pos != std::string::npos) {
                auto endPos = line.rfind('"');
                if (endPos > pos) {
                    parsedTitle = line.substr(pos + 1, endPos - pos - 1);
                }
            } else if (line.size() > 6) {
                parsedTitle = line.substr(6);
            }
        } else if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            std::istringstream iss(line.substr(11));
            iss >> parsedSize;
            if (parsedSize >= 2) {
                data.reserve(static_cast<size_t>(parsedSize * parsedSize * parsedSize * 3));
            }
        } else if (parsedSize >= 2) {
            // Coordinate lines: R G B floats
            std::istringstream iss(line);
            float r, g, b;
            if (iss >> r >> g >> b) {
                data.push_back(r);
                data.push_back(g);
                data.push_back(b);
            }
        }
    }

    if (parsedSize >= 2 && data.size() == static_cast<size_t>(parsedSize * parsedSize * parsedSize * 3)) {
        m_size = parsedSize;
        m_title = parsedTitle;
        m_table = std::move(data);
        return true;
    }

    return false;
}

void LUT3D::sample(float r, float g, float b, float& outR, float& outG, float& outB) const {
    if (!isValid()) {
        outR = r;
        outG = g;
        outB = b;
        return;
    }

    r = clamp01(r) * (m_size - 1);
    g = clamp01(g) * (m_size - 1);
    b = clamp01(b) * (m_size - 1);

    int r0 = static_cast<int>(r);
    int g0 = static_cast<int>(g);
    int b0 = static_cast<int>(b);

    int r1 = std::min(r0 + 1, m_size - 1);
    int g1 = std::min(g0 + 1, m_size - 1);
    int b1 = std::min(b0 + 1, m_size - 1);

    float fr = r - r0;
    float fg = g - g0;
    float fb = b - b0;

    auto getRGB = [this](int rx, int gx, int bx, float& cr, float& cg, float& cb) {
        size_t idx = static_cast<size_t>((rx + gx * m_size + bx * m_size * m_size) * 3);
        cr = m_table[idx + 0];
        cg = m_table[idx + 1];
        cb = m_table[idx + 2];
    };

    float c000_r, c000_g, c000_b;
    float c100_r, c100_g, c100_b;
    float c010_r, c010_g, c010_b;
    float c110_r, c110_g, c110_b;
    float c001_r, c001_g, c001_b;
    float c101_r, c101_g, c101_b;
    float c011_r, c011_g, c011_b;
    float c111_r, c111_g, c111_b;

    getRGB(r0, g0, b0, c000_r, c000_g, c000_b);
    getRGB(r1, g0, b0, c100_r, c100_g, c100_b);
    getRGB(r0, g1, b0, c010_r, c010_g, c010_b);
    getRGB(r1, g1, b0, c110_r, c110_g, c110_b);

    getRGB(r0, g0, b1, c001_r, c001_g, c001_b);
    getRGB(r1, g0, b1, c101_r, c101_g, c101_b);
    getRGB(r0, g1, b1, c011_r, c011_g, c011_b);
    getRGB(r1, g1, b1, c111_r, c111_g, c111_b);

    // Trilinear interpolation
    auto lerp = [](float a, float b, float t) { return a + t * (b - a); };

    float c00_r = lerp(c000_r, c100_r, fr);
    float c00_g = lerp(c000_g, c100_g, fr);
    float c00_b = lerp(c000_b, c100_b, fr);

    float c10_r = lerp(c010_r, c110_r, fr);
    float c10_g = lerp(c010_g, c110_g, fr);
    float c10_b = lerp(c010_b, c110_b, fr);

    float c01_r = lerp(c001_r, c101_r, fr);
    float c01_g = lerp(c001_g, c101_g, fr);
    float c01_b = lerp(c001_b, c101_b, fr);

    float c11_r = lerp(c011_r, c111_r, fr);
    float c11_g = lerp(c011_g, c111_g, fr);
    float c11_b = lerp(c011_b, c111_b, fr);

    float c0_r = lerp(c00_r, c10_r, fg);
    float c0_g = lerp(c00_g, c10_g, fg);
    float c0_b = lerp(c00_b, c10_b, fg);

    float c1_r = lerp(c01_r, c11_r, fg);
    float c1_g = lerp(c01_g, c11_g, fg);
    float c1_b = lerp(c01_b, c11_b, fg);

    outR = clamp01(lerp(c0_r, c1_r, fb));
    outG = clamp01(lerp(c0_g, c1_g, fb));
    outB = clamp01(lerp(c0_b, c1_b, fb));
}

// ─────────────────────────────────────────────────────────────────
//  ShaderPipeline Manager
// ─────────────────────────────────────────────────────────────────

ShaderPipeline::ShaderPipeline() = default;

void ShaderPipeline::setColorGrading(uint64_t sourceId, const ColorGradingConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_colorGrades[sourceId] = config;
}

ColorGradingConfig ShaderPipeline::getColorGrading(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_colorGrades.find(sourceId);
    if (it != m_colorGrades.end()) {
        return it->second;
    }
    return ColorGradingConfig{};
}

void ShaderPipeline::resetColorGrading(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_colorGrades[sourceId] = ColorGradingConfig{};
}

bool ShaderPipeline::loadLUT(uint64_t sourceId, const std::string& cubeFilePath) {
    auto lut = std::make_shared<LUT3D>();
    if (lut->loadFromFile(cubeFilePath)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_luts[sourceId] = lut;
        return true;
    }
    return false;
}

bool ShaderPipeline::setLUTFromString(uint64_t sourceId, const std::string& cubeContent, const std::string& title) {
    auto lut = std::make_shared<LUT3D>();
    if (lut->loadFromString(cubeContent)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_luts[sourceId] = lut;
        return true;
    }
    return false;
}

void ShaderPipeline::removeLUT(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_luts.erase(sourceId);
}

bool ShaderPipeline::hasLUT(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_luts.find(sourceId) != m_luts.end();
}

std::shared_ptr<LUT3D> ShaderPipeline::getLUT(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_luts.find(sourceId);
    if (it != m_luts.end()) {
        return it->second;
    }
    return nullptr;
}

std::string ShaderPipeline::generateGLSLFragmentShader(uint64_t sourceId) const {
    ColorGradingConfig cg = getColorGrading(sourceId);
    bool useLut = hasLUT(sourceId);

    std::ostringstream glsl;
    glsl << "#version 330 core\n";
    glsl << "// StreamMix vMix 29 GPU Shader Pipeline — Source " << sourceId << "\n";
    glsl << "in vec2 v_texCoord;\n";
    glsl << "out vec4 fragColor;\n\n";
    glsl << "uniform sampler2D u_inputTexture;\n";
    if (useLut) {
        glsl << "uniform sampler3D u_lutTexture;\n";
    }
    glsl << "\n";
    glsl << "// Color Grading Uniforms\n";
    glsl << "const vec3 u_lift = vec3(" << cg.liftR << ", " << cg.liftG << ", " << cg.liftB << ");\n";
    glsl << "const vec3 u_gamma = vec3(" << cg.gammaR << ", " << cg.gammaG << ", " << cg.gammaB << ");\n";
    glsl << "const vec3 u_gain = vec3(" << cg.gainR << ", " << cg.gainG << ", " << cg.gainB << ");\n";
    glsl << "const float u_saturation = " << cg.saturation << ";\n";
    glsl << "const float u_contrast = " << cg.contrast << ";\n";
    glsl << "const float u_brightness = " << cg.brightness << ";\n\n";

    glsl << "vec3 applyColorGrading(vec3 col) {\n";
    glsl << "    // 1. Lift / Gain\n";
    glsl << "    col = clamp(col * u_gain + u_lift * (1.0 - col), 0.0, 1.0);\n";
    glsl << "    // 2. Gamma\n";
    glsl << "    col = pow(col, 1.0 / max(u_gamma, vec3(0.001)));\n";
    glsl << "    // 3. Contrast & Brightness\n";
    glsl << "    col = (col - 0.5) * u_contrast + 0.5 + u_brightness;\n";
    glsl << "    // 4. Saturation\n";
    glsl << "    float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));\n";
    glsl << "    col = mix(vec3(luma), col, u_saturation);\n";
    glsl << "    return clamp(col, 0.0, 1.0);\n";
    glsl << "}\n\n";

    glsl << "void main() {\n";
    glsl << "    vec4 tex = texture(u_inputTexture, v_texCoord);\n";
    glsl << "    vec3 graded = applyColorGrading(tex.rgb);\n";
    if (useLut) {
        glsl << "    // 3D LUT Color Remapping\n";
        glsl << "    graded = texture(u_lutTexture, graded).rgb;\n";
    }
    glsl << "    fragColor = vec4(graded, tex.a);\n";
    glsl << "}\n";

    return glsl.str();
}

std::string ShaderPipeline::generateMetalShader(uint64_t sourceId) const {
    ColorGradingConfig cg = getColorGrading(sourceId);
    bool useLut = hasLUT(sourceId);

    std::ostringstream msl;
    msl << "#include <metal_stdlib>\n";
    msl << "using namespace metal;\n\n";
    msl << "// StreamMix vMix 29 Metal MSL Pipeline — Source " << sourceId << "\n";
    msl << "kernel void colorGradeKernel(\n";
    msl << "    texture2d<float, access::read> inTex [[texture(0)]],\n";
    msl << "    texture2d<float, access::write> outTex [[texture(1)]],\n";
    if (useLut) {
        msl << "    texture3d<float, access::sample> lutTex [[texture(2)]],\n";
        msl << "    sampler lutSampler [[sampler(0)]],\n";
    }
    msl << "    uint2 gid [[thread_position_in_grid]])\n";
    msl << "{\n";
    msl << "    if (gid.x >= inTex.get_width() || gid.y >= inTex.get_height()) return;\n";
    msl << "    float4 col = inTex.read(gid);\n";
    msl << "    float3 c = col.rgb;\n";
    msl << "    // Lift / Gain\n";
    msl << "    float3 lift = float3(" << cg.liftR << ", " << cg.liftG << ", " << cg.liftB << ");\n";
    msl << "    float3 gamma = float3(" << cg.gammaR << ", " << cg.gammaG << ", " << cg.gammaB << ");\n";
    msl << "    float3 gain = float3(" << cg.gainR << ", " << cg.gainG << ", " << cg.gainB << ");\n";
    msl << "    c = clamp(c * gain + lift * (1.0f - c), 0.0f, 1.0f);\n";
    msl << "    c = pow(c, 1.0f / max(gamma, float3(0.001f)));\n";
    msl << "    c = (c - 0.5f) * " << cg.contrast << " + 0.5f + " << cg.brightness << ";\n";
    msl << "    float luma = dot(c, float3(0.2126f, 0.7152f, 0.0722f));\n";
    msl << "    c = mix(float3(luma), c, " << cg.saturation << ");\n";
    msl << "    c = clamp(c, 0.0f, 1.0f);\n";
    if (useLut) {
        msl << "    c = lutTex.sample(lutSampler, c).rgb;\n";
    }
    msl << "    outTex.write(float4(c, col.a), gid);\n";
    msl << "}\n";

    return msl.str();
}

void ShaderPipeline::processFrame(uint64_t sourceId, VideoFramePtr frame) {
    if (!frame || frame->data.empty() || frame->width <= 0 || frame->height <= 0) return;

    ColorGradingConfig grade;
    std::shared_ptr<LUT3D> lut;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_colorGrades.find(sourceId);
        if (it != m_colorGrades.end()) {
            grade = it->second;
        }
        auto itLut = m_luts.find(sourceId);
        if (itLut != m_luts.end()) {
            lut = itLut->second;
        }
    }

    if (!grade.enabled && !lut) return;

    applyGradingAndLUT(grade, lut.get(), frame->data.data(), frame->width, frame->height, frame->linesize);
}

void ShaderPipeline::applyGradingAndLUT(const ColorGradingConfig& grade,
                                       const LUT3D* lut,
                                       uint8_t* rgba,
                                       int width,
                                       int height,
                                       int linesize)
{
    float inv255 = 1.0f / 255.0f;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = rgba + y * linesize;
        for (int x = 0; x < width; ++x) {
            float r = row[x * 4 + 0] * inv255;
            float g = row[x * 4 + 1] * inv255;
            float b = row[x * 4 + 2] * inv255;

            if (grade.enabled) {
                // 1. Lift & Gain
                r = clamp01(r * grade.gainR + grade.liftR * (1.0f - r));
                g = clamp01(g * grade.gainG + grade.liftG * (1.0f - g));
                b = clamp01(b * grade.gainB + grade.liftB * (1.0f - b));

                // 2. Gamma
                if (grade.gammaR != 1.0f) r = std::pow(r, 1.0f / std::max(0.001f, grade.gammaR));
                if (grade.gammaG != 1.0f) g = std::pow(g, 1.0f / std::max(0.001f, grade.gammaG));
                if (grade.gammaB != 1.0f) b = std::pow(b, 1.0f / std::max(0.001f, grade.gammaB));

                // 3. Contrast & Brightness
                if (grade.contrast != 1.0f || grade.brightness != 0.0f) {
                    r = clamp01((r - 0.5f) * grade.contrast + 0.5f + grade.brightness);
                    g = clamp01((g - 0.5f) * grade.contrast + 0.5f + grade.brightness);
                    b = clamp01((b - 0.5f) * grade.contrast + 0.5f + grade.brightness);
                }

                // 4. Saturation
                if (grade.saturation != 1.0f) {
                    float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    r = clamp01(luma + grade.saturation * (r - luma));
                    g = clamp01(luma + grade.saturation * (g - luma));
                    b = clamp01(luma + grade.saturation * (b - luma));
                }
            }

            // 5. 3D LUT Remapping
            if (lut && lut->isValid()) {
                lut->sample(r, g, b, r, g, b);
            }

            row[x * 4 + 0] = static_cast<uint8_t>(r * 255.0f + 0.5f);
            row[x * 4 + 1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
            row[x * 4 + 2] = static_cast<uint8_t>(b * 255.0f + 0.5f);
        }
    }
}

} // namespace sm
