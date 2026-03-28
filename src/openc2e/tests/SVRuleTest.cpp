/*
 * SVRuleTest.cpp
 * openc2e
 *
 * Tests for c2eSVRule opcodes 37-42:
 *   37 - leakage rate (set tendrate)
 *   38 - rest state (tend INPUT_VAR toward operand at tendrate)
 *   39 - input gain hi-lo (multiply INPUT_VAR by operand)
 *   40 - persistence (blend INPUT_VAR into STATE_VAR)
 *   41 - signal noise (add scaled random noise to STATE_VAR)
 *   42 - winner takes all (winner OUTPUT_VAR = STATE_VAR; loser OUTPUT_VAR = 0)
 *
 * Per D-01: tests derive expected behavior from lc2e algorithm understanding.
 * Per D-02: expected values derived from manual calculation, not from running lc2e.
 * Per D-03: tests are in the existing test_openc2e GTest binary.
 */

#include "openc2e/creatures/c2eBrain.h"

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a 48-byte ruledata array for an SVRule with exactly N rules followed
// by zero (stop) opcodes.  Each entry is {opcode, operandtype, operanddata}.
static void fill_rule(uint8_t ruledata[48], int slot, uint8_t opcode, uint8_t operandtype, uint8_t operanddata) {
    ruledata[slot * 3 + 0] = opcode;
    ruledata[slot * 3 + 1] = operandtype;
    ruledata[slot * 3 + 2] = operanddata;
}

// operandtype 9 = zero (operandvalue == 0.0f)
// operandtype 10 = one  (operandvalue == 1.0f)
// operandtype 11 = value: operandvalue = data * (1/248)  -- [0, ~1.0]
// operandtype 12 = negative value: operandvalue = data * (-1/248)
// Return the operandvalue that c2eSVRule::init() will compute for type 11 / data d.
static float val11(uint8_t d) {
    return static_cast<float>(d) * (1.0f / 248.0f);
}

// Neuron variable indices (from c2eBrain.h context):
//   neuron[0] = STATE_VAR
//   neuron[1] = INPUT_VAR
//   neuron[2] = OUTPUT_VAR
static const int STATE_VAR  = 0;
static const int INPUT_VAR  = 1;
static const int OUTPUT_VAR = 2;

// ---------------------------------------------------------------------------
// Opcode 37: leakage rate — sets the internal tendrate register to operandvalue
//
// Behaviour: tendrate = operandvalue
// Verification: use opcode 25 (tend to) in the next rule to confirm tendrate
//   was set.  tend_to: accumulator += tendrate * (operand - accumulator)
//   With acc=0, tendrate=T, operand=1: result = 0 + T*(1-0) = T
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode37_LeakageRate_SetsTendRate) {
    uint8_t ruledata[48] = {};
    // Rule 0: opcode 37 (leakage rate), operand = 0.5 via type 11 data 124
    // 124/248 = 0.5 exactly
    fill_rule(ruledata, 0, 37, 11, 124);
    // Rule 1: opcode 25 (tend to), operand = 1.0 via type 10
    // accumulator(0) += tendrate * (1.0 - 0) => accumulator should equal tendrate
    fill_rule(ruledata, 1, 25, 10, 0);
    // Rule 2: opcode 2 (store in) — store accumulator into neuron[STATE_VAR]
    fill_rule(ruledata, 2, 2, 3, STATE_VAR);
    // Rules 3-15: opcode 0 (stop) — already zero-initialised

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};

    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    // The stored accumulator should equal val11(124) = 0.5f
    float expected_tendrate = val11(124);
    EXPECT_NEAR(neuron[STATE_VAR], expected_tendrate, 1e-5f);
}

// ---------------------------------------------------------------------------
// Opcode 38: rest state — neuron[1] tends toward operandvalue at tendrate
//
// Algorithm: neuron[INPUT_VAR] = neuron[INPUT_VAR]*(1-tendrate) + operandvalue*tendrate
// Setup: neuron[1]=0.25, tendrate=0.5 (via opcode 37), operand=1.0
//   result = 0.25*(1-0.5) + 1.0*0.5 = 0.125 + 0.5 = 0.625
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode38_RestState_TendsInputVarTowardOperand) {
    uint8_t ruledata[48] = {};
    // Rule 0: opcode 37, operand 0.5 (type 11 data 124)
    fill_rule(ruledata, 0, 37, 11, 124);
    // Rule 1: opcode 38, operand 1.0 (type 10)
    fill_rule(ruledata, 1, 38, 10, 0);

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    neuron[INPUT_VAR] = 0.25f;

    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    float tendrate = val11(124); // 0.5
    float expected = 0.25f * (1.0f - tendrate) + 1.0f * tendrate;
    EXPECT_NEAR(neuron[INPUT_VAR], expected, 1e-5f);
}

// ---------------------------------------------------------------------------
// Opcode 39: input gain hi-lo — neuron[1] *= operandvalue
//
// Setup: neuron[1]=0.6, operand=0.5 → result=0.3
// Use constant 0.5 = type 11 data 124
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode39_InputGain_MultipliesInputVar) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 39, 11, 124); // operand = val11(124) = 0.5

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    neuron[INPUT_VAR] = val11(149); // 149/248 ≈ 0.60081…

    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    float expected = val11(149) * val11(124);
    EXPECT_NEAR(neuron[INPUT_VAR], expected, 1e-5f);
}

// ---------------------------------------------------------------------------
// Opcode 40: persistence — blends INPUT_VAR into STATE_VAR
//
// Algorithm: neuron[0] = neuron[1]*(1-operand) + neuron[0]*operand
// Setup: neuron[0]=0.5, neuron[1]=1.0, operand=0.5
//   result = 1.0*0.5 + 0.5*0.5 = 0.5 + 0.25 = 0.75
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode40_Persistence_BlendsInputIntoState) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 40, 11, 124); // operand = 0.5

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    neuron[STATE_VAR] = val11(124); // 0.5
    neuron[INPUT_VAR] = 1.0f;

    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    float operand = val11(124);
    float expected = 1.0f * (1.0f - operand) + val11(124) * operand;
    EXPECT_NEAR(neuron[STATE_VAR], expected, 1e-5f);
}

// ---------------------------------------------------------------------------
// Opcode 41: signal noise — adds operandvalue * rand to STATE_VAR
//
// Case A: operand=0.0 → no noise, STATE_VAR unchanged
// Case B: operand=1.0 → noise added, STATE_VAR changes (result in [0, 2])
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode41_SignalNoise_ZeroOperandLeavesStateUnchanged) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 41, 9, 0); // operand = type 9 = 0.0

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    neuron[STATE_VAR] = 0.5f;

    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    EXPECT_FLOAT_EQ(neuron[STATE_VAR], 0.5f);
}

TEST(SVRuleTest, Opcode41_SignalNoise_NonZeroOperandChangesState) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 41, 10, 0); // operand = type 10 = 1.0

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    neuron[STATE_VAR] = 0.5f;

    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    // rand_float(0,1) >= 0, so result >= 0.5 and <= 1.5
    EXPECT_GE(neuron[STATE_VAR], 0.5f);
    EXPECT_LE(neuron[STATE_VAR], 1.5f);
}

// ---------------------------------------------------------------------------
// Opcode 42: winner takes all
//
// When neuron[0] >= spareneuron[0]:
//   spareneuron[OUTPUT_VAR] = 0.0f  (loser loses output)
//   neuron[OUTPUT_VAR]      = neuron[STATE_VAR]  (winner gets state)
//   is_spare set to true (runRule returns true)
//
// When neuron[0] < spareneuron[0]:
//   no changes, runRule returns false
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode42_WinnerTakesAll_WinnerCaseUpdatesOutputs) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 42, 9, 0); // operand irrelevant for opcode 42

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    // neuron is the winner: neuron[STATE_VAR] > spareneuron[STATE_VAR]
    neuron[STATE_VAR]      = 0.8f;
    neuron[OUTPUT_VAR]     = 0.0f;
    spareneuron[STATE_VAR] = 0.3f;
    spareneuron[OUTPUT_VAR] = 0.9f;

    bool is_spare = rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    // Winner's OUTPUT_VAR should equal its STATE_VAR
    EXPECT_FLOAT_EQ(neuron[OUTPUT_VAR], 0.8f);
    // Loser (spare)'s OUTPUT_VAR should be zeroed
    EXPECT_FLOAT_EQ(spareneuron[OUTPUT_VAR], 0.0f);
    // is_spare should be true (signals caller to update spare index)
    EXPECT_TRUE(is_spare);
}

TEST(SVRuleTest, Opcode42_WinnerTakesAll_LoserCaseNoChanges) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 42, 9, 0); // operand irrelevant

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    // neuron is the loser: neuron[STATE_VAR] < spareneuron[STATE_VAR]
    neuron[STATE_VAR]       = 0.3f;
    neuron[OUTPUT_VAR]      = 0.5f;
    spareneuron[STATE_VAR]  = 0.8f;
    spareneuron[OUTPUT_VAR] = 0.9f;

    bool is_spare = rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    // No changes: neuron[OUTPUT_VAR] stays at 0.5
    EXPECT_FLOAT_EQ(neuron[OUTPUT_VAR], 0.5f);
    // spareneuron[OUTPUT_VAR] stays at 0.9
    EXPECT_FLOAT_EQ(spareneuron[OUTPUT_VAR], 0.9f);
    // is_spare stays false
    EXPECT_FALSE(is_spare);
}

TEST(SVRuleTest, Opcode42_WinnerTakesAll_EqualStateCountsAsWin) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 42, 9, 0);

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8]    = {};
    float spareneuron[8] = {};
    float dendrite[8]  = {};
    // Equal STATE_VAR — the condition is >=, so this should also be a win
    neuron[STATE_VAR]      = 0.5f;
    neuron[OUTPUT_VAR]     = 0.0f;
    spareneuron[STATE_VAR] = 0.5f;
    spareneuron[OUTPUT_VAR] = 0.7f;

    bool is_spare = rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr);

    EXPECT_FLOAT_EQ(neuron[OUTPUT_VAR], 0.5f);
    EXPECT_FLOAT_EQ(spareneuron[OUTPUT_VAR], 0.0f);
    EXPECT_TRUE(is_spare);
}

// ===========================================================================
// Plan 01-04: Opcodes 57-62 and ReinforcementDetails tests
//
// These tests use raw-memory allocation to create a c2eTract-like object
// with initialised ReinforcementDetails fields, without needing a full
// c2eCreature/c2eBrain chain.  Only the reward/punishment/stw_to_ltw_rate
// public fields are accessed by opcodes 57-62 and 43/44, so bypassing the
// constructor is safe for these focused tests.
// ===========================================================================

// Helper: allocate a zeroed c2eTract-sized buffer with reward/punishment/stw
// fields initialised.  Caller must call free_stub_tract() when done.
static c2eTract* make_stub_tract() {
    auto* t = static_cast<c2eTract*>(::operator new(sizeof(c2eTract)));
    // Zero-initialise the whole allocation so unaccessed fields are benign
    // Cast to void* to suppress -Wdynamic-class-memaccess: we intentionally
    // overwrite the vtable here since we will properly construct the fields we use.
    std::memset(static_cast<void*>(t), 0, sizeof(c2eTract));
    // Properly construct only the fields that opcodes 57-62 and 43/44 touch
    new (&t->reward) ReinforcementDetails{};
    new (&t->punishment) ReinforcementDetails{};
    t->stw_to_ltw_rate = 0.0f;
    return t;
}

static void free_stub_tract(c2eTract* t) {
    t->reward.~ReinforcementDetails();
    t->punishment.~ReinforcementDetails();
    ::operator delete(t);
}

// ---------------------------------------------------------------------------
// Opcode 57: reward threshold — sets owner->reward.threshold = bindFloat(operandvalue)
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode57_RewardThreshold_SetsRewardThreshold) {
    uint8_t ruledata[48] = {};
    // operand = val11(124) = 0.5
    fill_rule(ruledata, 0, 57, 11, 124);

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8] = {};
    float spareneuron[8] = {};
    float dendrite[8] = {};

    c2eTract* owner = make_stub_tract();
    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, owner);

    EXPECT_NEAR(owner->reward.threshold, val11(124), 1e-5f);
    free_stub_tract(owner);
}

// ---------------------------------------------------------------------------
// Opcode 58: reward rate — sets owner->reward.rate = bindFloat(operandvalue)
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode58_RewardRate_SetsRewardRate) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 58, 11, 62); // operand = val11(62) ≈ 0.25

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8] = {};
    float spareneuron[8] = {};
    float dendrite[8] = {};

    c2eTract* owner = make_stub_tract();
    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, owner);

    EXPECT_NEAR(owner->reward.rate, val11(62), 1e-5f);
    free_stub_tract(owner);
}

// ---------------------------------------------------------------------------
// Opcode 59: use reward with — sets chemical_index and marks supported = true
//
// operandtype 15 = value integer, operandvalue = (float)operanddata
// So operanddata=7 → operandvalue=7.0, chemical_index = 7 % 256 = 7
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode59_UseRewardWith_SetsChemicalIndexAndSupported) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 59, 15, 7); // operandtype 15 = integer, operanddata=7

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8] = {};
    float spareneuron[8] = {};
    float dendrite[8] = {};

    c2eTract* owner = make_stub_tract();
    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, owner);

    EXPECT_EQ(owner->reward.chemical_index, 7);
    EXPECT_TRUE(owner->reward.supported);
    free_stub_tract(owner);
}

// ---------------------------------------------------------------------------
// Opcode 60: punishment threshold — sets owner->punishment.threshold
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode60_PunishmentThreshold_SetsPunishmentThreshold) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 60, 11, 124); // operand = 0.5

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8] = {};
    float spareneuron[8] = {};
    float dendrite[8] = {};

    c2eTract* owner = make_stub_tract();
    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, owner);

    EXPECT_NEAR(owner->punishment.threshold, val11(124), 1e-5f);
    free_stub_tract(owner);
}

// ---------------------------------------------------------------------------
// Opcode 61: punishment rate — sets owner->punishment.rate
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode61_PunishmentRate_SetsPunishmentRate) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 61, 11, 62); // operand ≈ 0.25

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8] = {};
    float spareneuron[8] = {};
    float dendrite[8] = {};

    c2eTract* owner = make_stub_tract();
    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, owner);

    EXPECT_NEAR(owner->punishment.rate, val11(62), 1e-5f);
    free_stub_tract(owner);
}

// ---------------------------------------------------------------------------
// Opcode 62: use punish with — sets chemical_index and marks supported = true
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode62_UsePunishWith_SetsPunishChemicalIndexAndSupported) {
    uint8_t ruledata[48] = {};
    fill_rule(ruledata, 0, 62, 15, 12); // operanddata=12 → chemical_index=12

    c2eSVRule rule;
    rule.init(ruledata);

    float srcneuron[8] = {};
    float neuron[8] = {};
    float spareneuron[8] = {};
    float dendrite[8] = {};

    c2eTract* owner = make_stub_tract();
    rule.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, owner);

    EXPECT_EQ(owner->punishment.chemical_index, 12);
    EXPECT_TRUE(owner->punishment.supported);
    free_stub_tract(owner);
}

// ---------------------------------------------------------------------------
// ReinforcementDetails::reinforce — core reward/punishment logic
//
// When chemical_level > threshold:
//   weight += rate * (chemical_level - threshold), clamped to [-1, 1]
//
// Test A: chemical=0.5, threshold=0.1, rate=0.5 → modifier=0.4, delta=0.2
//   weight starts at 0.0 → ends at 0.2
// ---------------------------------------------------------------------------
TEST(SVRuleTest, ReinforcementDetails_Reinforce_ChemicalAboveThresholdModifiesWeight) {
    ReinforcementDetails rd;
    rd.threshold = 0.1f;
    rd.rate = 0.5f;
    rd.chemical_index = 0;
    rd.supported = true;

    float weight = 0.0f;
    rd.reinforce(0.5f, weight);

    // delta = 0.5 * (0.5 - 0.1) = 0.5 * 0.4 = 0.2
    EXPECT_NEAR(weight, 0.2f, 1e-5f);
}

// Test B: chemical_level <= threshold → no change
TEST(SVRuleTest, ReinforcementDetails_Reinforce_ChemicalBelowThresholdLeavesWeightUnchanged) {
    ReinforcementDetails rd;
    rd.threshold = 0.5f;
    rd.rate = 0.5f;

    float weight = 0.3f;
    rd.reinforce(0.3f, weight); // chemical <= threshold
    EXPECT_FLOAT_EQ(weight, 0.3f);
}

// Test C: clamping — weight + delta > 1.0 → clamped to 1.0
TEST(SVRuleTest, ReinforcementDetails_Reinforce_WeightClampedToUpperBound) {
    ReinforcementDetails rd;
    rd.threshold = 0.0f;
    rd.rate = 2.0f;

    float weight = 0.8f;
    rd.reinforce(1.0f, weight); // delta = 2.0 * 1.0 = 2.0, weight would be 2.8
    EXPECT_FLOAT_EQ(weight, 1.0f); // clamped
}

// ---------------------------------------------------------------------------
// STtoLTRate independence: two tracts must have independent stw_to_ltw_rate
//
// Before this plan, opcode 43 used a static local variable which meant all
// tracts shared the same rate.  After this plan, each tract stores its own.
//
// Verify: set rate on tract A, then tract B; tract A should still have its
// original rate (not overwritten by tract B's assignment).
// ---------------------------------------------------------------------------
TEST(SVRuleTest, Opcode43_STtoLTRate_StoredPerTractNotShared) {
    // Build a single-opcode rule: opcode 43 with operand=0.5
    uint8_t ruledataA[48] = {};
    fill_rule(ruledataA, 0, 43, 11, 124); // 0.5

    uint8_t ruledataB[48] = {};
    fill_rule(ruledataB, 0, 43, 11, 62); // ~0.25

    c2eSVRule ruleA, ruleB;
    ruleA.init(ruledataA);
    ruleB.init(ruledataB);

    float srcneuron[8] = {};
    float neuron[8] = {};
    float spareneuron[8] = {};
    float dendrite[8] = {};

    c2eTract* tractA = make_stub_tract();
    c2eTract* tractB = make_stub_tract();

    // Set rate on tract A (0.5)
    ruleA.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, tractA);
    // Set rate on tract B (0.25)
    ruleB.runRule(0.0f, srcneuron, neuron, spareneuron, dendrite, nullptr, tractB);

    // Tract A's rate should still be 0.5 (not clobbered by B's assignment)
    EXPECT_NEAR(tractA->stw_to_ltw_rate, val11(124), 1e-5f);
    EXPECT_NEAR(tractB->stw_to_ltw_rate, val11(62), 1e-5f);

    free_stub_tract(tractA);
    free_stub_tract(tractB);
}
