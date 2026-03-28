/*
 * GeneticAlgorithms.cpp
 * openc2e
 *
 * Phase 04, Plan 01 — GENE-01 through GENE-05
 *
 * Implementations of genome crossover, power-curve mutation, and cutting
 * errors (duplication/excision). All algorithms are clean-room reimplementations
 * using C3 engine algorithm references.
 */

#include "creatures/GeneticAlgorithms.h"

#include "common/Random.h"
#include "common/io/SpanReader.h"
#include "common/io/VectorWriter.h"
#include "common/span.h"

#include <cmath>
#include <fmt/core.h>

// ---------------------------------------------------------------------------
// mutateGeneData — power-curve mutation on gene data bytes (D-05 through D-09)
// ---------------------------------------------------------------------------

unsigned int mutateGeneData(gene& g, uint8_t parentChance, uint8_t parentDegree) {
	// D-09: MUT flag guard — immutable genes are never mutated
	if (!g.header.flags._mutable) {
		return 0;
	}

	// Serialize gene data bytes only (the virtual write() writes only the
	// subclass data portion, not the header — confirmed by operator<< which
	// writes the header separately then calls g.write(s)).
	VectorWriter writer;
	g.write(writer);
	std::vector<uint8_t> data = writer.vector();

	if (data.empty()) {
		return 0;
	}

	// Three-factor scaling of the base mutation rate (D-06 / D-07):
	//   factor 1: gene's own mutweighting (lower weight = more mutable)
	//   factor 2: parent's mutation chance parameter
	//   base rate is MUTATION_BASERATE = 4800
	int baseChance = MUTATION_BASERATE;
	baseChance = (baseChance * (256 - static_cast<int>(g.header.mutweighting))) / 256;
	baseChance = (baseChance * (256 - static_cast<int>(parentChance))) / 256;
	if (baseChance < 1) {
		baseChance = 1;
	}

	unsigned int mutations = 0;

	// D-05: power-curve formula — dP = pow(dRandom, dDegree)
	// D-08: XOR mutation
	for (uint8_t& byte : data) {
		if (rand_uint32(0, static_cast<uint32_t>(baseChance) - 1) == 0) {
			// Degree range: parentDegree=0 → dDegree≈128 (large power, small changes)
			//               parentDegree=255 → dDegree=1.0 (linear, bigger changes)
			double dDegree = 1.0 + (127.0 * (static_cast<double>(255 - parentDegree) / 255.0));
			double dRandom = static_cast<double>(rand_float(0.f, 1.f));
			double dP = pow(dRandom, dDegree);
			uint8_t mask = static_cast<uint8_t>(255.0 * dP);
			if (mask == 0) {
				mask = 1;
			}
			byte ^= mask; // D-08: XOR mutation
			++mutations;
		}
	}

	if (mutations > 0) {
		// Deserialize mutated bytes back into the gene's data fields.
		// SpanReader takes a span<const uint8_t>.
		span<const uint8_t> dataSpan(data.data(), data.size());
		SpanReader reader(dataSpan);
		g.read(reader);
	}

	return mutations;
}

// ---------------------------------------------------------------------------
// crossoverGenomes — run-length crossover model (D-01 through D-04, D-10 through D-12)
// ---------------------------------------------------------------------------

CrossoverResult crossoverGenomes(
	genomeFile& mum, genomeFile& dad,
	uint8_t mumMutChance, uint8_t mumMutDegree,
	uint8_t dadMutChance, uint8_t dadMutDegree) {

	CrossoverResult result;
	result.crossover_points = 0;
	result.point_mutations = 0;

	// Version mismatch edge case: clone mum's genes and return
	if (mum.getVersion() != dad.getVersion()) {
		auto child = std::make_shared<genomeFile>();
		child->setVersion(mum.getVersion());
		for (auto& g : mum.genes) {
			child->genes.push_back(g->clone());
		}
		result.child = child;
		return result;
	}

	auto child = std::make_shared<genomeFile>();
	child->setVersion(mum.getVersion());

	// D-04: randomly choose which parent to start from
	bool useMum = rand_bool();
	genomeFile* src = useMum ? &mum : &dad;
	genomeFile* alt = useMum ? &dad : &mum;
	uint8_t srcChance = useMum ? mumMutChance : dadMutChance;
	uint8_t srcDegree = useMum ? mumMutDegree : dadMutDegree;
	uint8_t altChance = useMum ? dadMutChance : mumMutChance;
	uint8_t altDegree = useMum ? dadMutDegree : mumMutDegree;

	// Run-length crossover model: genes are copied in runs; when the run
	// length expires, we switch to the other parent (per RESEARCH.md D-01).
	// Run range is [10, CROSSOVER_LINKAGE * 2] = [10, 100].
	int runLength = static_cast<int>(rand_uint32(10, static_cast<uint32_t>(CROSSOVER_LINKAGE) * 2));

	size_t i = 0;

	while (i < src->genes.size()) {
		// Check for crossover (run exhausted)
		if (runLength <= 0) {
			// Crossover: switch to other parent
			++result.crossover_points;
			std::swap(src, alt);
			std::swap(srcChance, altChance);
			std::swap(srcDegree, altDegree);
			runLength = static_cast<int>(rand_uint32(10, static_cast<uint32_t>(CROSSOVER_LINKAGE) * 2));

			// If the new source is exhausted, the remaining genes from the old
			// source (now alt) are dropped — this is consistent with crossover
			// semantics where we follow the new source.
			if (i >= src->genes.size()) {
				break;
			}
		}

		// D-10: cutting error check (1 in CROSSOVER_CUTERRORRATE chance)
		bool cuttingError = (rand_uint32(0, static_cast<uint32_t>(CROSSOVER_CUTERRORRATE) - 1) == 0);
		bool handled = false;

		if (cuttingError) {
			// D-11: pick error type: 1=duplication, 2=excision
			int errType = static_cast<int>(rand_uint32(1, 2));

			if (errType == 1) {
				// D-12: duplication — only if gene has dupable flag
				if (src->genes[i]->header.flags.dupable) {
					// Clone and mutate twice — gene is duplicated
					auto copy1 = src->genes[i]->clone();
					result.point_mutations += mutateGeneData(*copy1, srcChance, srcDegree);
					child->genes.push_back(std::move(copy1));

					auto copy2 = src->genes[i]->clone();
					result.point_mutations += mutateGeneData(*copy2, srcChance, srcDegree);
					child->genes.push_back(std::move(copy2));

					handled = true;
				}
			} else {
				// D-12: excision — only if gene has delable flag
				if (src->genes[i]->header.flags.delable) {
					// Skip this gene entirely (excised from child genome)
					handled = true;
				}
			}
		}

		if (!handled) {
			// Normal copy: clone gene and mutate it
			auto cloned = src->genes[i]->clone();
			result.point_mutations += mutateGeneData(*cloned, srcChance, srcDegree);
			child->genes.push_back(std::move(cloned));
		}

		--runLength;
		++i;
	}

	result.child = child;
	return result;
}

/* vim: set noet: */
