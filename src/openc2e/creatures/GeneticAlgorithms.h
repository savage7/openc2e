/*
 * GeneticAlgorithms.h
 * openc2e
 *
 * Phase 04, Plan 01 — GENE-01 through GENE-05
 *
 * Core genetics algorithms: genome crossover with run-length crossover points,
 * power-curve mutation, and cutting errors (duplication/excision).
 * All functions are standalone free functions with no Creature dependency.
 */

#pragma once

#include "fileformats/genomeFile.h"

#include <memory>

// Algorithm constants (from C3 engine reference — clean-room reimplementation)
constexpr int CROSSOVER_LINKAGE = 50;     // average genes between crossover points
constexpr int CROSSOVER_CUTERRORRATE = 80; // 1-in-80 cutting error chance
constexpr int MUTATION_BASERATE = 4800;    // base 1-in-4800 per codon

// Crossover result with statistics
struct CrossoverResult {
	std::shared_ptr<genomeFile> child;
	unsigned int crossover_points; // count of crossover events
	unsigned int point_mutations;  // count of mutated codons
};

// Core algorithms (per D-01 through D-12)

// Crossover two parent genomes to produce a child genome.
// mumMutChance/mumMutDegree and dadMutChance/dadMutDegree control the
// per-parent mutation parameters applied as genes are copied.
// (GENE-01 through GENE-05)
CrossoverResult crossoverGenomes(
	genomeFile& mum, genomeFile& dad,
	uint8_t mumMutChance, uint8_t mumMutDegree,
	uint8_t dadMutChance, uint8_t dadMutDegree);

// Mutate a single gene's data bytes in-place using the power-curve formula.
// Gene header bytes (type, subtype, generation, flags) are never modified.
// Returns the number of codons that were mutated. (D-05 through D-09)
unsigned int mutateGeneData(gene& g, uint8_t parentChance, uint8_t parentDegree);
