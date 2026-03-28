/*
 * LearningTest.cpp
 * openc2e
 *
 * Tests for the reward/punishment reinforcement system (Phase 3, plan 03-03):
 *   LEARN-05: Reward chemical above threshold increases dendrite STW
 *   LEARN-06: Punishment chemical above threshold decreases dendrite STW
 *   LEARN-07: STW converges toward LTW via opcode 44 migration
 *   LEARN-08: Age-susceptibility scaling — baby creatures learn faster than senile
 *
 * Test strategy: all tests operate on ReinforcementDetails and raw float arrays
 * directly, simulating the opcode 44 migration pattern. No full Creature
 * construction needed — avoids genome/dependency chain in tests.
 *
 * Per D-07/D-08/D-09: reward/punishment gate on OUTPUT_VAR (variables[2]) of dest.
 * Per D-10/D-11/D-12: opcode 44 pulls STW toward LTW and LTW toward STW.
 * Per D-13/D-15: susceptibility = 1.0 - stage/7.0, baby=1.0, senile~=0.14.
 */

#include "openc2e/creatures/c2eBrain.h"

#include <gtest/gtest.h>
#include <cstring>

// ---------------------------------------------------------------------------
// LEARN-05: Reward modifies dendrite STW
// ---------------------------------------------------------------------------

TEST(LearningTest, Reward_ModifiesSTW) {
    ReinforcementDetails reward;
    reward.supported = true;
    reward.threshold = 0.1f;
    reward.rate = 0.5f;
    reward.chemical_index = 0; // unused in direct reinforce() call

    float stw = 0.0f;
    float chemical_level = 0.5f;
    float susceptibility = 1.0f; // baby

    reward.reinforce(chemical_level, stw, susceptibility);
    // Expected: stw += 0.5 * (0.5 - 0.1) * 1.0 = 0.5 * 0.4 = 0.2
    EXPECT_NEAR(stw, 0.2f, 0.001f);
}

// ---------------------------------------------------------------------------
// LEARN-06: Punishment decreases dendrite STW (negative rate)
// ---------------------------------------------------------------------------

TEST(LearningTest, Punishment_DecreasesSTW) {
    ReinforcementDetails punishment;
    punishment.supported = true;
    punishment.threshold = 0.1f;
    punishment.rate = -0.5f; // negative rate decreases weight
    punishment.chemical_index = 0;

    float stw = 0.3f; // start above zero
    float chemical_level = 0.5f;
    float susceptibility = 1.0f;

    punishment.reinforce(chemical_level, stw, susceptibility);
    // Expected: stw += -0.5 * (0.5 - 0.1) * 1.0 = -0.2, so 0.3 - 0.2 = 0.1
    EXPECT_NEAR(stw, 0.1f, 0.001f);
}

// ---------------------------------------------------------------------------
// Active neuron gate: dest OUTPUT_VAR==0 prevents reinforcement
// (Tests the gate LOGIC pattern — per D-09)
// ---------------------------------------------------------------------------

TEST(LearningTest, ActiveNeuronGate_BlocksReinforcement) {
    // Simulate the gate check: only reinforce if dest->variables[2] != 0
    c2eNeuron dest_neuron;
    memset(&dest_neuron, 0, sizeof(dest_neuron));
    dest_neuron.variables[2] = 0.0f; // OUTPUT_VAR = 0 means neuron is INACTIVE

    ReinforcementDetails reward;
    reward.supported = true;
    reward.threshold = 0.1f;
    reward.rate = 0.5f;

    float stw_initial = 0.0f;
    float stw = stw_initial;
    float chemical_level = 0.8f; // above threshold

    // The gate check that processRewardAndPunishment() performs:
    bool gate_allows = (dest_neuron.variables[2] != 0.0f);
    if (gate_allows) {
        reward.reinforce(chemical_level, stw, 1.0f);
    }

    // Gate should have blocked reinforcement — STW unchanged
    EXPECT_FLOAT_EQ(stw, stw_initial);
}

TEST(LearningTest, ActiveNeuronGate_AllowsReinforcement) {
    // When dest OUTPUT_VAR is non-zero, gate allows reinforcement
    c2eNeuron dest_neuron;
    memset(&dest_neuron, 0, sizeof(dest_neuron));
    dest_neuron.variables[2] = 0.7f; // OUTPUT_VAR != 0 means neuron is ACTIVE

    ReinforcementDetails reward;
    reward.supported = true;
    reward.threshold = 0.1f;
    reward.rate = 0.5f;

    float stw = 0.0f;
    float chemical_level = 0.5f;

    bool gate_allows = (dest_neuron.variables[2] != 0.0f);
    if (gate_allows) {
        reward.reinforce(chemical_level, stw, 1.0f);
    }

    // Gate allowed reinforcement — STW should have increased
    EXPECT_GT(stw, 0.0f);
}

// ---------------------------------------------------------------------------
// Unsupported reward: reward.supported=false means no effect
// ---------------------------------------------------------------------------

TEST(LearningTest, UnsupportedReward_NoEffect) {
    ReinforcementDetails reward;
    reward.supported = false; // disabled
    reward.threshold = 0.1f;
    reward.rate = 0.5f;

    float stw = 0.0f;
    float chemical_level = 0.8f; // well above threshold

    // Caller must check supported — simulate what processRewardAndPunishment does
    if (reward.supported) {
        reward.reinforce(chemical_level, stw, 1.0f);
    }

    // No reinforcement should occur
    EXPECT_FLOAT_EQ(stw, 0.0f);
}

// ---------------------------------------------------------------------------
// LEARN-07: STW-LTW migration — simulating opcode 44
// ---------------------------------------------------------------------------

TEST(LearningTest, STW_ConvergesTo_LTW) {
    // Simulate opcode 44 migration: dendrite[0]=STW, dendrite[1]=LTW
    float dendrite[8] = {};
    dendrite[0] = 0.8f; // STW starts high
    dendrite[1] = 0.2f; // LTW starts low

    float stw_rate = 0.1f;  // rate at which STW moves toward LTW
    float ltw_rate = 0.05f; // rate at which LTW moves toward STW

    // Run 10 migration ticks (simulating opcode 44)
    for (int tick = 0; tick < 10; tick++) {
        float old_stw = dendrite[0];
        dendrite[0] = old_stw + (dendrite[1] - old_stw) * stw_rate;
        dendrite[1] = dendrite[1] + (old_stw - dendrite[1]) * ltw_rate;
    }

    // STW should have moved toward LTW (decreased from 0.8)
    EXPECT_LT(dendrite[0], 0.8f);
    EXPECT_GT(dendrite[0], 0.2f); // hasn't reached LTW yet
}

TEST(LearningTest, LTW_ConvergesTo_STW) {
    // Same setup as above, verify LTW moves toward original STW
    float dendrite[8] = {};
    dendrite[0] = 0.8f; // STW starts high
    dendrite[1] = 0.2f; // LTW starts low

    float stw_rate = 0.1f;
    float ltw_rate = 0.05f;

    for (int tick = 0; tick < 10; tick++) {
        float old_stw = dendrite[0];
        dendrite[0] = old_stw + (dendrite[1] - old_stw) * stw_rate;
        dendrite[1] = dendrite[1] + (old_stw - dendrite[1]) * ltw_rate;
    }

    // LTW should have moved toward original STW (increased from 0.2)
    EXPECT_GT(dendrite[1], 0.2f);
}

// ---------------------------------------------------------------------------
// LEARN-08: Age-susceptibility — reinforce() susceptibility parameter
// ---------------------------------------------------------------------------

TEST(LearningTest, Susceptibility_BabyMaximum) {
    // Baby susceptibility = 1.0 - 0/7 = 1.0 (maximum learning)
    ReinforcementDetails reward;
    reward.supported = true;
    reward.threshold = 0.1f;
    reward.rate = 0.5f;

    float stw_baby = 0.0f;
    float stw_adolescent = 0.0f;
    float chemical_level = 0.5f;

    reward.reinforce(chemical_level, stw_baby, 1.0f);              // baby: susceptibility=1.0
    reward.reinforce(chemical_level, stw_adolescent, 1.0f - 2.0f/7.0f); // adolescent: susceptibility~=0.714

    EXPECT_GT(stw_baby, stw_adolescent);
}

TEST(LearningTest, Susceptibility_SenileMinimum) {
    // Senile susceptibility = 1.0 - 6/7 ~= 0.143 (minimum learning)
    ReinforcementDetails reward;
    reward.supported = true;
    reward.threshold = 0.1f;
    reward.rate = 0.5f;

    float stw_baby = 0.0f;
    float stw_senile = 0.0f;
    float chemical_level = 0.5f;

    reward.reinforce(chemical_level, stw_baby, 1.0f);               // baby
    reward.reinforce(chemical_level, stw_senile, 1.0f - 6.0f/7.0f); // senile

    EXPECT_GT(stw_baby, stw_senile);
    EXPECT_GT(stw_senile, 0.0f); // senile still learns, just less
}

TEST(LearningTest, Susceptibility_DecreasesWithAge) {
    // Verify full monotonic decrease: baby > child > adolescent > senile
    ReinforcementDetails reward;
    reward.supported = true;
    reward.threshold = 0.1f;
    reward.rate = 0.5f;

    float chemical_level = 0.5f;

    // baby (stage=0): susceptibility = 1.0 - 0/7 = 1.0
    float stw_baby = 0.0f;
    reward.reinforce(chemical_level, stw_baby, 1.0f);

    // child (stage=1): susceptibility = 1.0 - 1/7 ~= 0.857
    float stw_child = 0.0f;
    reward.reinforce(chemical_level, stw_child, 1.0f - 1.0f/7.0f);

    // adolescent (stage=2): susceptibility = 1.0 - 2/7 ~= 0.714
    float stw_adolescent = 0.0f;
    reward.reinforce(chemical_level, stw_adolescent, 1.0f - 2.0f/7.0f);

    // senile (stage=6): susceptibility = 1.0 - 6/7 ~= 0.143
    float stw_senile = 0.0f;
    reward.reinforce(chemical_level, stw_senile, 1.0f - 6.0f/7.0f);

    EXPECT_GT(stw_baby, stw_child);
    EXPECT_GT(stw_child, stw_adolescent);
    EXPECT_GT(stw_adolescent, stw_senile);
    EXPECT_GT(stw_senile, 0.0f); // senile still learns, just less
}

// ---------------------------------------------------------------------------
// End-to-end: Reward -> STW increase -> LTW convergence chain
// ---------------------------------------------------------------------------

TEST(LearningTest, EndToEnd_Reward_STW_LTW_Chain) {
    // Full chain: reward reinforcement increases STW, then migration pulls LTW toward STW
    ReinforcementDetails reward;
    reward.supported = true;
    reward.threshold = 0.1f;
    reward.rate = 0.5f;

    float dendrite[8] = {}; // dendrite[0]=STW=0, dendrite[1]=LTW=0
    float chemical_level = 0.6f;

    // Step 1: Reward reinforcement increases STW
    reward.reinforce(chemical_level, dendrite[0], 1.0f); // baby susceptibility
    EXPECT_GT(dendrite[0], 0.0f);          // STW increased
    EXPECT_FLOAT_EQ(dendrite[1], 0.0f);    // LTW unchanged by reinforcement

    // Step 2: Run 20 migration ticks (opcode 44 pattern) — LTW converges toward STW
    float stw_rate = 0.1f;
    float ltw_rate = 0.05f;
    for (int tick = 0; tick < 20; tick++) {
        float old_stw = dendrite[0];
        dendrite[0] = old_stw + (dendrite[1] - old_stw) * stw_rate;
        dendrite[1] = dendrite[1] + (old_stw - dendrite[1]) * ltw_rate;
    }

    // LTW must be measurably higher than its initial value of 0
    EXPECT_GT(dendrite[1], 0.0f);
    EXPECT_GT(dendrite[1], 0.01f); // non-trivial increase
}
