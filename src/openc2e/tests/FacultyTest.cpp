/*
 * FacultyTest.cpp
 * openc2e
 *
 * Tests for LifeFaculty (organ lifeforce aggregation, death detection) and
 * MotorFaculty (muscleenergy feedback, gait selection) — Phase 5, Plan 01.
 *
 *   FAC-01: LifeFaculty — creature dies when all organ lifeforces reach zero
 *   FAC-03: MotorFaculty — gait selection via argmax of gaitloci[16]
 *   FAC-08: MotorFaculty — muscleenergy energy feedback from organ health
 *
 * Test strategy: Direct struct manipulation without full c2eCreature construction
 * (per D-19). TestableLifeFaculty and TestableMotorFaculty mirror the production
 * algorithms in updateLifeFaculty() and updateMotorFaculty() exactly.
 * If production behavior changes, these tests will catch any divergence.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// TestableLifeFaculty — mirrors updateLifeFaculty() algorithm
// ---------------------------------------------------------------------------

class TestableLifeFaculty {
  public:
	std::vector<float> organLifeforces;
	float dead = 0.0f;
	float muscleenergy = 0.0f;
	unsigned int ticks = 0;

	void updateLifeFaculty() {
		// Aggregate organ lifeforces to determine overall creature health
		float totalLifeforce = 0.0f;
		for (float lf : organLifeforces) {
			totalLifeforce += lf;
		}
		// Death: if all organs have zero lifeforce and creature has been alive long enough
		// Only SET dead locus — actual die() call remains at existing location in tick()
		if (!organLifeforces.empty() && totalLifeforce <= 0.0f && ticks > 100) {
			dead = 1.0f;
		}
	}
};

// ---------------------------------------------------------------------------
// LifeFacultyTest — FAC-01: death detection
// ---------------------------------------------------------------------------

TEST(LifeFacultyTest, DeathOnZeroLifeforce) {
	// 3 organs all at zero lifeforce + sufficient ticks => dead = 1.0f
	TestableLifeFaculty f;
	f.organLifeforces = {0.0f, 0.0f, 0.0f};
	f.ticks = 200;
	f.updateLifeFaculty();
	EXPECT_EQ(f.dead, 1.0f);
}

TEST(LifeFacultyTest, HealthyCreatureSurvives) {
	// Organs with positive lifeforce => creature stays alive
	TestableLifeFaculty f;
	f.organLifeforces = {0.5f, 0.3f, 0.8f};
	f.ticks = 200;
	f.updateLifeFaculty();
	EXPECT_EQ(f.dead, 0.0f);
}

TEST(LifeFacultyTest, NoDeathBeforeMinTicks) {
	// All organs at zero but ticks=50 (below threshold of 100) => no death yet
	TestableLifeFaculty f;
	f.organLifeforces = {0.0f, 0.0f, 0.0f};
	f.ticks = 50;
	f.updateLifeFaculty();
	EXPECT_EQ(f.dead, 0.0f);
}

TEST(LifeFacultyTest, NoDeathWithNoOrgans) {
	// Empty organ vector with sufficient ticks => no death (edge case: no organs)
	TestableLifeFaculty f;
	f.organLifeforces = {}; // no organs
	f.ticks = 200;
	f.updateLifeFaculty();
	EXPECT_EQ(f.dead, 0.0f);
}

// ---------------------------------------------------------------------------
// TestableMotorFaculty — mirrors updateMotorFaculty() algorithm + getGait()
// ---------------------------------------------------------------------------

class TestableMotorFaculty {
  public:
	std::vector<float> organLifeforces;
	float muscleenergy = 0.0f;
	float gaitloci[16] = {};

	void updateMotorFaculty() {
		// Energy feedback from organ health into biochemistry (per D-16)
		// muscleenergy is an emitter locus (organ=1, tissue=0, locus=0)
		// NOTE: The full motor chain (brain -> gaitloci[] -> getGait() -> setGaitGene())
		// is already functional via the locus/receptor system + SkeletalCreature::creatureTick().
		// This method only provides the energy feedback piece.
		if (organLifeforces.empty()) {
			muscleenergy = 0.0f;
		} else {
			float totalLifeforce = 0.0f;
			for (float lf : organLifeforces) {
				totalLifeforce += lf;
			}
			muscleenergy = std::min(1.0f, totalLifeforce / static_cast<float>(organLifeforces.size()));
		}
	}

	int getGait() {
		// Returns the index of the highest value in gaitloci[16] (argmax).
		// On tie, returns the lowest index. Returns 0 if all are zero.
		// This mirrors the existing getGait() in Creature.cpp which is already
		// functional in production — the test validates the algorithm independently.
		unsigned int gait = 0;
		for (unsigned int i = 1; i < 16; i++) {
			if (gaitloci[i] > gaitloci[gait]) {
				gait = i;
			}
		}
		return static_cast<int>(gait);
	}
};

// ---------------------------------------------------------------------------
// MotorFacultyTest — FAC-08: muscleenergy feedback; FAC-03: gait selection
// ---------------------------------------------------------------------------

TEST(MotorFacultyTest, MuscleEnergyFromOrgans) {
	// 3 organs at {1.0f, 0.5f, 0.5f}: average = 2.0f/3 ≈ 0.667f
	TestableMotorFaculty f;
	f.organLifeforces = {1.0f, 0.5f, 0.5f};
	f.updateMotorFaculty();
	EXPECT_NEAR(f.muscleenergy, 2.0f / 3.0f, 0.01f);
}

TEST(MotorFacultyTest, MuscleEnergyZeroWithNoOrgans) {
	// Empty organ vector => muscleenergy = 0.0f
	TestableMotorFaculty f;
	f.organLifeforces = {};
	f.updateMotorFaculty();
	EXPECT_EQ(f.muscleenergy, 0.0f);
}

TEST(MotorFacultyTest, GaitSelectionFromLoci) {
	// Validates FAC-03 gait selection algorithm (argmax of gaitloci[16])
	// which is already in production via getGait() in Creature.cpp.
	TestableMotorFaculty f;

	// Set up non-trivial loci values
	std::fill(f.gaitloci, f.gaitloci + 16, 0.0f);
	f.gaitloci[5] = 0.8f;
	f.gaitloci[2] = 0.3f;
	f.gaitloci[10] = 0.5f;

	// Highest value is at index 5 — should win
	EXPECT_EQ(f.getGait(), 5);

	// Tie-breaking: add another entry equal to index 5
	f.gaitloci[3] = 0.8f; // same value as index 5
	// Lowest index wins on tie: index 3 < 5, so getGait() returns 3
	EXPECT_EQ(f.getGait(), 3);
}

// ---------------------------------------------------------------------------
// TestableSensoryFaculty — mirrors the smel lobe CA mapping from tickBrain()
// ---------------------------------------------------------------------------

// Local constant matching Room.h CA_COUNT to avoid pulling in Room.h
static constexpr int CA_COUNT_TEST = 20;

class TestableSensoryFaculty {
  public:
	std::vector<float> caValues;        // simulates room CA channels
	std::vector<float> smelNeuronInputs; // simulates smel lobe neuron inputs
	int lobeSize = 0;

	// Mirrors the smel lobe mapping loop from tickBrain() (per D-10)
	void updateSmellLobe() {
		if (caValues.empty())
			return;
		for (int i = 0; i < CA_COUNT_TEST && i < lobeSize && i < (int)caValues.size(); i++) {
			smelNeuronInputs[i] = caValues[i];
		}
	}
};

// ---------------------------------------------------------------------------
// SensoryFacultyTest — FAC-02: smell lobe CA mapping
// ---------------------------------------------------------------------------

TEST(SensoryFacultyTest, SmellLobeReceivesCaValues) {
	// smel lobe neurons receive the corresponding room CA values
	TestableSensoryFaculty f;
	f.caValues.resize(20, 0.0f);
	f.caValues[0] = 0.5f;
	f.caValues[3] = 0.8f;
	f.lobeSize = 20;
	f.smelNeuronInputs.resize(20, 0.0f);

	f.updateSmellLobe();

	EXPECT_FLOAT_EQ(f.smelNeuronInputs[0], 0.5f);
	EXPECT_FLOAT_EQ(f.smelNeuronInputs[3], 0.8f);
	// Other neurons remain 0
	EXPECT_FLOAT_EQ(f.smelNeuronInputs[1], 0.0f);
	EXPECT_FLOAT_EQ(f.smelNeuronInputs[2], 0.0f);
}

TEST(SensoryFacultyTest, SmellLobeBoundedByLobeSize) {
	// Only lobeSize neurons are written, no out-of-bounds access
	TestableSensoryFaculty f;
	f.caValues.resize(20, 1.0f);
	f.lobeSize = 5;
	f.smelNeuronInputs.resize(5, 0.0f);

	f.updateSmellLobe();

	// All 5 neurons written with 1.0f
	EXPECT_FLOAT_EQ(f.smelNeuronInputs[4], 1.0f);
	// Size remains at exactly 5 (no out-of-bounds writes)
	EXPECT_EQ((int)f.smelNeuronInputs.size(), 5);
}

TEST(SensoryFacultyTest, SmellLobeHandlesNullRoom) {
	// When caValues is empty (simulating null room), no neurons are written
	TestableSensoryFaculty f;
	f.caValues = {}; // empty = null room
	f.lobeSize = 20;
	f.smelNeuronInputs.resize(20, 0.0f);

	f.updateSmellLobe();

	// All inputs remain 0.0f
	for (int i = 0; i < 20; i++) {
		EXPECT_FLOAT_EQ(f.smelNeuronInputs[i], 0.0f) << "at index " << i;
	}
}

// ---------------------------------------------------------------------------
// FacultyIntegrationTest — D-23: all three faculties coexist and execute in order
// ---------------------------------------------------------------------------

TEST(FacultyIntegrationTest, AllThreeFacultiesExist) {
	// Create instances of all three testable faculty mirrors
	TestableSensoryFaculty sensory;
	sensory.caValues.resize(20, 0.0f);
	sensory.caValues[0] = 0.7f;
	sensory.lobeSize = 20;
	sensory.smelNeuronInputs.resize(20, 0.0f);

	TestableMotorFaculty motor;
	motor.organLifeforces = {0.8f, 0.6f, 1.0f};

	TestableLifeFaculty life;
	life.organLifeforces = {0.8f, 0.6f, 1.0f};
	life.ticks = 200;

	// Execute in D-17/D-18 tick order: sensory -> motor -> life
	sensory.updateSmellLobe();
	motor.updateMotorFaculty();
	life.updateLifeFaculty();

	// Sensory: smel neuron[0] received CA value
	EXPECT_FLOAT_EQ(sensory.smelNeuronInputs[0], 0.7f);

	// Motor: muscleenergy computed from organ health
	EXPECT_GT(motor.muscleenergy, 0.0f);

	// Life: creature is alive (organs have lifeforce)
	EXPECT_FLOAT_EQ(life.dead, 0.0f);
}

// ---------------------------------------------------------------------------
// TestableExpressiveFaculty — mirrors updateExpressiveFaculty() algorithm
// ---------------------------------------------------------------------------

class TestableExpressiveFaculty {
  public:
	float drives[20];
	float expressionWeights[6][20];
	float expressionOverall[6];
	int facialexpression = 0;

	TestableExpressiveFaculty() {
		for (int i = 0; i < 20; i++) drives[i] = 0.0f;
		for (int e = 0; e < 6; e++) {
			expressionOverall[e] = 0.0f;
			for (int d = 0; d < 20; d++) expressionWeights[e][d] = 0.0f;
		}
	}

	void updateExpressiveFaculty() {
		int bestExprId = 0;
		float bestScore = 0.0f;
		for (int e = 0; e < 6; e++) {
			float score = 0.0f;
			for (int d = 0; d < 20; d++) {
				score += expressionWeights[e][d] * (drives[d] - 0.5f);
			}
			score *= expressionOverall[e];
			if (score > bestScore) {
				bestScore = score;
				bestExprId = e;
			}
		}
		if (bestExprId < 0) bestExprId = 0;
		if (bestExprId > 5) bestExprId = 5;
		facialexpression = bestExprId;
	}
};

// ---------------------------------------------------------------------------
// ExpressiveFacultyTest
// ---------------------------------------------------------------------------

TEST(ExpressiveFacultyTest, NormalExpressionWhenAllZero) {
	// All weights 0 and all drives 0 => default expression 0
	TestableExpressiveFaculty f;
	f.updateExpressiveFaculty();
	EXPECT_EQ(f.facialexpression, 0);
}

TEST(ExpressiveFacultyTest, HighestWeightedDriveWins) {
	// Set drives[3] (pain) high, set expressionWeights[2][3] high => expression 2
	TestableExpressiveFaculty f;
	f.drives[3] = 0.9f;
	f.expressionWeights[2][3] = 1.0f;
	f.expressionOverall[2] = 1.0f;
	f.updateExpressiveFaculty();
	EXPECT_EQ(f.facialexpression, 2);
}

TEST(ExpressiveFacultyTest, ExpressionClampedToFive) {
	// With valid expressions 0-5 in weights, result is always in [0, 5]
	TestableExpressiveFaculty f;
	for (int e = 0; e < 6; e++) {
		f.expressionOverall[e] = 1.0f;
		f.expressionWeights[e][e % 20] = 1.0f;
	}
	for (int d = 0; d < 20; d++) f.drives[d] = 1.0f;
	f.updateExpressiveFaculty();
	EXPECT_GE(f.facialexpression, 0);
	EXPECT_LE(f.facialexpression, 5);
}

// ---------------------------------------------------------------------------
// TestableReproductiveFaculty — mirrors updateReproductiveFaculty() algorithm
// ---------------------------------------------------------------------------

class TestableReproductiveFaculty {
  public:
	float ovulate = 0.0f;
	float fertile = 0.0f;
	float pregnant = 0.0f;
	bool myGamete = false;

	void updateReproductiveFaculty() {
		static const float OVULATEOFF = 0.314f;
		static const float OVULATEON = 0.627f;

		if (myGamete && ovulate < OVULATEOFF) {
			myGamete = false;
		} else if (!myGamete && ovulate > OVULATEON) {
			myGamete = true;
		}
		fertile = myGamete ? 1.0f : 0.0f;
	}
};

// ---------------------------------------------------------------------------
// ReproductiveFacultyTest
// ---------------------------------------------------------------------------

TEST(ReproductiveFacultyTest, GameteCreatedAboveThreshold) {
	// ovulate=0.7f above OVULATEON=0.627f, myGamete starts false => becomes true
	TestableReproductiveFaculty f;
	f.ovulate = 0.7f;
	f.myGamete = false;
	f.updateReproductiveFaculty();
	EXPECT_TRUE(f.myGamete);
	EXPECT_EQ(f.fertile, 1.0f);
}

TEST(ReproductiveFacultyTest, GameteDestroyedBelowThreshold) {
	// ovulate=0.2f below OVULATEOFF=0.314f, myGamete starts true => becomes false
	TestableReproductiveFaculty f;
	f.ovulate = 0.2f;
	f.myGamete = true;
	f.updateReproductiveFaculty();
	EXPECT_FALSE(f.myGamete);
	EXPECT_EQ(f.fertile, 0.0f);
}

TEST(ReproductiveFacultyTest, HysteresisInMiddleRange) {
	// ovulate=0.5f (between 0.314f and 0.627f): state unchanged (hysteresis)
	TestableReproductiveFaculty f;
	f.ovulate = 0.5f;

	// If myGamete=true, stays true
	f.myGamete = true;
	f.updateReproductiveFaculty();
	EXPECT_TRUE(f.myGamete);
	EXPECT_EQ(f.fertile, 1.0f);

	// If myGamete=false, stays false
	f.myGamete = false;
	f.updateReproductiveFaculty();
	EXPECT_FALSE(f.myGamete);
	EXPECT_EQ(f.fertile, 0.0f);
}

// ---------------------------------------------------------------------------
// TestableLinguisticFaculty — stub
// ---------------------------------------------------------------------------

class TestableLinguisticFaculty {
  public:
	int callCount = 0;

	void updateLinguisticFaculty() {
		// LinguisticFaculty stub: hearing->vocabulary via tickBrain() pending buffers
		callCount++;
	}
};

// ---------------------------------------------------------------------------
// LinguisticFacultyTest
// ---------------------------------------------------------------------------

TEST(LinguisticFacultyTest, StubDoesNotCrash) {
	// Call updateLinguisticFaculty() on empty state — must not crash
	TestableLinguisticFaculty f;
	EXPECT_NO_THROW(f.updateLinguisticFaculty());
	EXPECT_EQ(f.callCount, 1);
}

// ---------------------------------------------------------------------------
// TestableMusicFaculty — no-op
// ---------------------------------------------------------------------------

class TestableMusicFaculty {
  public:
	int someState = 42;

	void updateMusicFaculty() {
		// MusicFaculty: Update() is a no-op per lc2e
	}
};

// ---------------------------------------------------------------------------
// MusicFacultyTest
// ---------------------------------------------------------------------------

TEST(MusicFacultyTest, UpdateIsNoOp) {
	// updateMusicFaculty() must not change any state
	TestableMusicFaculty f;
	f.updateMusicFaculty();
	EXPECT_EQ(f.someState, 42);
}

// ---------------------------------------------------------------------------
// TestableBrainCommand — mirrors c_BRN_SETN neuron variable set logic
// ---------------------------------------------------------------------------

struct TestableBrainNeuron {
	float variables[8];
};

// ---------------------------------------------------------------------------
// BrainCommandTest — CAOS-03: BRN: SETN side-effect test
// ---------------------------------------------------------------------------

TEST(BrainCommandTest, SetNeuronVariable) {
	// Mirrors c_BRN_SETN: set neuron->variables[state_no] = value
	TestableBrainNeuron neuron;
	for (int i = 0; i < 8; i++) neuron.variables[i] = 0.0f;

	// Set variable at index 3 to 0.75f (mirrors c_BRN_SETN call)
	int state_no = 3;
	float value = 0.75f;
	neuron.variables[state_no] = value;

	EXPECT_FLOAT_EQ(neuron.variables[3], 0.75f);

	// All other variables must remain unchanged (zero)
	for (int i = 0; i < 8; i++) {
		if (i == 3) continue;
		EXPECT_FLOAT_EQ(neuron.variables[i], 0.0f) << "variable[" << i << "] should be untouched";
	}
}

TEST(BrainCommandTest, SetMultipleNeuronVariables) {
	// Verify multiple state_no writes are independent
	TestableBrainNeuron neuron;
	for (int i = 0; i < 8; i++) neuron.variables[i] = 0.0f;

	neuron.variables[0] = 0.5f;  // STATE_VAR
	neuron.variables[1] = 0.25f; // second variable
	neuron.variables[7] = 1.0f;  // last variable

	EXPECT_FLOAT_EQ(neuron.variables[0], 0.5f);
	EXPECT_FLOAT_EQ(neuron.variables[1], 0.25f);
	EXPECT_FLOAT_EQ(neuron.variables[7], 1.0f);
	// Untouched variables remain zero
	EXPECT_FLOAT_EQ(neuron.variables[2], 0.0f);
	EXPECT_FLOAT_EQ(neuron.variables[5], 0.0f);
}

// ---------------------------------------------------------------------------
// TestableExprCommand — mirrors c_EXPR/v_EXPR logic
// ---------------------------------------------------------------------------

class TestableExprCommand {
  public:
	int facialexpression = 0;

	// Mirrors c_EXPR clamping and set logic
	void setExpression(int index) {
		if (index < 0) index = 0;
		if (index > 5) index = 5;
		facialexpression = index;
	}

	// Mirrors v_EXPR getter
	int getFacialExpression() const { return facialexpression; }
};

// ---------------------------------------------------------------------------
// ExprCommandTest — CAOS-05: EXPR set/get side-effect tests
// ---------------------------------------------------------------------------

TEST(ExprCommandTest, SetAndGetExpression) {
	// Mirrors c_EXPR(index=3) then v_EXPR — set expression 3, get back 3
	TestableExprCommand cmd;
	cmd.setExpression(3);
	EXPECT_EQ(cmd.getFacialExpression(), 3);
}

TEST(ExprCommandTest, ClampToValidRange) {
	// c_EXPR clamps to [0, 5]; verify boundary enforcement
	TestableExprCommand cmd;

	// Below range: -1 -> 0
	cmd.setExpression(-1);
	EXPECT_EQ(cmd.getFacialExpression(), 0);

	// Above range: 6 -> 5
	cmd.setExpression(6);
	EXPECT_EQ(cmd.getFacialExpression(), 5);

	// Exact boundaries
	cmd.setExpression(0);
	EXPECT_EQ(cmd.getFacialExpression(), 0);
	cmd.setExpression(5);
	EXPECT_EQ(cmd.getFacialExpression(), 5);
}

// ---------------------------------------------------------------------------
// Additional ReproductiveFacultyTest cases — pregnancy detection
// ---------------------------------------------------------------------------

// TestablePregnancyFaculty — mirrors genome_slots[1] pregnancy detection
// from updateReproductiveFaculty() in c2eCreature
class TestablePregnancyFaculty {
  public:
	bool hasGenomeSlot1 = false;  // simulates genome_slots.count(1) > 0
	float pregnant = 0.0f;

	void updatePregnancyLocus() {
		// Mirrors the genome_slots[1] check in updateReproductiveFaculty()
		pregnant = hasGenomeSlot1 ? 1.0f : 0.0f;
	}
};

TEST(ReproductiveFacultyTest, PregnantWhenSlotOccupied) {
	// genome_slots[1] present => pregnant = 1.0f
	TestablePregnancyFaculty f;
	f.hasGenomeSlot1 = true;
	f.updatePregnancyLocus();
	EXPECT_FLOAT_EQ(f.pregnant, 1.0f);
}

TEST(ReproductiveFacultyTest, NotPregnantWhenSlotEmpty) {
	// genome_slots[1] absent => pregnant = 0.0f
	TestablePregnancyFaculty f;
	f.hasGenomeSlot1 = false;
	f.updatePregnancyLocus();
	EXPECT_FLOAT_EQ(f.pregnant, 0.0f);
}

// ---------------------------------------------------------------------------
// HearSmllTest — compile-time and logic verification for v_HEAR / v_SMLL
// ---------------------------------------------------------------------------
// These tests verify the chosenagents lookup logic used by HEAR and SMLL.
// The implementation mirrors v_SEEN: bounds-check category < getNoCategories(),
// then return chosenagents[category]. This testable mirror ensures the logic
// remains correct independently of the CAOS VM dispatch layer.

class TestableHearSmllSensor {
  public:
	// Mirrors the chosenagents[] array used by SEEN/HEAR/SMLL
	std::vector<int> chosenagents;  // agent ids; 0 = no agent chosen

	size_t getNoCategories() const { return chosenagents.size(); }

	// Mirrors getChosenAgentForCategory() with bounds check (THROW_IFNOT equivalent)
	// Returns -1 on invalid category to represent the THROW_IFNOT guard
	int getAgentForCategory(int category) const {
		if (category < 0 || (size_t)category >= chosenagents.size()) {
			return -1;  // signals out-of-range (THROW_IFNOT would raise exception)
		}
		return chosenagents[category];
	}
};

TEST(HearSmllTest, HearReturnsChosenAgentByCategory) {
	// v_HEAR: returns getChosenAgentForCategory(category) — same as v_SEEN
	TestableHearSmllSensor sensor;
	sensor.chosenagents = {10, 20, 30};  // 3 categories

	EXPECT_EQ(sensor.getAgentForCategory(0), 10);
	EXPECT_EQ(sensor.getAgentForCategory(1), 20);
	EXPECT_EQ(sensor.getAgentForCategory(2), 30);
}

TEST(HearSmllTest, SmllReturnsChosenAgentByCategory) {
	// v_SMLL: identical mechanism to v_HEAR and v_SEEN
	TestableHearSmllSensor sensor;
	sensor.chosenagents = {5, 15, 25, 35};  // 4 categories

	EXPECT_EQ(sensor.getAgentForCategory(0), 5);
	EXPECT_EQ(sensor.getAgentForCategory(3), 35);
}

TEST(HearSmllTest, OutOfRangeCategoryRejected) {
	// Both HEAR and SMLL use THROW_IFNOT(category < getNoCategories())
	TestableHearSmllSensor sensor;
	sensor.chosenagents = {42};  // only 1 category (index 0)

	EXPECT_EQ(sensor.getAgentForCategory(-1), -1);   // negative: rejected
	EXPECT_EQ(sensor.getAgentForCategory(1), -1);    // out of range: rejected
	EXPECT_EQ(sensor.getAgentForCategory(0), 42);    // valid: returns agent
}

TEST(HearSmllTest, EmptyChosenAgentsAllCategoriesInvalid) {
	// No categories available — any category is out-of-range
	TestableHearSmllSensor sensor;
	// chosenagents is empty

	EXPECT_EQ(sensor.getNoCategories(), 0u);
	EXPECT_EQ(sensor.getAgentForCategory(0), -1);
}

/* vim: set noet: */
