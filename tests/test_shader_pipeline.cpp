#include "streammix/shader_pipeline.h"
#include <cassert>
#include <iostream>
#include <cmath>

using namespace sm;

int main() {
    std::cout << "[TEST] Starting GPU Shader Pipeline & 3D LUT Test Suite...\n";

    ShaderPipeline pipeline;
    const uint64_t CAM1 = 101;

    // ─────────────────────────────────────────────────────────────
    // 1. Color Grading Parameter Management
    // ─────────────────────────────────────────────────────────────
    ColorGradingConfig grade;
    grade.liftR = 0.05f;
    grade.gammaG = 1.2f;
    grade.gainB = 1.1f;
    grade.saturation = 0.0f; // Pure Grayscale test
    grade.contrast = 1.2f;
    grade.brightness = 0.02f;

    pipeline.setColorGrading(CAM1, grade);
    auto retrieved = pipeline.getColorGrading(CAM1);
    assert(retrieved.enabled == true);
    assert(std::abs(retrieved.liftR - 0.05f) < 0.001f);
    assert(std::abs(retrieved.gammaG - 1.2f) < 0.001f);
    assert(std::abs(retrieved.saturation - 0.0f) < 0.001f);

    std::cout << "  ✓ Color grading configuration & parameter storage verified\n";

    // ─────────────────────────────────────────────────────────────
    // 2. Grayscale & Contrast Frame Processing
    // ─────────────────────────────────────────────────────────────
    auto frame = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(64, 64));
    // Fill with colored pixels: Pure Red (255, 0, 0, 255)
    for (size_t i = 0; i < frame->data.size(); i += 4) {
        frame->data[i + 0] = 200; // Red
        frame->data[i + 1] = 50;  // Green
        frame->data[i + 2] = 20;  // Blue
        frame->data[i + 3] = 255;
    }

    pipeline.processFrame(CAM1, frame);

    // Because saturation was set to 0.0f, R, G, and B should be desaturated (R == G == B)
    uint8_t outR = frame->data[0];
    uint8_t outG = frame->data[1];
    uint8_t outB = frame->data[2];

    // Check that R, G, B are equal (pure monochrome/grayscale)
    assert(std::abs(static_cast<int>(outR) - static_cast<int>(outG)) <= 2);
    assert(std::abs(static_cast<int>(outG) - static_cast<int>(outB)) <= 2);

    std::cout << "  ✓ Saturation & color grading image execution verified (R=" 
              << (int)outR << ", G=" << (int)outG << ", B=" << (int)outB << ")\n";

    // ─────────────────────────────────────────────────────────────
    // 3. 3D LUT Engine (.cube format parsing & trilinear sampling)
    // ─────────────────────────────────────────────────────────────
    // Create an invert-colors 3D LUT (size 2x2x2)
    std::string cubeLUT = 
        "# StreamMix Test 3D LUT\n"
        "TITLE \"ColorInvert\"\n"
        "LUT_3D_SIZE 2\n"
        "1.0 1.0 1.0\n" // (0,0,0) -> inverts to white
        "0.0 1.0 1.0\n" // (1,0,0)
        "1.0 0.0 1.0\n" // (0,1,0)
        "0.0 0.0 1.0\n" // (1,1,0)
        "1.0 1.0 0.0\n" // (0,0,1)
        "0.0 1.0 0.0\n" // (1,0,1)
        "1.0 0.0 0.0\n" // (0,1,1)
        "0.0 0.0 0.0\n"; // (1,1,1) -> inverts to black

    LUT3D lut;
    bool loaded = lut.loadFromString(cubeLUT);
    assert(loaded);
    assert(lut.size() == 2);
    assert(lut.title() == "ColorInvert");

    // Sample corner: (0, 0, 0) should invert to (1, 1, 1)
    float sR, sG, sB;
    lut.sample(0.0f, 0.0f, 0.0f, sR, sG, sB);
    assert(std::abs(sR - 1.0f) < 0.01f);
    assert(std::abs(sG - 1.0f) < 0.01f);
    assert(std::abs(sB - 1.0f) < 0.01f);

    // Sample mid: (0.5, 0.5, 0.5) should trilinear interpolate to (0.5, 0.5, 0.5)
    lut.sample(0.5f, 0.5f, 0.5f, sR, sG, sB);
    assert(std::abs(sR - 0.5f) < 0.05f);

    // Register LUT on CAM1
    pipeline.setLUTFromString(CAM1, cubeLUT, "ColorInvert");
    assert(pipeline.hasLUT(CAM1));

    std::cout << "  ✓ 3D LUT parser and trilinear 3D interpolation verified\n";

    // ─────────────────────────────────────────────────────────────
    // 4. Cross-Platform Shader Code Generation (GLSL & Metal MSL)
    // ─────────────────────────────────────────────────────────────
    std::string glsl = pipeline.generateGLSLFragmentShader(CAM1);
    assert(glsl.find("#version 330 core") != std::string::npos);
    assert(glsl.find("uniform sampler3D u_lutTexture;") != std::string::npos);
    assert(glsl.find("applyColorGrading") != std::string::npos);

    std::string msl = pipeline.generateMetalShader(CAM1);
    assert(msl.find("#include <metal_stdlib>") != std::string::npos);
    assert(msl.find("kernel void colorGradeKernel") != std::string::npos);
    assert(msl.find("texture3d<float, access::sample> lutTex") != std::string::npos);

    std::cout << "  ✓ GLSL Fragment and Apple Metal MSL shader code generation verified\n";

    // Reset color grade
    pipeline.resetColorGrading(CAM1);
    pipeline.removeLUT(CAM1);
    assert(!pipeline.hasLUT(CAM1));

    std::cout << "[PASS] GPU Shader Pipeline & 3D LUT Test Suite completed successfully!\n";
    return 0;
}
