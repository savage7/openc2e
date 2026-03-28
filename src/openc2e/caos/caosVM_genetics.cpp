/*
 *  caosVM_genetics.cpp
 *  openc2e
 *
 *  Created by Alyssa Milburn on Fri Dec 9 2005.
 *  Copyright (c) 2005 Alyssa Milburn. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 */

#include "World.h"
#include "caosVM.h"
#include "common/throw_ifnot.h"
#include "creatures/CreatureAgent.h"
#include "creatures/GeneticAlgorithms.h"
#include "fileformats/genomeFile.h"
#include "historyManager.h"

#include <cassert>
#include <memory>

/**
 GENE CLON (command) dest_agent (agent) dest_slot (integer) src_agent (agent) src_slot (integer)
 %status maybe

 Clone a genome. A new moniker is created.
*/
void c_GENE_CLON(caosVM* vm) {
	VM_PARAM_INTEGER(src_slot)
	VM_PARAM_VALIDAGENT(src_agent)
	VM_PARAM_INTEGER(dest_slot)
	VM_PARAM_VALIDAGENT(dest_agent)

	// Find source genome (per D-17)
	auto src_it = src_agent->genome_slots.find(src_slot);
	THROW_IFNOT(src_it != src_agent->genome_slots.end());

	// Deep-copy genome: clone each gene (per D-17 — no crossover, no mutation)
	auto clone = std::make_shared<genomeFile>();
	clone->setVersion(src_it->second->getVersion());
	for (auto& g : src_it->second->genes) {
		clone->genes.push_back(g->clone());
	}

	// Store in destination slot
	dest_agent->genome_slots[dest_slot] = clone;

	// Create new unique moniker (per D-17)
	std::string cloneMoniker = world.history->newMoniker(clone);

	// Find source moniker for lineage
	std::string srcMoniker = world.history->findMoniker(src_it->second);

	// Record Cloned event (event type 14) with source as MON1, empty MON2 (per D-18)
	monikerData& md = world.history->getMoniker(cloneMoniker);
	md.addEvent(14, srcMoniker, "");
	md.moveToAgent(dest_agent);

	// Clone has zero crossover points and zero mutations
	md.no_crossover_points = 0;
	md.no_point_mutations = 0;
}

/**
 GENE CROS (command) dest_agent (agent) dest_slot (integer) mum_agent (agent) mum_slot (integer) dad_agent (agent) dad_slot (integer) mum_mutation_chance (integer) mum_mutation_degree (integer) dad_mutation_chance (integer) dad_mutation_degree (integer)
 %status maybe

 Cross two genomes, creating a new one.
*/
void c_GENE_CROS(caosVM* vm) {
	VM_PARAM_INTEGER(dad_mutation_degree)
	VM_PARAM_INTEGER(dad_mutation_chance)
	VM_PARAM_INTEGER(mum_mutation_degree)
	VM_PARAM_INTEGER(mum_mutation_chance)
	VM_PARAM_INTEGER(dad_slot)
	VM_PARAM_VALIDAGENT(dad_agent)
	VM_PARAM_INTEGER(mum_slot)
	VM_PARAM_VALIDAGENT(mum_agent)
	VM_PARAM_INTEGER(dest_slot)
	VM_PARAM_VALIDAGENT(dest_agent)

	// Retrieve parent genomes from slots
	auto mum_it = mum_agent->genome_slots.find(mum_slot);
	THROW_IFNOT(mum_it != mum_agent->genome_slots.end());
	auto dad_it = dad_agent->genome_slots.find(dad_slot);
	THROW_IFNOT(dad_it != dad_agent->genome_slots.end());

	genomeFile& mum = *mum_it->second;
	genomeFile& dad = *dad_it->second;

	// Run crossover algorithm (per D-14)
	CrossoverResult result = crossoverGenomes(
		mum, dad,
		static_cast<uint8_t>(mum_mutation_chance),
		static_cast<uint8_t>(mum_mutation_degree),
		static_cast<uint8_t>(dad_mutation_chance),
		static_cast<uint8_t>(dad_mutation_degree));

	// Store child genome in destination slot (per D-14)
	dest_agent->genome_slots[dest_slot] = result.child;

	// Create child moniker via history->newMoniker() directly (per D-15)
	// DO NOT use World::newMoniker() — that fires event 2 (Gene loaded)
	std::string childMoniker = world.history->newMoniker(result.child);

	// Find parent monikers
	std::string mumMoniker = world.history->findMoniker(mum_it->second);
	std::string dadMoniker = world.history->findMoniker(dad_it->second);

	// Record Conceived event (event type 0) with parent monikers (per D-16)
	monikerData& md = world.history->getMoniker(childMoniker);
	md.addEvent(0, mumMoniker, dadMoniker);
	md.moveToAgent(dest_agent);

	// Record crossover statistics (per D-15)
	md.no_crossover_points = result.crossover_points;
	md.no_point_mutations = result.point_mutations;
}

/**
 GENE KILL (command) agent (agent) slot (integer)
 %status maybe

 Delete a genome from a slot.
*/
void c_GENE_KILL(caosVM* vm) {
	VM_PARAM_INTEGER(slot)
	VM_PARAM_VALIDAGENT(agent)

	// Find genome in slot (per D-19)
	auto it = agent->genome_slots.find(slot);
	if (it == agent->genome_slots.end()) return; // slot empty, no-op

	// Update moniker status if exists
	std::string moniker = world.history->findMoniker(it->second);
	if (!moniker.empty()) {
		world.history->getMoniker(moniker).moveToAgent(AgentRef());
	}

	// Remove genome from slot
	agent->genome_slots.erase(it);
}

/**
 GENE LOAD (command) agent (agent) slot (integer) genefile (string)
 %status maybe

 Load a genome file into a slot. You can use * and ? wildcards in the filename.
*/
void c_GENE_LOAD(caosVM* vm) {
	VM_PARAM_STRING(genefile)
	VM_PARAM_INTEGER(slot)
	VM_PARAM_VALIDAGENT(agent)

	std::shared_ptr<genomeFile> p = world.loadGenome(genefile);
	if (!p)
		throw Exception("failed to find genome file '" + genefile + "'");

	THROW_IFNOT(p->getVersion() == 3);

	agent->genome_slots[slot] = p;
	world.newMoniker(p, genefile, agent);
}

/**
 GENE MOVE (command) dest_agent (agent) dest_slot (integer) src_agent (agent) src_slot (integer)
 %status maybe

 Move a genome to another slot.
*/
void c_GENE_MOVE(caosVM* vm) {
	VM_PARAM_INTEGER(src_slot)
	VM_PARAM_VALIDAGENT(src_agent)
	VM_PARAM_INTEGER(dest_slot)
	VM_PARAM_VALIDAGENT(dest_agent)

	std::map<unsigned int, std::shared_ptr<class genomeFile> >::iterator i = src_agent->genome_slots.find(src_slot);
	THROW_IFNOT(i != src_agent->genome_slots.end());

	std::string moniker = world.history->findMoniker(i->second);
	assert(moniker != std::string("")); // internal consistency, i think..

	dest_agent->genome_slots[dest_slot] = src_agent->genome_slots[src_slot];
	src_agent->genome_slots.erase(i);
	world.history->getMoniker(moniker).moveToAgent(dest_agent);
}

/**
 GTOS (string) slot (integer)
 %status maybe
 
 Return the moniker stored in the given gene slot of the target agent.
*/
void v_GTOS(caosVM* vm) {
	VM_PARAM_INTEGER(slot)

	valid_agent(vm->targ);
	if (vm->targ->genome_slots.find(slot) == vm->targ->genome_slots.end()) {
		vm->result.setString(""); // CV needs this, at least
	} else {
		std::shared_ptr<class genomeFile> g = vm->targ->genome_slots[slot];
		vm->result.setString(world.history->findMoniker(g));
	}
}

/**
 MTOA (agent) moniker (string)
 %status maybe

 Return the agent which has the given moniker stored in a gene slot, or NULL if none.
*/
void v_MTOA(caosVM* vm) {
	VM_PARAM_STRING(moniker)

	THROW_IFNOT(world.history->hasMoniker(moniker));
	vm->result.setAgent(world.history->getMoniker(moniker).owner);
}

/**
 MTOC (agent) moniker (string)
 %status maybe

 Return the live creature with the given moniker, or NULL if none.
*/
void v_MTOC(caosVM* vm) {
	VM_PARAM_STRING(moniker)

	vm->result.setAgent(0);
	if (!world.history->hasMoniker(moniker))
		return;
	Agent* a = world.history->getMoniker(moniker).owner;
	if (!a)
		return;
	CreatureAgent* c = dynamic_cast<CreatureAgent*>(a);
	THROW_IFNOT(c); // TODO: is this assert valid? can history events have non-creature owners?
	vm->result.setAgent(a);
}

/**
 NEW: GENE (command) mum (integer) dad (integer) destination (variable)
 %status stub
 %variants c1 c2
*/
void c_NEW_GENE(caosVM* vm) {
	VM_PARAM_VARIABLE(destination)
	VM_PARAM_INTEGER_UNUSED(dad)
	VM_PARAM_INTEGER(mum)

	destination->setInt(mum); // TODO
}

/* vim: set noet: */
