// Tempo movement: roll curves, performer swing / feel / fills, LFO rate jump on a macro.
#include "TestUtil.h"
#include "helix/ModSystem.h"
#include "helix/Params.h"
#include <cmath>
#include <string>

using namespace helix;

int main()
{
    // Roll x4: four hits per step
    CHECK(std::fabs(perfCurve(26, 0.0f) - 1.0f) < 1e-6f && perfCurve(26, 0.249f) < 0.01f && std::fabs(perfCurve(26, 0.25f) - 1.0f) < 1e-5f,
          "Roll x4 shape");
    CHECK(choices::perfStepShape.size() == (size_t)kNumPerfCurves, "curve names %zu", choices::perfStepShape.size());
    CHECK(paramDef(P_P1_C1).max == (float)(kNumPerfCurves - 1), "shape range");

    float p[kNumParams];
    for (int i = 0; i < kNumParams; ++i) p[i] = paramDef(i).def;
    p[P_P1_STEPS] = 4;
    p[P_P1_S1] = 1.0f; p[P_P1_S2] = -1.0f; p[P_P1_S3] = 1.0f; p[P_P1_S4] = -1.0f;

    // Swing: full swing stretches the first step of each pair to 1.5 steps
    CHECK(performerValue(p, 0, 1.2) < 0.0f, "straight timing");
    p[P_P1_SWING] = 1.0f;
    CHECK(performerValue(p, 0, 1.2) > 0.0f && performerValue(p, 0, 1.6) < 0.0f, "swing did not delay step 2");
    CHECK(performerValue(p, 0, 2.1) > 0.0f, "swing must keep pairs in place");
    p[P_P1_SWING] = 0.0f;

    // Feel
    p[P_P1_DIV] = 13;   // whatever the step is, half-time plays half as many steps, double twice as many
    const double n = performerSteps(p, 0, 4.0);
    p[P_P1_FEEL] = 1; CHECK(std::fabs(performerSteps(p, 0, 4.0) - n * 0.5) < 1e-9, "half-time");
    p[P_P1_FEEL] = 2; CHECK(std::fabs(performerSteps(p, 0, 4.0) - n * 2.0) < 1e-9, "double");
    p[P_P1_FEEL] = 3; CHECK(std::fabs(performerSteps(p, 0, 4.0) - n * 1.5) < 1e-9, "triplet");
    p[P_P1_FEEL] = 0;

    // Fill every 2: the last quarter of every second pass rolls
    for (int s = 0; s < 4; ++s) p[P_P1_S1 + s] = 1.0f;
    p[P_P1_FILL] = 1;
    CHECK(std::fabs(performerValue(p, 0, 3.375) - 1.0f) < 1e-5f, "first pass must not fill");
    CHECK(std::fabs(performerValue(p, 0, 7.375) - 0.25f) < 1e-4f, "second pass did not roll (%f)", performerValue(p, 0, 7.375));
    CHECK(std::fabs(performerValue(p, 0, 6.375) - 1.0f) < 1e-5f, "fill only on the last quarter");
    p[P_P1_FILL] = 2;
    CHECK(std::fabs(performerValue(p, 0, 7.375) - 1.0f) < 1e-5f && std::fabs(performerValue(p, 0, 15.375) - 0.25f) < 1e-4f, "fill every 4");

    // Loops: 8 steps, loop 3..6
    p[P_P1_FILL] = 0;
    p[P_P1_STEPS] = 8;
    p[P_P1_LSTART] = 3; p[P_P1_LEND] = 6;
    auto seq = [&](int mode, int count) {
        p[P_P1_LOOP] = (float)mode;
        std::string out;
        for (int n = 0; n < count; ++n) out += (char)('1' + performerStepAt(p, 0, n));
        return out;
    };
    CHECK(seq(0, 10) == "1234567812", "off '%s'", seq(0, 10).c_str());
    CHECK(seq(1, 9) == "345634563", "forward '%s'", seq(1, 9).c_str());
    CHECK(seq(2, 9) == "654365436", "backward '%s'", seq(2, 9).c_str());
    CHECK(seq(3, 10) == "3456543456", "ping-pong '%s'", seq(3, 10).c_str());
    CHECK(seq(5, 12) == "123456345634", "intro+loop '%s'", seq(5, 12).c_str());
    const std::string r1 = seq(4, 64), r2 = seq(4, 64);
    bool inRange = true, varied = false;
    for (char c : r1) { inRange &= c >= '3' && c <= '6'; varied |= c != r1[0]; }
    CHECK(inRange && varied && r1 == r2, "random '%s'", r1.c_str());
    p[P_P1_LSTART] = 7; p[P_P1_LEND] = 4;   // reversed points are swapped
    CHECK(seq(1, 5) == "45674", "swapped points '%s'", seq(1, 5).c_str());
    p[P_P1_LSTART] = 2; p[P_P1_LEND] = 16;  // the end stays inside the pattern (Length 8)
    CHECK(seq(1, 9) == "234567823", "end clamped to Length '%s'", seq(1, 9).c_str());
    // the value follows the loop: step 6's level plays at step count 0 going backward
    for (int k = 0; k < 8; ++k) { p[P_P1_S1 + k] = (float)k / 8.0f; p[P_P1_C1 + k] = 0.0f; }
    p[P_P1_LSTART] = 3; p[P_P1_LEND] = 6; p[P_P1_LOOP] = 2;
    CHECK(std::fabs(performerValue(p, 0, 0.5) - 5.0f / 8.0f) < 1e-5f, "backward value %f", performerValue(p, 0, 0.5));
    CHECK(std::fabs(performerPatternValue(p, 0, 0.5) - 0.0f) < 1e-5f, "pattern view ignores the loop");
    p[P_P1_LOOP] = 0;

    // LFO jump
    p[P_L2_JUMP] = 3;   // x4
    p[P_LJ_MACRO] = 3;  // Macro 4
    p[P_MACRO4] = 0.2f;
    CHECK(lfoJumpFactor(p, 1) == 1.0f, "jump with macro down");
    p[P_MACRO4] = 0.8f;
    CHECK(lfoJumpFactor(p, 1) == 4.0f && lfoJumpFactor(p, 0) == 1.0f, "jump with macro up");
    p[P_L2_JUMP] = 5;
    CHECK(lfoJumpFactor(p, 1) == 0.5f, "jump x1/2");
    TEST_MAIN_END();
}
