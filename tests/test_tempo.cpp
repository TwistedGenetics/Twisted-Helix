// Tempo movement: roll curves, performer swing / feel / fills, LFO rate jump on a macro.
#include "TestUtil.h"
#include "helix/ModSystem.h"
#include "helix/Params.h"
#include <cmath>

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
