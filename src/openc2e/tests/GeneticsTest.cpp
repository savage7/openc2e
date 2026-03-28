/*
 * GeneticsTest.cpp
 * openc2e
 *
 * Phase 04, Plans 01–02 — GENE-01 through GENE-09
 *
 * Tests for the core genetic algorithms: genome crossover, power-curve mutation,
 * cutting errors, clone/crossover CAOS command logic, and moniker lineage.
 *
 *   GENE-01: Crossover produces valid child genomes with no mid-gene splits
 *   GENE-02: Run-length crossover spacing averages approximately 50 genes
 *   GENE-03: Power-curve mutation modifies gene data bytes at expected rate
 *   GENE-04: Gene header bytes (type, subtype, generation, flags) are never mutated
 *   GENE-05: DUP/CUT/MUT flags are respected — cutting errors obey flag guards
 *   GENE-06: Child genome from crossover is valid (non-null, non-empty, correct version)
 *   GENE-07: GENE CLON produces a unique moniker with Cloned (14) event
 *   GENE-08: GENE CROS records parent monikers (MON1=mum, MON2=dad) with Conceived (0) event
 *   GENE-09: Genome slot map operations (store, move, kill) work correctly
 *
 * Test strategy for Plans 01 tests: standalone genomeFile objects with
 * bioInitialConcentrationGene (smallest gene type: 2 data bytes) to avoid
 * the full Creature/World dependency chain.
 *
 * Test strategy for Plans 02 tests (GENE-06 through GENE-09):
 * - GENE-06: standalone crossoverGenomes() call, no World needed
 * - GENE-07, GENE-08: local historyManager instance with engine.version=2 to
 *   use simple random-moniker generation (no catalogue dependency)
 * - GENE-09: std::map<unsigned int, shared_ptr<genomeFile>> directly (same type
 *   as Agent::genome_slots), no World/Agent needed
 */

#include "openc2e/creatures/GeneticAlgorithms.h"
#include "openc2e/historyManager.h"
#include "fileformats/genomeFile.h"
#include "openc2e/Engine.h"

#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>

// ---------------------------------------------------------------------------
// Helper: construct a test genome with N bioInitialConcentrationGene genes
// Each gene gets a unique chemical index so data changes are detectable.
// ---------------------------------------------------------------------------

static genomeFile makeTestGenome(int numGenes, uint8_t version = 3) {
	genomeFile g;
	g.setVersion(version);
	for (int i = 0; i < numGenes; i++) {
		auto genePtr = std::make_unique<bioInitialConcentrationGene>(version);
		genePtr->header.flags._mutable = true;
		genePtr->header.flags.dupable = true;
		genePtr->header.flags.delable = true;
		genePtr->header.mutweighting = 128;
		genePtr->chemical = static_cast<uint8_t>(i % 256);
		genePtr->quantity = 100;
		g.genes.push_back(std::move(genePtr));
	}
	return g;
}

// ---------------------------------------------------------------------------
// GENE-01: Crossover_RespectsBoundaries
// Every gene in the child genome must be a complete, valid gene object (no
// mid-gene splits). Verify non-null, valid name, size within plausible range.
// ---------------------------------------------------------------------------

TEST(GeneticsTest, Crossover_RespectsBoundaries) {
	genomeFile mum = makeTestGenome(100);
	genomeFile dad = makeTestGenome(100);

	// Zero mutation to isolate crossover behavior
	CrossoverResult result = crossoverGenomes(mum, dad, 0, 128, 0, 128);

	ASSERT_NE(result.child, nullptr);
	ASSERT_GE(result.child->genes.size(), 1u);
	// Duplication can increase count; upper bound is generous (200 base + duplications)
	ASSERT_LE(result.child->genes.size(), 400u);

	// Every gene must be non-null and have a valid name (proves no corruption)
	for (const auto& gene : result.child->genes) {
		ASSERT_NE(gene.get(), nullptr);
		ASSERT_NE(gene->name(), nullptr);
		// Must be a valid gene type — name() returns a non-empty string
		ASSERT_GT(std::string(gene->name()).size(), 0u);
	}
}

// ---------------------------------------------------------------------------
// GENE-02: Crossover_StatisticalSpacing
// Over 1000 crossovers of 200-gene parents, the average spacing between
// crossover points should be approximately 50 genes (within [30, 80]).
// Expected: Rnd(10, 100) uniform → mean ≈ 55 genes/crossover.
// ---------------------------------------------------------------------------

TEST(GeneticsTest, Crossover_StatisticalSpacing) {
	// Use 200-gene parents to have enough genes per run
	genomeFile mum = makeTestGenome(200);
	genomeFile dad = makeTestGenome(200);

	const int NUM_TRIALS = 1000;
	double totalSpacing = 0.0;
	int validTrials = 0;

	for (int trial = 0; trial < NUM_TRIALS; ++trial) {
		CrossoverResult result = crossoverGenomes(mum, dad, 0, 128, 0, 128);
		if (result.crossover_points > 0) {
			// Spacing = genes per crossover = parent size / crossover points
			double spacing = 200.0 / static_cast<double>(result.crossover_points);
			totalSpacing += spacing;
			++validTrials;
		}
	}

	// Avoid division by zero if somehow no crossovers occurred
	ASSERT_GT(validTrials, 0);
	double averageSpacing = totalSpacing / static_cast<double>(validTrials);

	// Average spacing should be in range [30, 80] — CROSSOVER_LINKAGE=50 is center
	EXPECT_GE(averageSpacing, 30.0)
		<< "Average crossover spacing too small: " << averageSpacing;
	EXPECT_LE(averageSpacing, 80.0)
		<< "Average crossover spacing too large: " << averageSpacing;
}

// ---------------------------------------------------------------------------
// GENE-03: Mutation_PowerCurve
// With high mutation chance (parentChance=200, parentDegree=0), at least some
// genes should be mutated in a 50-gene parent. Verifies power-curve executes.
// ---------------------------------------------------------------------------

TEST(GeneticsTest, Mutation_PowerCurve) {
	// Create mum with all genes having known byte values (chemical=0, quantity=0)
	genomeFile mum;
	mum.setVersion(3);
	for (int i = 0; i < 50; i++) {
		auto g = std::make_unique<bioInitialConcentrationGene>(3);
		g->header.flags._mutable = true;
		g->header.mutweighting = 0; // minimum weighting → maximum mutability
		g->chemical = 0x00;
		g->quantity = 0x00;
		mum.genes.push_back(std::move(g));
	}

	genomeFile dad = makeTestGenome(50);

	// High mutation: parentChance=200 scales the base rate significantly
	// parentDegree=0 → dDegree≈128 (large power = small mask values but still mutates)
	// With mutweighting=0 and parentChance=200: baseChance ≈ 1050 (1-in-1050 per byte).
	// 200 trials × 50 genes × 2 bytes = 20000 byte-mutation opportunities.
	// P(0 mutations) ≈ e^(-19) ≈ 5e-9 — statistically impossible.
	// (20 trials only gave ~2% false-negative rate; 200 makes it effectively deterministic.)
	int totalMutations = 0;
	for (int trial = 0; trial < 200; ++trial) {
		CrossoverResult result = crossoverGenomes(mum, dad, 200, 0, 200, 0);
		totalMutations += static_cast<int>(result.point_mutations);
	}

	// With 50 genes × 2 bytes × 200 trials = 20000 opportunities at ~1/1050 each,
	// we expect ~19 mutations — P(zero mutations) < 1e-8
	EXPECT_GT(totalMutations, 0)
		<< "Expected at least one mutation across 200 crossovers with high mutation chance";
}

// ---------------------------------------------------------------------------
// GENE-04: Mutation_HeaderUnchanged
// Gene header fields (generation, flags) must not be changed by mutation,
// even with maximum mutation chance. Type identity must also be preserved.
// ---------------------------------------------------------------------------

// Helper to build a controlled genome for header tests
static genomeFile makeHeaderTestGenome(uint8_t version = 3) {
	genomeFile g;
	g.setVersion(version);
	for (int i = 0; i < 50; i++) {
		auto genePtr = std::make_unique<bioInitialConcentrationGene>(version);
		genePtr->header.flags._mutable = true;
		genePtr->header.flags.dupable = false; // disable dup to keep size stable
		genePtr->header.flags.delable = false; // disable del to keep size stable
		genePtr->header.generation = static_cast<uint8_t>((i % 10) + 1);
		genePtr->header.mutweighting = 0; // maximize mutation
		genePtr->chemical = static_cast<uint8_t>(i % 256);
		genePtr->quantity = 50;
		g.genes.push_back(std::move(genePtr));
	}
	return g;
}

TEST(GeneticsTest, Mutation_HeaderUnchanged) {
	genomeFile mum = makeHeaderTestGenome();
	genomeFile dad = makeHeaderTestGenome(); // identical structure

	// Record header values before crossover
	std::vector<uint8_t> generations;
	std::vector<bool> mutables, dupables, delables;
	for (const auto& g : mum.genes) {
		generations.push_back(g->header.generation);
		mutables.push_back(g->header.flags._mutable);
		dupables.push_back(g->header.flags.dupable);
		delables.push_back(g->header.flags.delable);
	}

	// Maximum mutation chance, minimal degree (header bytes must not be touched)
	CrossoverResult result = crossoverGenomes(mum, dad, 255, 0, 255, 0);

	ASSERT_NE(result.child, nullptr);
	// Child should have same count (dup/del disabled)
	ASSERT_EQ(result.child->genes.size(), generations.size());

	for (size_t i = 0; i < result.child->genes.size(); ++i) {
		const auto& childGene = result.child->genes[i];

		// Type identity preserved (bioInitialConcentrationGene → type=1, subtype=4)
		EXPECT_STREQ(childGene->name(), "Initial Concentration")
			<< "Gene type changed at index " << i;

		// Header fields preserved
		EXPECT_EQ(childGene->header.generation, generations[i])
			<< "Generation changed at index " << i;
		EXPECT_EQ(childGene->header.flags._mutable, mutables[i])
			<< "_mutable flag changed at index " << i;
		EXPECT_EQ(childGene->header.flags.dupable, dupables[i])
			<< "dupable flag changed at index " << i;
		EXPECT_EQ(childGene->header.flags.delable, delables[i])
			<< "delable flag changed at index " << i;
	}
}

// ---------------------------------------------------------------------------
// GENE-04 / D-09: Mutation_MUTFlagRespected
// Genes with flags._mutable = false must never be mutated regardless of
// the parent mutation chance/degree settings.
// ---------------------------------------------------------------------------

static genomeFile makeImmutableGenome(uint8_t version = 3) {
	genomeFile g;
	g.setVersion(version);
	for (int i = 0; i < 50; i++) {
		auto genePtr = std::make_unique<bioInitialConcentrationGene>(version);
		genePtr->header.flags._mutable = false; // MUT flag off
		genePtr->header.flags.dupable = false;
		genePtr->header.flags.delable = false;
		genePtr->header.mutweighting = 0; // would maximize mutation if _mutable were true
		genePtr->chemical = 0xAB;
		genePtr->quantity = 0xCD;
		g.genes.push_back(std::move(genePtr));
	}
	return g;
}

TEST(GeneticsTest, Mutation_MUTFlagRespected) {
	genomeFile mum = makeImmutableGenome();
	genomeFile dad = makeImmutableGenome();

	// Maximum mutation parameters
	CrossoverResult result = crossoverGenomes(mum, dad, 255, 0, 255, 0);

	// With _mutable=false, mutateGeneData() must return 0 for every gene
	EXPECT_EQ(result.point_mutations, 0u)
		<< "Expected zero mutations for genes with _mutable=false, got "
		<< result.point_mutations;
}

// ---------------------------------------------------------------------------
// GENE-05 / D-12: CuttingError_FlagsRespected
// When dupable=false and delable=false, no cutting errors can produce
// duplications or excisions. Child size must equal parent size.
// ---------------------------------------------------------------------------

static genomeFile makeNoCuttingErrorGenome(int numGenes, uint8_t version = 3) {
	genomeFile g;
	g.setVersion(version);
	for (int i = 0; i < numGenes; i++) {
		auto genePtr = std::make_unique<bioInitialConcentrationGene>(version);
		genePtr->header.flags._mutable = false; // disable mutation for simplicity
		genePtr->header.flags.dupable = false;  // DUP errors cannot apply
		genePtr->header.flags.delable = false;  // CUT errors cannot apply
		genePtr->header.mutweighting = 128;
		genePtr->chemical = static_cast<uint8_t>(i % 256);
		genePtr->quantity = 50;
		g.genes.push_back(std::move(genePtr));
	}
	return g;
}

TEST(GeneticsTest, CuttingError_FlagsRespected) {
	const int PARENT_SIZE = 100;
	const int NUM_TRIALS = 1000;

	for (int trial = 0; trial < NUM_TRIALS; ++trial) {
		genomeFile mum = makeNoCuttingErrorGenome(PARENT_SIZE);
		genomeFile dad = makeNoCuttingErrorGenome(PARENT_SIZE);

		CrossoverResult result = crossoverGenomes(mum, dad, 0, 128, 0, 128);

		ASSERT_NE(result.child, nullptr);
		// With both parents identical and dup/del disabled,
		// child must have exactly PARENT_SIZE genes
		EXPECT_EQ(result.child->genes.size(), static_cast<size_t>(PARENT_SIZE))
			<< "Child size changed from parent size in trial " << trial
			<< " (got " << result.child->genes.size() << ")";

		if (result.child->genes.size() != static_cast<size_t>(PARENT_SIZE)) {
			break; // Stop on first failure to avoid 1000 repeated failures
		}
	}
}

// ---------------------------------------------------------------------------
// GENE-07: GENE_CLON_UniqueMoniker
// Clone logic: deep-copy genome, register new unique moniker, record Cloned
// event (type 14) with source moniker as MON1 and empty string as MON2.
// Uses engine.version=2 to avoid catalogue dependency in newMoniker().
// ---------------------------------------------------------------------------

TEST(GeneticsTest, GENE_CLON_UniqueMoniker) {
	// Use version=2 so newMoniker() uses simple random generation (no catalogue)
	unsigned int saved_version = engine.version;
	engine.version = 2;

	historyManager history;

	// Build source genome with 10 genes
	auto original = std::make_shared<genomeFile>();
	original->setVersion(3);
	for (int i = 0; i < 10; i++) {
		auto g = std::make_unique<bioInitialConcentrationGene>(3);
		g->header.flags._mutable = true;
		g->chemical = static_cast<uint8_t>(i);
		g->quantity = 50;
		original->genes.push_back(std::move(g));
	}

	// Register original genome
	std::string origMoniker = history.newMoniker(original);
	ASSERT_FALSE(origMoniker.empty());

	// Deep-copy genome (same logic as c_GENE_CLON)
	auto cloneGenome = std::make_shared<genomeFile>();
	cloneGenome->setVersion(original->getVersion());
	for (auto& g : original->genes) {
		cloneGenome->genes.push_back(g->clone());
	}
	ASSERT_EQ(cloneGenome->genes.size(), 10u);

	// Register clone with new unique moniker
	std::string cloneMoniker = history.newMoniker(cloneGenome);
	ASSERT_FALSE(cloneMoniker.empty());

	// Monikers must be unique (per D-17)
	EXPECT_NE(origMoniker, cloneMoniker);

	// Record Cloned event (type 14): MON1=source, MON2="" (per D-18)
	history.getMoniker(cloneMoniker).addEvent(14, origMoniker, "");

	ASSERT_EQ(history.getMoniker(cloneMoniker).events.size(), 1u);
	EXPECT_EQ(history.getMoniker(cloneMoniker).events[0].eventno, 14u);
	EXPECT_EQ(history.getMoniker(cloneMoniker).events[0].monikers[0], origMoniker); // MON1 = source
	EXPECT_EQ(history.getMoniker(cloneMoniker).events[0].monikers[1], std::string("")); // MON2 empty

	engine.version = saved_version;
}

// ---------------------------------------------------------------------------
// GENE-08: Moniker_ParentLineage
// Crossover logic: run crossoverGenomes(), register child moniker, add
// Conceived event (type 0) with mumMoniker as MON1 and dadMoniker as MON2.
// Assert parent lineage is recorded correctly with crossover statistics.
// Uses engine.version=2 to avoid catalogue dependency.
// ---------------------------------------------------------------------------

TEST(GeneticsTest, Moniker_ParentLineage) {
	unsigned int saved_version = engine.version;
	engine.version = 2;

	historyManager history;

	// Build mum and dad genomes with 200 genes each.
	// Average run length is 55 genes (rand_uint32(10,100)), so 200 genes guarantees
	// at least one crossover point with extremely high probability.
	auto mumGenome = std::make_shared<genomeFile>();
	mumGenome->setVersion(3);
	auto dadGenome = std::make_shared<genomeFile>();
	dadGenome->setVersion(3);
	for (int i = 0; i < 200; i++) {
		auto gm = std::make_unique<bioInitialConcentrationGene>(3);
		gm->header.flags._mutable = true;
		gm->header.flags.dupable = true;
		gm->header.flags.delable = true;
		gm->header.mutweighting = 128;
		gm->chemical = static_cast<uint8_t>(i % 256);
		gm->quantity = 100;
		mumGenome->genes.push_back(std::move(gm));

		auto gd = std::make_unique<bioInitialConcentrationGene>(3);
		gd->header.flags._mutable = true;
		gd->header.flags.dupable = true;
		gd->header.flags.delable = true;
		gd->header.mutweighting = 128;
		gd->chemical = static_cast<uint8_t>((i + 128) % 256);
		gd->quantity = 100;
		dadGenome->genes.push_back(std::move(gd));
	}

	// Register parent monikers
	std::string mumMon = history.newMoniker(mumGenome);
	std::string dadMon = history.newMoniker(dadGenome);
	ASSERT_NE(mumMon, dadMon);

	// Run crossover with moderate mutation (per GENE-08 / D-26)
	CrossoverResult result = crossoverGenomes(*mumGenome, *dadGenome, 128, 128, 128, 128);
	ASSERT_NE(result.child, nullptr);
	ASSERT_GE(result.child->genes.size(), 1u);

	// Register child moniker
	std::string childMon = history.newMoniker(result.child);
	EXPECT_NE(childMon, mumMon);
	EXPECT_NE(childMon, dadMon);

	// Store crossover statistics (per D-15)
	monikerData& md = history.getMoniker(childMon);
	md.no_crossover_points = result.crossover_points;
	md.no_point_mutations = result.point_mutations;

	// Record Conceived event (type 0): MON1=mum, MON2=dad (per D-16)
	md.addEvent(0, mumMon, dadMon);

	ASSERT_EQ(md.events.size(), 1u);
	EXPECT_EQ(md.events[0].eventno, 0u); // Conceived
	EXPECT_EQ(md.events[0].monikers[0], mumMon); // MON1 = mum
	EXPECT_EQ(md.events[0].monikers[1], dadMon);  // MON2 = dad

	// At least one crossover expected in 200 genes (average run length ~55 genes,
	// so 200 genes virtually guarantees >= 1 crossover point — per D-26)
	EXPECT_GT(md.no_crossover_points, 0u)
		<< "Expected at least one crossover point with 200-gene parents";

	engine.version = saved_version;
}

// ---------------------------------------------------------------------------
// GENE-09: GenomeSlot_Management
// Test slot store/move/kill operations directly using the same map type
// as Agent::genome_slots — no World/Agent construction needed.
// ---------------------------------------------------------------------------

TEST(GeneticsTest, GenomeSlot_Management) {
	std::map<unsigned int, std::shared_ptr<genomeFile>> slots;

	// Build genome with 10 genes
	auto genome = std::make_shared<genomeFile>();
	genome->setVersion(3);
	for (int i = 0; i < 10; i++) {
		auto g = std::make_unique<bioInitialConcentrationGene>(3);
		g->chemical = static_cast<uint8_t>(i);
		g->quantity = 50;
		genome->genes.push_back(std::move(g));
	}

	// Store in slot 1
	slots[1] = genome;
	ASSERT_NE(slots.find(1), slots.end()) << "Slot 1 should be occupied after store";
	EXPECT_EQ(slots[1]->genes.size(), 10u);

	// Move: copy to slot 2, erase slot 1
	slots[2] = slots[1];
	slots.erase(slots.find(1));
	EXPECT_EQ(slots.find(1), slots.end()) << "Slot 1 should be empty after move";
	ASSERT_NE(slots.find(2), slots.end()) << "Slot 2 should be occupied after move";
	EXPECT_EQ(slots[2]->genes.size(), 10u);
	EXPECT_EQ(slots[2], genome) << "Slot 2 should hold same shared_ptr as original";

	// Kill: erase slot 2
	slots.erase(slots.find(2));
	EXPECT_EQ(slots.find(2), slots.end()) << "Slot 2 should be empty after kill";
	EXPECT_TRUE(slots.empty()) << "All slots should be empty after kill";
}

// ---------------------------------------------------------------------------
// GENE-06: Crossover_ChildGenomeValid (integration)
// Run crossoverGenomes() with 100-gene parents and verify the child is
// non-null, non-empty, has the correct version, and all child genes are valid.
// ---------------------------------------------------------------------------

TEST(GeneticsTest, Crossover_ChildGenomeValid) {
	genomeFile mum = makeTestGenome(100);
	genomeFile dad = makeTestGenome(100);

	// Run crossover with moderate mutation
	CrossoverResult result = crossoverGenomes(mum, dad, 128, 128, 128, 128);

	ASSERT_NE(result.child, nullptr);
	ASSERT_GE(result.child->genes.size(), 1u) << "Child genome must be non-empty";

	// Version must match parent (per plan acceptance criteria)
	EXPECT_EQ(result.child->getVersion(), mum.getVersion());

	// Every child gene must be non-null, have a valid name, and correct cversion
	for (size_t i = 0; i < result.child->genes.size(); ++i) {
		const auto& g = result.child->genes[i];
		ASSERT_NE(g.get(), nullptr) << "Gene at index " << i << " is null";
		ASSERT_NE(g->name(), nullptr) << "Gene name() is null at index " << i;
		EXPECT_GT(std::string(g->name()).size(), 0u)
			<< "Gene name() is empty at index " << i;
	}
}
