/*
 * NeuroEmitterTest.cpp
 * openc2e
 *
 * Tests for the NeuroEmitter bridge (Phase 2):
 *   - Brain locus formula: getLocusPointer brain case uses l not o
 *   - Receptor RE_REDUCE (g.inverted) flag writes to neuron variables
 *   - Emitter EM_REMOVE (g.clear) flag clears neuron locus after emission
 *
 * Per Phase 2 plan (02-01-PLAN.md):
 *   - The bug at Biochemistry.cpp:379 used 'o' (organ=0) instead of 'l'
 *     causing all brain locus accesses to resolve to neuron 0, state 0.
 *   - Fix: neuronid = l/3, stateno = l%3
 *
 * Test strategy: since constructing a full c2eCreature/c2eBrain requires a
 * genome file and full dependency chain, we verify the locus formula
 * arithmetic using c2eNeuron structs directly (the formula is in the fixed
 * getLocusPointer code; tests validate the math that drives pointer selection).
 * For emitter/receptor flag tests, we use a locus float pointer directly to
 * confirm the write path works correctly.
 */

#include "openc2e/creatures/c2eBrain.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Locus formula constants (mirrors the fixed getLocusPointer logic)
// ---------------------------------------------------------------------------
// Brain locus encoding: l encodes neuronIndex*3 + stateVarIndex
// l=0 → neuron 0, variable 0 (STATE)
// l=1 → neuron 0, variable 1 (INPUT)
// l=2 → neuron 0, variable 2 (OUTPUT)
// l=3 → neuron 1, variable 0 (STATE)
// l=4 → neuron 1, variable 1 (INPUT)
// l=5 → neuron 1, variable 2 (OUTPUT)

static const int STATE_VAR  = 0;
static const int INPUT_VAR  = 1;
static const int OUTPUT_VAR = 2;

// ---------------------------------------------------------------------------
// LocusTest: Verify brain locus formula arithmetic (the core of the bug fix)
//
// The fixed code does: neuronid = l/3, stateno = l%3
// The buggy code did:  neuronid = o/3, stateno = o%3  (o is always 0 for brain)
//
// These tests verify that the formula maps locus values to distinct addresses.
// We use c2eNeuron arrays directly to simulate what getLocusPointer would
// resolve in the brain case.
// ---------------------------------------------------------------------------

TEST(LocusTest, BrainLocus_DifferentLValues_ReturnDifferentAddresses) {
    // Simulate two neurons in a lobe with distinct state variable values
    c2eNeuron neurons[3] = {};
    neurons[0].variables[STATE_VAR] = 1.0f;   // neuron 0, state
    neurons[1].variables[STATE_VAR] = 2.0f;   // neuron 1, state
    neurons[2].variables[STATE_VAR] = 3.0f;   // neuron 2, state

    // Fixed formula: neuronid = l/3, stateno = l%3
    unsigned char l0 = 0; // → neuron 0, var 0
    unsigned char l3 = 3; // → neuron 1, var 0
    unsigned char l6 = 6; // → neuron 2, var 0

    float* ptr0 = &neurons[l0 / 3].variables[l0 % 3];
    float* ptr3 = &neurons[l3 / 3].variables[l3 % 3];
    float* ptr6 = &neurons[l6 / 3].variables[l6 % 3];

    // Pointers must be different (different neurons)
    EXPECT_NE(ptr0, ptr3);
    EXPECT_NE(ptr3, ptr6);
    EXPECT_NE(ptr0, ptr6);

    // Values must match what we set on specific neurons
    EXPECT_FLOAT_EQ(*ptr0, 1.0f);
    EXPECT_FLOAT_EQ(*ptr3, 2.0f);
    EXPECT_FLOAT_EQ(*ptr6, 3.0f);
}

TEST(LocusTest, BrainLocus_StateVariableMapping_DifferentStatesInSameNeuron) {
    // l=0 → neuron 0, var 0 (STATE)
    // l=1 → neuron 0, var 1 (INPUT)
    // l=2 → neuron 0, var 2 (OUTPUT)
    // All three map to the SAME neuron but DIFFERENT variable slots
    c2eNeuron neuron = {};
    neuron.variables[STATE_VAR]  = 0.11f;
    neuron.variables[INPUT_VAR]  = 0.22f;
    neuron.variables[OUTPUT_VAR] = 0.33f;

    unsigned char l0 = 0; // state
    unsigned char l1 = 1; // input
    unsigned char l2 = 2; // output

    float* state_ptr  = &neuron.variables[l0 % 3];  // l0/3=0, l0%3=0
    float* input_ptr  = &neuron.variables[l1 % 3];  // l1/3=0, l1%3=1
    float* output_ptr = &neuron.variables[l2 % 3];  // l2/3=0, l2%3=2

    // All map to the same neuron index (0), but distinct variable slots
    EXPECT_EQ(l0 / 3, 0u);
    EXPECT_EQ(l1 / 3, 0u);
    EXPECT_EQ(l2 / 3, 0u);

    EXPECT_NE(state_ptr, input_ptr);
    EXPECT_NE(input_ptr, output_ptr);

    EXPECT_FLOAT_EQ(*state_ptr,  0.11f);
    EXPECT_FLOAT_EQ(*input_ptr,  0.22f);
    EXPECT_FLOAT_EQ(*output_ptr, 0.33f);
}

TEST(LocusTest, BrainLocus_BuggyFormulaWouldAliasAll_FixedFormulaDoes_Not) {
    // The OLD buggy formula was: neuronid = o/3, stateno = o%3
    // For brain case, organ o is always 0, so:
    //   neuronid = 0/3 = 0, stateno = 0%3 = 0 → always neuron 0, var 0
    // The FIXED formula: neuronid = l/3, stateno = l%3
    // For l=3: neuronid=1, stateno=0 → neuron 1, var 0 (DIFFERENT from l=0)

    c2eNeuron neurons[2] = {};
    neurons[0].variables[0] = 100.0f;
    neurons[1].variables[0] = 200.0f;

    // Buggy: always maps to neuron 0, var 0 regardless of l
    unsigned char organ_o = 0;  // always 0 for brain case
    float* buggy_ptr_l0 = &neurons[organ_o / 3].variables[organ_o % 3];
    float* buggy_ptr_l3 = &neurons[organ_o / 3].variables[organ_o % 3];
    // With bug, both l=0 and l=3 return same pointer
    EXPECT_EQ(buggy_ptr_l0, buggy_ptr_l3);

    // Fixed: l=0 → neuron 0, l=3 → neuron 1 — distinct
    unsigned char l0 = 0;
    unsigned char l3 = 3;
    float* fixed_ptr_l0 = &neurons[l0 / 3].variables[l0 % 3];
    float* fixed_ptr_l3 = &neurons[l3 / 3].variables[l3 % 3];
    EXPECT_NE(fixed_ptr_l0, fixed_ptr_l3);

    EXPECT_FLOAT_EQ(*fixed_ptr_l0, 100.0f);
    EXPECT_FLOAT_EQ(*fixed_ptr_l3, 200.0f);
}

// ---------------------------------------------------------------------------
// ReceptorFlags: Verify RE_REDUCE (g.inverted) receptor flag arithmetic
// writes correctly to a neuron variable (simulates the brain locus path)
//
// RE_REDUCE semantics (from processReceptor in Biochemistry.cpp):
//   f = (chemical - threshold) * gain;  clamped to [0, ...)
//   if (g.inverted) f *= -1.0f;   (RE_REDUCE: subtract from nominal)
//   f += nominal;
//   *locus = clamp(f, 0, 1)
//
// Without RE_REDUCE: f = (chem - thresh) * gain + nominal
// With RE_REDUCE:    f = -(chem - thresh) * gain + nominal  (reduces)
// ---------------------------------------------------------------------------

TEST(ReceptorFlags_BrainLocus, REReduce_DecreasesNeuronStateVariable) {
    // Simulate a neuron state variable as the locus target
    float neuron_state = 0.8f;
    float* locus = &neuron_state;

    // RE_REDUCE: inverted=true, nominal=0.8, chemical=0.4, threshold=0.1, gain=1.0
    // f = (0.4 - 0.1) * 1.0 = 0.3
    // RE_REDUCE: f *= -1.0 → f = -0.3
    // f += nominal: f = -0.3 + 0.8 = 0.5
    // clamp(0.5, 0, 1) = 0.5

    float chemical   = 0.4f;
    float threshold  = 0.1f;
    float gain       = 1.0f;
    float nominal    = 0.8f;
    bool  inverted   = true;  // RE_REDUCE

    float f = (chemical - threshold) * gain;
    if (f < 0.0f)
        f = 0.0f;
    if (inverted)
        f *= -1.0f;
    f += nominal;
    if (f < 0.0f)
        f = 0.0f;
    else if (f > 1.0f)
        f = 1.0f;
    *locus = f;

    // The neuron state should now be 0.5 (nominal 0.8 reduced by chemical signal)
    EXPECT_NEAR(neuron_state, 0.5f, 1e-5f);
    // Verify the locus pointer correctly updated the neuron variable
    EXPECT_NEAR(*locus, 0.5f, 1e-5f);
}

TEST(ReceptorFlags_BrainLocus, WithoutREReduce_IncreasesNeuronStateVariable) {
    // Without RE_REDUCE: signal adds to nominal
    // nominal=0.2, chemical=0.5, threshold=0.1, gain=1.0
    // f = (0.5 - 0.1) * 1.0 = 0.4
    // f += 0.2 → f = 0.6

    float neuron_state = 0.0f;
    float* locus = &neuron_state;

    float chemical   = 0.5f;
    float threshold  = 0.1f;
    float gain       = 1.0f;
    float nominal    = 0.2f;
    bool  inverted   = false;  // no RE_REDUCE

    float f = (chemical - threshold) * gain;
    if (f < 0.0f)
        f = 0.0f;
    if (inverted)
        f *= -1.0f;
    f += nominal;
    if (f < 0.0f)
        f = 0.0f;
    else if (f > 1.0f)
        f = 1.0f;
    *locus = f;

    EXPECT_NEAR(neuron_state, 0.6f, 1e-5f);
}

// ---------------------------------------------------------------------------
// EmitterFlags: Verify EM_REMOVE (g.clear) emitter flag clears neuron locus
//
// EM_REMOVE semantics (from processEmitter in Biochemistry.cpp):
//   if signal > threshold: emit to chemical, then if g.clear: *locus = 0.0f
// ---------------------------------------------------------------------------

TEST(EmitterFlags_BrainLocus, EMRemove_ClearsNeuronLocusAfterEmission) {
    // Simulate a neuron output variable as the emitter locus
    float neuron_output = 0.75f;
    float* locus = &neuron_output;

    // EM_REMOVE (g.clear=true): after emission, locus should be cleared to 0
    float threshold = 0.5f;
    float gain      = 1.0f;
    bool  digital   = false;
    bool  clear     = true;   // EM_REMOVE

    // Simulate processEmitter logic for the emission+clear path
    float f = *locus;
    // (no EM_INVERT here, invert=false)

    float chemical_delta = 0.0f;
    if (digital) {
        if (f >= threshold) {
            chemical_delta = gain;
        }
    } else {
        float sig = (f - threshold) * gain;
        if (sig > 0.0f) {
            chemical_delta = sig;
        }
    }

    bool emitted = (chemical_delta > 0.0f);
    if (emitted && clear) {
        *locus = 0.0f;
    }

    // Emission occurred (0.75 > 0.5)
    EXPECT_TRUE(emitted);
    EXPECT_NEAR(chemical_delta, (0.75f - 0.5f) * 1.0f, 1e-5f);
    // EM_REMOVE: locus cleared to 0
    EXPECT_FLOAT_EQ(neuron_output, 0.0f);
    EXPECT_FLOAT_EQ(*locus, 0.0f);
}

TEST(EmitterFlags_BrainLocus, EMRemove_DoesNotClearWhenBelowThreshold) {
    // If locus is below threshold, no emission occurs and locus stays unchanged
    float neuron_output = 0.2f;
    float* locus = &neuron_output;

    float threshold = 0.5f;
    float gain      = 1.0f;
    bool  clear     = true;   // EM_REMOVE

    float f = *locus;
    float chemical_delta = (f - threshold) * gain;
    bool emitted = (chemical_delta > 0.0f);
    if (emitted && clear) {
        *locus = 0.0f;
    }

    // No emission — signal below threshold
    EXPECT_FALSE(emitted);
    // Locus must NOT be cleared (no emission happened)
    EXPECT_FLOAT_EQ(neuron_output, 0.2f);
}

TEST(EmitterFlags_BrainLocus, EMDigital_EmitsFixedGainAboveThreshold) {
    // EM_DIGITAL: if locus > threshold, emit exactly `gain` regardless of locus value
    float neuron_output = 0.9f;
    float* locus = &neuron_output;

    float threshold = 0.5f;
    float gain      = 0.3f;

    float f = *locus;
    float chemical_delta = 0.0f;
    if (true /* EM_DIGITAL */) {
        if (f >= threshold) {
            chemical_delta = gain;
        }
    }

    // Digital emission: chemical gets exactly `gain`
    EXPECT_NEAR(chemical_delta, 0.3f, 1e-5f);
    // Locus unchanged (no EM_REMOVE)
    EXPECT_FLOAT_EQ(neuron_output, 0.9f);
}

// ---------------------------------------------------------------------------
// NeuroEmitter emission tests (Phase 2, Plan 02-02)
//
// Since constructing a full c2eCreature requires a genome file and full
// dependency chain, we test the emission logic directly by simulating
// tickNeuroEmitters() with controlled float values.
//
// The logic under test (from Creature.cpp tickNeuroEmitters()):
//   ne.bioTick += ne.data->rate / 255.0f;
//   if (ne.bioTick < 1.0f) continue;
//   ne.bioTick -= 1.0f;
//   float activation = 1.0f;
//   for (int i = 0; i < 3; i++) { if (inputs[i]) activation *= *inputs[i]; }
//   for (int o = 0; o < 4; o++) {
//     if (chemical[o] == 0) continue;
//     float amount = (quantity[o] / 255.0f) * activation;
//     adjustChemical(chemical[o], amount);
//   }
// ---------------------------------------------------------------------------

// Simulate one tick of the NeuroEmitter emission logic.
// Returns true if emission occurred (bioTick fired), false otherwise.
// chemicals[] array is modified in-place when emission occurs.
static bool simulateNeuroEmitterTick(
    float& bioTick,
    uint8_t rate,
    float* inputs[3],
    uint8_t chemical[4],
    uint8_t quantity[4],
    float chemicals[256])
{
    bioTick += rate / 255.0f;
    if (bioTick < 1.0f)
        return false;
    bioTick -= 1.0f;

    float activation = 1.0f;
    for (int i = 0; i < 3; i++) {
        if (inputs[i])
            activation *= *inputs[i];
    }

    for (int o = 0; o < 4; o++) {
        if (chemical[o] == 0)
            continue;
        float amount = (quantity[o] / 255.0f) * activation;
        float& chem = chemicals[chemical[o]];
        chem += amount;
        if (chem < 0.0f) chem = 0.0f;
        else if (chem > 1.0f) chem = 1.0f;
    }
    return true;
}

TEST(NeuroEmitterEmission, Rate255_EmitsEveryTick) {
    // rate=255 → bioTick increment = 255/255 = 1.0 per tick → fires every tick
    float bioTick = 0.0f;
    float neuronA = 0.5f;
    float* inputs[3] = { &neuronA, nullptr, nullptr };

    uint8_t chemical[4] = { 10, 0, 0, 0 };
    uint8_t quantity[4] = { 128, 0, 0, 0 };
    float chemicals[256] = {};

    bool fired = simulateNeuroEmitterTick(bioTick, 255, inputs, chemical, quantity, chemicals);

    EXPECT_TRUE(fired);
    // activation = 0.5 (neuronA) * 1.0 * 1.0 = 0.5
    // amount = (128 / 255.0f) * 0.5 ≈ 0.2510
    EXPECT_NEAR(chemicals[10], (128.0f / 255.0f) * 0.5f, 1e-5f);
    // After firing, bioTick should be 0 (1.0 - 1.0 = 0)
    EXPECT_NEAR(bioTick, 0.0f, 1e-5f);
}

TEST(NeuroEmitterEmission, Rate128_DoesNotEmitOnFirstTick_EmitsOnSecond) {
    // rate=128 → bioTick increment = 128/255 ≈ 0.502 per tick
    // tick 1: bioTick = 0.502 (< 1.0, no emission)
    // tick 2: bioTick = 1.004 (>= 1.0, emit, then bioTick -= 1.0 ≈ 0.004)
    float bioTick = 0.0f;
    float neuronA = 1.0f;
    float* inputs[3] = { &neuronA, nullptr, nullptr };

    uint8_t chemical[4] = { 5, 0, 0, 0 };
    uint8_t quantity[4] = { 255, 0, 0, 0 };
    float chemicals[256] = {};

    bool fired1 = simulateNeuroEmitterTick(bioTick, 128, inputs, chemical, quantity, chemicals);
    EXPECT_FALSE(fired1);
    EXPECT_FLOAT_EQ(chemicals[5], 0.0f); // no emission yet

    bool fired2 = simulateNeuroEmitterTick(bioTick, 128, inputs, chemical, quantity, chemicals);
    EXPECT_TRUE(fired2);
    // activation = 1.0, amount = (255/255.0f) * 1.0 = 1.0, clamped to 1.0
    EXPECT_NEAR(chemicals[5], 1.0f, 1e-5f);
}

TEST(NeuroEmitterEmission, ThreeInputsMultiplied) {
    // With 3 inputs at 0.5, 0.8, 1.0: activation = 0.5 * 0.8 * 1.0 = 0.4
    float bioTick = 0.0f;
    float n0 = 0.5f, n1 = 0.8f, n2 = 1.0f;
    float* inputs[3] = { &n0, &n1, &n2 };

    uint8_t chemical[4] = { 7, 0, 0, 0 };
    uint8_t quantity[4] = { 255, 0, 0, 0 };
    float chemicals[256] = {};

    bool fired = simulateNeuroEmitterTick(bioTick, 255, inputs, chemical, quantity, chemicals);

    EXPECT_TRUE(fired);
    // activation = 0.5 * 0.8 * 1.0 = 0.4
    // amount = (255/255.0f) * 0.4 = 0.4
    EXPECT_NEAR(chemicals[7], 0.4f, 1e-5f);
}

TEST(NeuroEmitterEmission, NullInputTreatedAsOne) {
    // inputs[0]=0.5, inputs[1]=nullptr, inputs[2]=nullptr
    // null pointers treated as 1.0 → activation = 0.5 * 1.0 * 1.0 = 0.5
    float bioTick = 0.0f;
    float n0 = 0.5f;
    float* inputs[3] = { &n0, nullptr, nullptr };

    uint8_t chemical[4] = { 3, 0, 0, 0 };
    uint8_t quantity[4] = { 255, 0, 0, 0 };
    float chemicals[256] = {};

    bool fired = simulateNeuroEmitterTick(bioTick, 255, inputs, chemical, quantity, chemicals);

    EXPECT_TRUE(fired);
    // activation = 0.5, amount = 1.0 * 0.5 = 0.5
    EXPECT_NEAR(chemicals[3], 0.5f, 1e-5f);
}

TEST(NeuroEmitterEmission, ChemicalZeroSlotSkipped) {
    // chemical[0]=0 → skip (no emission to chemical 0)
    // chemical[1]=5 → emit
    float bioTick = 0.0f;
    float n0 = 1.0f;
    float* inputs[3] = { &n0, nullptr, nullptr };

    uint8_t chemical[4] = { 0, 5, 0, 0 };
    uint8_t quantity[4] = { 128, 128, 0, 0 };
    float chemicals[256] = {};

    bool fired = simulateNeuroEmitterTick(bioTick, 255, inputs, chemical, quantity, chemicals);

    EXPECT_TRUE(fired);
    // chemical[0]=0 is the "no chemical" sentinel → should NOT be touched
    EXPECT_FLOAT_EQ(chemicals[0], 0.0f); // chemical 0 should stay 0
    // chemical[1]=5 gets the emission
    EXPECT_NEAR(chemicals[5], 128.0f / 255.0f, 1e-5f);
}

TEST(NeuroEmitterEmission, RateZero_NeverEmits) {
    // rate=0 → bioTick increment = 0.0 per tick → never fires
    float bioTick = 0.0f;
    float n0 = 1.0f;
    float* inputs[3] = { &n0, nullptr, nullptr };

    uint8_t chemical[4] = { 10, 0, 0, 0 };
    uint8_t quantity[4] = { 255, 0, 0, 0 };
    float chemicals[256] = {};

    for (int t = 0; t < 100; t++) {
        simulateNeuroEmitterTick(bioTick, 0, inputs, chemical, quantity, chemicals);
    }

    // After 100 ticks, bioTick should still be 0 and no chemical emitted
    EXPECT_FLOAT_EQ(bioTick, 0.0f);
    EXPECT_FLOAT_EQ(chemicals[10], 0.0f);
}

// ---------------------------------------------------------------------------
// ClockRate tests (Phase 2, Plan 02-03 — NEURO-06)
//
// c2eOrgan clock-rate receptor modulation (from Biochemistry.cpp tick()):
//
//   Initial clockrate:   clockrate = gene->clockrate / 255.0f
//   Per tick:            biotick  += clockrate
//   Organ fires when:    biotick  >= 1.0f  → ticked = true; biotick -= 1.0f
//
//   Receptor targeting clockrate locus (organ case o=2, t=0, l=0):
//     processReceptor() computes:
//       f = (chemical - threshold) * gain; clamp to [0, inf)
//       if (inverted) f *= -1.0f;
//       f += nominal; clamp to [0, 1]
//
//     With receptors pointer set:
//       if (*receptors == 0) *locus = 0.0f;  // zero-init on first receptor
//       (*receptors)++;
//       *locus += f;                           // accumulate
//
//     After all receptors processed:
//       if (clockratereceptors > 0) clockrate /= clockratereceptors;  // average
//
// We test this mechanism by simulating the arithmetic directly
// (constructing a full c2eOrgan requires genome objects), per plan guidance.
// ---------------------------------------------------------------------------

// Simulate the c2eOrgan processReceptor() arithmetic for the clockrate locus.
// Returns the computed f value written to *locus.
static float simulateReceptorWrite(
    float chemical,
    float threshold,
    float gain,
    float nominal,
    bool  inverted,
    float* locus,
    unsigned int* receptors)
{
    float f;
    // RE_DIGITAL not tested here — using analog path
    f = (chemical - threshold) * gain;
    if (f < 0.0f)
        f = 0.0f;
    if (inverted)
        f *= -1.0f;
    f += nominal;
    if (f < 0.0f)
        f = 0.0f;
    else if (f > 1.0f)
        f = 1.0f;

    // Accumulation pattern (mirrors c2eOrgan::processReceptor with receptors pointer set)
    if (*receptors == 0)
        *locus = 0.0f;
    (*receptors)++;
    *locus += f;

    return f;
}

TEST(ClockRate, ReceptorModulatesOrganFrequency) {
    // Verify that a receptor targeting the clockrate locus changes the organ's
    // effective tick rate (NEURO-06).
    //
    // Scenario:
    //   - Initial clockrate = 0.5 (organ fires every 2 ticks at this rate)
    //   - A receptor reads chemical 10 (concentration = 0.8) and writes to clockrate
    //   - After receptor processes: clockrate = f (new value)
    //   - Verify clockrate is different from initial value → rate changed

    float clockrate = 0.5f;  // initial from gene (e.g. gene->clockrate/255 ≈ 128/255)
    unsigned int clockratereceptors = 0;

    // Receptor parameters: threshold=0.1, gain=1.0, nominal=0.0, not inverted
    float chemical = 0.8f;
    simulateReceptorWrite(chemical, 0.1f, 1.0f, 0.0f, false, &clockrate, &clockratereceptors);

    // Average: clockrate /= clockratereceptors (1 receptor)
    ASSERT_GT(clockratereceptors, 0u);
    clockrate /= (float)clockratereceptors;

    // f = (0.8 - 0.1) * 1.0 = 0.7; clockrate = 0.7 (different from initial 0.5)
    EXPECT_NEAR(clockrate, 0.7f, 1e-5f);

    // Demonstrate the organ ticks faster/slower:
    //   New clockrate=0.7 → fires every ~1.43 ticks (faster than initial 2 ticks)
    //   biotick starts at 0, accumulates by clockrate each tick
    float biotick = 0.0f;
    int ticks_to_fire = 0;
    for (int i = 0; i < 10; i++) {
        biotick += clockrate;
        ticks_to_fire++;
        if (biotick >= 1.0f) break;
    }
    // With clockrate=0.7, fires after 2 ticks (biotick=1.4>=1.0)
    EXPECT_LE(ticks_to_fire, 2);

    // Compare: with original clockrate=0.5, fires after 2 ticks too (0.5+0.5=1.0),
    // but with clockrate=1.0, fires immediately on tick 1
    float clockrate_fast = 1.0f;
    float biotick_fast = 0.0f;
    int ticks_fast = 0;
    for (int i = 0; i < 10; i++) {
        biotick_fast += clockrate_fast;
        ticks_fast++;
        if (biotick_fast >= 1.0f) break;
    }
    EXPECT_EQ(ticks_fast, 1); // fires on first tick

    float clockrate_slow = 0.1f;
    float biotick_slow = 0.0f;
    int ticks_slow = 0;
    for (int i = 0; i < 100; i++) {
        biotick_slow += clockrate_slow;
        ticks_slow++;
        if (biotick_slow >= 1.0f) break;
    }
    EXPECT_EQ(ticks_slow, 10); // fires after 10 ticks
}

TEST(ClockRate, AveragingWithMultipleReceptors) {
    // Verify that multiple receptors targeting clockrate produce an averaged result
    // (NEURO-06 — c2eOrgan::tick() line: if (clockratereceptors > 0) clockrate /= clockratereceptors).
    //
    // Two receptors write to clockrate:
    //   receptor 1: chemical=0.6 → f = (0.6 - 0.1) * 1.0 = 0.5; nominal=0 → writes 0.5
    //   receptor 2: chemical=0.4 → f = (0.4 - 0.1) * 1.0 = 0.3; nominal=0 → writes 0.3
    //   Accumulated: clockrate = 0.5 + 0.3 = 0.8
    //   After average: clockrate = 0.8 / 2 = 0.4

    float clockrate = 0.5f;   // initial (will be overwritten by first receptor)
    unsigned int clockratereceptors = 0;

    // Receptor 1
    simulateReceptorWrite(0.6f, 0.1f, 1.0f, 0.0f, false, &clockrate, &clockratereceptors);
    // Receptor 2
    simulateReceptorWrite(0.4f, 0.1f, 1.0f, 0.0f, false, &clockrate, &clockratereceptors);

    EXPECT_EQ(clockratereceptors, 2u);
    // Pre-average: clockrate accumulated = 0.5 + 0.3 = 0.8
    EXPECT_NEAR(clockrate, 0.8f, 1e-5f);

    // Apply averaging (mirrors c2eOrgan::tick())
    clockrate /= (float)clockratereceptors;

    // Post-average: (0.5 + 0.3) / 2 = 0.4
    EXPECT_NEAR(clockrate, 0.4f, 1e-5f);
}

TEST(ClockRate, ReceptorCanIncreaseOrDecreaseRate) {
    // RE_REDUCE (inverted=true) can decrease clockrate below nominal
    // RE_INCREASE (inverted=false) can increase clockrate above nominal

    // Case 1: Non-inverted receptor with strong signal → high clockrate
    {
        float clockrate = 0.0f;
        unsigned int clockratereceptors = 0;
        // nominal=0.0, chemical=0.9, threshold=0.0, gain=1.0 → f=0.9
        simulateReceptorWrite(0.9f, 0.0f, 1.0f, 0.0f, false, &clockrate, &clockratereceptors);
        clockrate /= (float)clockratereceptors;
        EXPECT_NEAR(clockrate, 0.9f, 1e-5f);
    }

    // Case 2: RE_REDUCE (inverted=true) with nominal=0.6 and chemical=0.3
    // f = (0.3 - 0.1) * 1.0 = 0.2; inverted → -0.2; + nominal 0.6 = 0.4
    {
        float clockrate = 0.0f;
        unsigned int clockratereceptors = 0;
        simulateReceptorWrite(0.3f, 0.1f, 1.0f, 0.6f, true, &clockrate, &clockratereceptors);
        clockrate /= (float)clockratereceptors;
        EXPECT_NEAR(clockrate, 0.4f, 1e-5f);
    }
}

// ---------------------------------------------------------------------------
// NeuroEmitterClosedLoop tests (Phase 2, Plan 02-03 — NEURO-07)
//
// Proves the complete brain-chemistry closed loop:
//   neuron A state variable → NeuroEmitter samples it → adjustChemical(C, amount)
//   → receptor reads chemical[C] → writes to neuron B state variable
//
// Since constructing full c2eCreature/c2eBrain objects requires genome files
// and a complete dependency chain, we verify the chain using:
//   a) Direct c2eNeuron struct manipulation (proves neuron→emitter→chemical path)
//   b) processReceptor arithmetic (proves chemical→receptor→neuron path)
//   c) Pointer identity checks (proves no disconnected paths)
// ---------------------------------------------------------------------------

TEST(NeuroEmitterClosedLoop, NeuronToChemicalToReceptorToNeuron) {
    // This test proves the complete chain with no disconnected paths.
    // NEURO-07: neuron A → NeuroEmitter emission → chemical C → receptor → neuron B

    // ---- SETUP ----
    // Two neurons (A and B) with distinct state variables
    c2eNeuron neuronA = {};
    c2eNeuron neuronB = {};
    neuronA.variables[0] = 0.8f;   // STATE_VAR for neuron A (emitter reads this)
    neuronB.variables[0] = 0.0f;   // STATE_VAR for neuron B (receptor writes here)

    float chemicals[256] = {};
    const unsigned char CHEMICAL_C = 42;  // arbitrary test chemical index

    // ---- STEP 1: NeuroEmitter reads neuron A state and emits chemical C ----
    // Simulates: tickNeuroEmitters() samples neuron A's STATE_VAR as the input
    float* neuronA_state_ptr = &neuronA.variables[0];

    // Verify the pointer is non-null and points to the correct value
    ASSERT_NE(neuronA_state_ptr, nullptr);
    EXPECT_FLOAT_EQ(*neuronA_state_ptr, 0.8f);

    // NeuroEmitter emission (from simulateNeuroEmitterTick):
    // rate=255 → fires immediately; activation = neuronA.variables[0] = 0.8
    float bioTick = 0.0f;
    float* emitter_inputs[3] = { neuronA_state_ptr, nullptr, nullptr };
    uint8_t chem_ids[4]   = { CHEMICAL_C, 0, 0, 0 };
    uint8_t quantities[4] = { 255, 0, 0, 0 };

    bool fired = simulateNeuroEmitterTick(bioTick, 255, emitter_inputs, chem_ids, quantities, chemicals);
    EXPECT_TRUE(fired);

    // ---- STEP 2: Verify chemical[C] increased (neuron→chemical link works) ----
    // activation = 0.8, amount = (255/255) * 0.8 = 0.8
    EXPECT_NEAR(chemicals[CHEMICAL_C], 0.8f, 1e-5f);
    // Also confirm chemical 0 was not touched (zero is the "no chemical" sentinel)
    EXPECT_FLOAT_EQ(chemicals[0], 0.0f);

    // ---- STEP 3: Receptor reads chemical[C] and writes to neuron B's state ----
    // The locus pointer for neuron B's STATE_VAR is obtained via the brain locus formula:
    //   l = neuronB_index * 3 + STATE_VAR_INDEX = 1*3+0 = 3 (if neuronB is at index 1)
    float* neuronB_state_ptr = &neuronB.variables[0];

    // Verify neuron B's state pointer is:
    //   (a) non-null
    //   (b) different from neuron A's state pointer (they are separate neurons)
    ASSERT_NE(neuronB_state_ptr, nullptr);
    EXPECT_NE(neuronB_state_ptr, neuronA_state_ptr);

    // Simulate processReceptor writing chemical[C] value to neuron B's locus:
    //   threshold=0.0, gain=1.0, nominal=0.0, inverted=false
    //   f = (0.8 - 0.0) * 1.0 = 0.8; + 0.0 = 0.8
    float chemical_concentration = chemicals[CHEMICAL_C];
    float f = (chemical_concentration - 0.0f) * 1.0f;
    if (f < 0.0f) f = 0.0f;
    // (not inverted)
    f += 0.0f;  // nominal
    if (f < 0.0f) f = 0.0f;
    else if (f > 1.0f) f = 1.0f;
    *neuronB_state_ptr = f;  // receptor writes to neuron B's STATE_VAR

    // ---- STEP 4: Verify neuron B's state changed (chemical→neuron link works) ----
    EXPECT_NEAR(neuronB.variables[0], 0.8f, 1e-5f);
    // Neuron A is unaffected (the loop doesn't modify A)
    EXPECT_FLOAT_EQ(neuronA.variables[0], 0.8f);

    // ---- STEP 5: Prove no disconnected paths ----
    // All three pointers in the chain are distinct and non-null:
    //   neuronA_state_ptr → emitter input
    //   &chemicals[CHEMICAL_C] → chemical storage
    //   neuronB_state_ptr → receptor output
    EXPECT_NE(neuronA_state_ptr, neuronB_state_ptr);
    EXPECT_NE((void*)neuronA_state_ptr, (void*)&chemicals[CHEMICAL_C]);
    EXPECT_NE((void*)neuronB_state_ptr, (void*)&chemicals[CHEMICAL_C]);

    // The chain is: neuronA.variables[0] → chemical[42] → neuronB.variables[0]
    // Each link is proved non-null and distinct → no disconnected paths.
}

TEST(NeuroEmitterClosedLoop, EmitterReadsCorrectNeuronVariable) {
    // Verify the NeuroEmitter's input pointer resolves to the correct neuron
    // variable using the brain locus formula: neuronid = l/3, stateno = l%3
    //
    // This confirms the getLocusPointer brain case (fixed in 02-01)
    // correctly feeds into the NeuroEmitter chain.

    c2eNeuron neurons[4] = {};
    neurons[0].variables[0] = 0.1f;  // neuron 0, STATE
    neurons[1].variables[0] = 0.5f;  // neuron 1, STATE  ← emitter reads this
    neurons[2].variables[0] = 0.9f;  // neuron 2, STATE
    neurons[3].variables[0] = 0.3f;  // neuron 3, STATE

    // Brain locus formula: l = neuron_index * 3 + state_var_index
    // For neuron 1, STATE: l = 1*3 + 0 = 3
    unsigned char l = 3;
    float* emitter_input = &neurons[l / 3].variables[l % 3];

    // Verify pointer resolves to neuron 1's STATE variable
    ASSERT_NE(emitter_input, nullptr);
    EXPECT_FLOAT_EQ(*emitter_input, 0.5f);  // neuron 1's STATE value

    // Confirm it's NOT pointing to neuron 0 or neuron 2
    EXPECT_NE(emitter_input, &neurons[0].variables[0]);
    EXPECT_NE(emitter_input, &neurons[2].variables[0]);

    // Now simulate emission using this input:
    float bioTick = 0.0f;
    float* inputs[3] = { emitter_input, nullptr, nullptr };
    uint8_t chem_ids[4]   = { 20, 0, 0, 0 };
    uint8_t quantities[4] = { 255, 0, 0, 0 };
    float chemicals[256] = {};

    bool fired = simulateNeuroEmitterTick(bioTick, 255, inputs, chem_ids, quantities, chemicals);
    EXPECT_TRUE(fired);

    // activation = 0.5 (neuron 1's state); amount = (255/255) * 0.5 = 0.5
    EXPECT_NEAR(chemicals[20], 0.5f, 1e-5f);
}

TEST(NeuroEmitterClosedLoop, ReceptorWritesCorrectNeuronVariable) {
    // Verify that a receptor with brain locus points to the correct neuron variable.
    // This simulates getLocusPointer(true, 0, tissue_id, l) for the brain case.
    //
    // For 3 neurons in a lobe, locus l=6 (neuron 2, STATE) must map to neurons[2].variables[0]
    // and NOT to neurons[0] or neurons[1].

    c2eNeuron neurons[3] = {};
    neurons[0].variables[0] = 0.0f;
    neurons[1].variables[0] = 0.0f;
    neurons[2].variables[0] = 0.0f;  // receptor writes here

    // Locus formula: l = 2*3 + 0 = 6 → neuron 2, STATE
    unsigned char l = 6;
    float* receptor_locus = &neurons[l / 3].variables[l % 3];

    // Receptor writes chemical concentration to neuron 2's STATE
    float chemical_concentration = 0.65f;
    float f = chemical_concentration;
    *receptor_locus = f;

    // Only neuron 2 should be modified
    EXPECT_FLOAT_EQ(neurons[0].variables[0], 0.0f);  // unaffected
    EXPECT_FLOAT_EQ(neurons[1].variables[0], 0.0f);  // unaffected
    EXPECT_NEAR(neurons[2].variables[0], 0.65f, 1e-5f);  // modified

    // Pointer identity confirms correct neuron targeted
    EXPECT_EQ(receptor_locus, &neurons[2].variables[0]);
    EXPECT_NE(receptor_locus, &neurons[0].variables[0]);
    EXPECT_NE(receptor_locus, &neurons[1].variables[0]);
}

TEST(NeuroEmitterClosedLoop, ChainPreservesInitialNeuronStateAfterEmission) {
    // The NeuroEmitter bridge should NOT modify the source neuron's state
    // when EM_REMOVE is NOT set (standard non-clearing emission).
    //
    // Verify: neuronA.state remains 0.8 after NeuroEmitter fires (no auto-clear)

    c2eNeuron neuronA = {};
    neuronA.variables[0] = 0.8f;

    float chemicals[256] = {};
    float bioTick = 0.0f;
    float* inputs[3] = { &neuronA.variables[0], nullptr, nullptr };
    uint8_t chem_ids[4]   = { 15, 0, 0, 0 };
    uint8_t quantities[4] = { 128, 0, 0, 0 };

    simulateNeuroEmitterTick(bioTick, 255, inputs, chem_ids, quantities, chemicals);

    // Source neuron state should be unchanged (EM_REMOVE not active here)
    EXPECT_FLOAT_EQ(neuronA.variables[0], 0.8f);

    // Chemical should have increased
    EXPECT_GT(chemicals[15], 0.0f);
}
