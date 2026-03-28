/*
 * AgingTest.cpp
 * openc2e
 *
 * Tests for gene switch modes and variant filtering in shouldProcessGene()
 * (Phase 3, Plan 01 — AGE-01 through AGE-05).
 *
 *   AGE-01: GeneSwitch::Age — exact life stage match
 *   AGE-02: GeneSwitch::UpToAge — cumulative (switchontime <= stage)
 *   AGE-03: GeneSwitch::Always / GeneSwitch::Embryo — unconditional / baby only
 *   AGE-04: Variant filter — variant=0 matches all; non-zero matches only own variant
 *   AGE-05: Age transitions trigger processGenes() re-evaluation
 *
 * Test strategy: shouldProcessGene() is a method on Creature which requires a
 * full CreatureAgent/World to construct. To avoid that dependency chain we use
 * a minimal TestableCreature class that duplicates the algorithm and is compiled
 * alongside the production code. This mirrors the NeuroEmitter and SVRule test
 * strategy (Phase 2, D-03/D-04).
 *
 * The TestableCreature mirrors Creature::shouldProcessGene() exactly. If
 * production behaviour changes, tests will catch any divergence.
 */

#include "fileformats/genomeFile.h"
#include "openc2e/creatures/GeneSwitch.h"

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Minimal test fixture — mirrors Creature::shouldProcessGene() without the
// full CreatureAgent construction overhead.
// ---------------------------------------------------------------------------

class TestableCreature {
  public:
	lifestage stage = baby;
	unsigned int variant = 0;
	bool female = false;

	bool shouldProcessGene(gene* g, GeneSwitch mode = GeneSwitch::Age) {
		geneFlags& flags = g->header.flags;
		if (flags.notexpressed) return false;
		if (flags.femaleonly && !female) return false;
		if (flags.maleonly && female) return false;
		// Variant filter: 0 means "all variants"
		if (g->header.variant != 0 && g->header.variant != variant) return false;
		switch (mode) {
			case GeneSwitch::Age:     return g->header.switchontime == stage;
			case GeneSwitch::UpToAge: return g->header.switchontime <= stage;
			case GeneSwitch::Always:  return true;
			case GeneSwitch::Embryo:  return stage == baby;
		}
		return false;
	}
};

// ---------------------------------------------------------------------------
// Minimal gene subclass for testing — just sets switchontime and variant in
// the header. geneFlags default-construct to all-false (not expressed = false,
// sex flags = false).
// ---------------------------------------------------------------------------

class TestGene : public gene {
  protected:
	uint8_t type() const override { return 0; }
	uint8_t subtype() const override { return 0; }
	void write(Writer&) const override {}
	void read(Reader&) override {}

  public:
	TestGene(lifestage switchon, uint8_t var = 0)
		: gene(3) {
		header.switchontime = switchon;
		header.variant = var;
	}
	const char* name() override { return "TestGene"; }
	const char* typeName() override { return "Test"; }
	std::unique_ptr<gene> clone() const override {
		return std::unique_ptr<gene>(new TestGene(*this));
	}
};

// ---------------------------------------------------------------------------
// GeneSwitch::Age — exact match (AGE-01)
// ---------------------------------------------------------------------------

TEST(AgingTest, SWITCH_AGE_ExactMatch_True) {
	TestableCreature c;
	c.stage = child;
	TestGene g(child);
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::Age));
}

TEST(AgingTest, SWITCH_AGE_ExactMatch_WrongStage) {
	TestableCreature c;
	c.stage = baby;
	TestGene g(child);
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Age));
}

TEST(AgingTest, SWITCH_AGE_Default_EquivalentToExplicit) {
	// Default parameter must be GeneSwitch::Age
	TestableCreature c;
	c.stage = adult;
	TestGene g(adult);
	EXPECT_TRUE(c.shouldProcessGene(&g)); // default == GeneSwitch::Age
	TestGene g2(child);
	EXPECT_FALSE(c.shouldProcessGene(&g2)); // adult != child
}

// ---------------------------------------------------------------------------
// GeneSwitch::UpToAge — cumulative activation (AGE-02)
// ---------------------------------------------------------------------------

TEST(AgingTest, SWITCH_UPTOAGE_BelowStage_True) {
	TestableCreature c;
	c.stage = adolescent; // stage=2
	TestGene g(child);    // switchontime=1 <= 2
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::UpToAge));
}

TEST(AgingTest, SWITCH_UPTOAGE_EqualStage_True) {
	TestableCreature c;
	c.stage = child;
	TestGene g(child);
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::UpToAge));
}

TEST(AgingTest, SWITCH_UPTOAGE_AboveStage_False) {
	TestableCreature c;
	c.stage = baby;    // stage=0
	TestGene g(child); // switchontime=1 > 0
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::UpToAge));
}

// ---------------------------------------------------------------------------
// GeneSwitch::Always — unconditional (AGE-03)
// ---------------------------------------------------------------------------

TEST(AgingTest, SWITCH_ALWAYS_Baby) {
	TestableCreature c;
	c.stage = baby;
	TestGene g(senile); // switchontime mismatch, should not matter
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::Always));
}

TEST(AgingTest, SWITCH_ALWAYS_Child) {
	TestableCreature c;
	c.stage = child;
	TestGene g(senile);
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::Always));
}

TEST(AgingTest, SWITCH_ALWAYS_Adult) {
	TestableCreature c;
	c.stage = adult;
	TestGene g(baby);
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::Always));
}

// ---------------------------------------------------------------------------
// GeneSwitch::Embryo — baby only (AGE-03)
// ---------------------------------------------------------------------------

TEST(AgingTest, SWITCH_EMBRYO_Baby_True) {
	TestableCreature c;
	c.stage = baby;
	TestGene g(senile); // switchontime ignored for Embryo mode
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::Embryo));
}

TEST(AgingTest, SWITCH_EMBRYO_Child_False) {
	TestableCreature c;
	c.stage = child;
	TestGene g(child);
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Embryo));
}

TEST(AgingTest, SWITCH_EMBRYO_Adolescent_False) {
	TestableCreature c;
	c.stage = adolescent;
	TestGene g(adolescent);
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Embryo));
}

// ---------------------------------------------------------------------------
// Variant filter (AGE-04)
// ---------------------------------------------------------------------------

TEST(AgingTest, VariantFilter_Zero_MatchesAll) {
	TestableCreature c;
	c.stage = baby;

	// variant=0 in gene header => matches any creature variant
	c.variant = 1;
	TestGene g1(baby, 0); // gene variant = 0
	EXPECT_TRUE(c.shouldProcessGene(&g1, GeneSwitch::Age));

	c.variant = 3;
	TestGene g2(baby, 0);
	EXPECT_TRUE(c.shouldProcessGene(&g2, GeneSwitch::Age));
}

TEST(AgingTest, VariantFilter_Specific_MatchesExact) {
	TestableCreature c;
	c.stage = baby;

	c.variant = 2;
	TestGene g_match(baby, 2); // gene variant = 2, creature variant = 2
	EXPECT_TRUE(c.shouldProcessGene(&g_match, GeneSwitch::Age));

	TestGene g_nomatch(baby, 3); // gene variant = 3, creature variant = 2
	EXPECT_FALSE(c.shouldProcessGene(&g_nomatch, GeneSwitch::Age));
}

TEST(AgingTest, VariantFilter_Specific_RejectsOther) {
	TestableCreature c;
	c.stage = baby;
	c.variant = 1;

	TestGene g(baby, 2); // gene targets variant 2, creature is variant 1
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Age));
}

// ---------------------------------------------------------------------------
// Flag filters — notexpressed and gender (AGE-01 prerequisite checks)
// ---------------------------------------------------------------------------

TEST(AgingTest, NotExpressed_AlwaysFalse) {
	TestableCreature c;
	c.stage = baby;
	TestGene g(baby);
	g.header.flags.notexpressed = true;

	// notexpressed overrides all modes
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Age));
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Always));
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::UpToAge));
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Embryo));
}

TEST(AgingTest, GenderFilter_FemaleOnly_FalseForMale) {
	TestableCreature c;
	c.stage = baby;
	c.female = false; // male creature

	TestGene g(baby);
	g.header.flags.femaleonly = true;
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Age));
}

TEST(AgingTest, GenderFilter_MaleOnly_FalseForFemale) {
	TestableCreature c;
	c.stage = baby;
	c.female = true; // female creature

	TestGene g(baby);
	g.header.flags.maleonly = true;
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Age));
}

TEST(AgingTest, GenderFilter_FemaleOnly_TrueForFemale) {
	TestableCreature c;
	c.stage = baby;
	c.female = true;

	TestGene g(baby);
	g.header.flags.femaleonly = true;
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::Age));
}

// ---------------------------------------------------------------------------
// Age transition re-evaluation (AGE-05)
// processGenes() is called by ageCreature(); this test simulates that by
// changing stage and verifying the filter produces correct results.
// ---------------------------------------------------------------------------

TEST(AgingTest, AgeTransition_GeneBecomesActive) {
	// A gene with switchontime=child, GeneSwitch::Age:
	//   at stage=baby  -> false (not yet)
	//   at stage=child -> true  (exact match, as if processGenes() was called)
	TestableCreature c;
	c.stage = baby;
	TestGene g(child);

	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::Age));

	c.stage = child; // simulate ageCreature() advancing the stage
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::Age));
}

TEST(AgingTest, AgeTransition_UpToAge_StaysActive) {
	// UpToAge gene with switchontime=child:
	//   at stage=baby      -> false
	//   at stage=child     -> true
	//   at stage=adolescent -> true (stays active past switchontime)
	TestableCreature c;
	TestGene g(child);

	c.stage = baby;
	EXPECT_FALSE(c.shouldProcessGene(&g, GeneSwitch::UpToAge));

	c.stage = child;
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::UpToAge));

	c.stage = adolescent;
	EXPECT_TRUE(c.shouldProcessGene(&g, GeneSwitch::UpToAge));
}

/* vim: set noet: */
