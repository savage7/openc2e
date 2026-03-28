/*
 *  Creature.cpp
 *  openc2e
 *
 *  Created by Alyssa Milburn on Tue May 25 2004.
 *  Copyright (c) 2004-2006 Alyssa Milburn. All rights reserved.
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

#include "Creature.h"

#include "Catalogue.h"
#include "CreatureAgent.h"
#include "SkeletalCreature.h"
#include "World.h"
#include "c2eBrain.h"
#include "c2eCreature.h"
#include "common/throw_ifnot.h"
#include "fileformats/genomeFile.h"
#include "historyManager.h"
#include "oldBrain.h"
#include "oldCreature.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <typeinfo>

Creature::Creature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a) {
	assert(g);
	genome = g;

	female = is_female;
	genus = 0; // TODO: really, we shouldn't do this, and should instead later assert that a genus was set
	variant = _variant;
	stage = baby;

	parent = a;
	assert(parent);
	parentagent = dynamic_cast<Agent*>(parent);
	assert(parentagent);

	alive = true; // ?
	asleep = false; // ?
	dreaming = false; // ?
	tickage = false;
	zombie = false;

	age = ticks = 0;

	attn = decn = -1;

	for (unsigned short& i : tintinfo)
		i = 128;
}

Creature::~Creature() {
}

void Creature::finishInit() {
	processGenes();
}

bool Creature::shouldProcessGene(gene* g, GeneSwitch mode) {
	geneFlags& flags = g->header.flags;

	// non-expressed genes are to be ignored
	if (flags.notexpressed)
		return false;

	// gender-specific genes are only to be processed if they are of this gender
	if (flags.femaleonly && !female)
		return false;
	if (flags.maleonly && female)
		return false;

	// Variant filter (AGE-04): variant 0 means "all variants"; non-zero must match creature variant
	if (g->header.variant != 0 && g->header.variant != variant)
		return false;

	// Switch mode logic (AGE-01 through AGE-03, per lc2e Genome.cpp:739-754 clean-room)
	switch (mode) {
		case GeneSwitch::Age:     return g->header.switchontime == stage;
		case GeneSwitch::UpToAge: return g->header.switchontime <= stage;
		case GeneSwitch::Always:  return true;
		case GeneSwitch::Embryo:  return stage == baby;
	}
	return false;
}

void Creature::processGenes() {
	for (auto& g : genome->genes) {
		// Determine the correct switch mode per gene type (AGE-01 through AGE-03).
		// In lc2e the switch mode is a call-site parameter, not stored in the gene.
		// Brain lobe and tract genes use UpToAge so a mutated switchontime cannot
		// prevent brain structure from initialising.
		// Instinct genes use Always — they are collected at every age transition.
		// All other genes (biochemistry, appearance, etc.) use Age (exact match).
		GeneSwitch mode = GeneSwitch::Age;
		const gene* gptr = g.get();
		if (typeid(*gptr) == typeid(c2eBrainLobeGene) ||
		    typeid(*gptr) == typeid(c2eBrainTractGene)) {
			mode = GeneSwitch::UpToAge;
		} else if (typeid(*gptr) == typeid(creatureInstinctGene)) {
			mode = GeneSwitch::Always;
		}
		if (shouldProcessGene(g.get(), mode))
			addGene(g.get());
	}
}

void oldCreature::processGenes() {
	brain->processGenes();
	Creature::processGenes();
}

void c2Creature::processGenes() {
	oldCreature::processGenes();

	for (auto& organ : organs) {
		organ->processGenes();
	}
}

void c2eCreature::processGenes() {
	// brain must be processed first (to create loci etc)
	// organs should be processed last, because new ones will be created by normal processGenes()

	brain->processGenes();
	Creature::processGenes();
	for (auto& organ : organs) {
		organ->processGenes();
	}
}

void Creature::addGene(gene* g) {
	if (typeid(*g) == typeid(creatureInstinctGene)) {
		unprocessedinstincts.push_back((creatureInstinctGene*)g);
	} else if (typeid(*g) == typeid(creatureGenusGene)) {
		// TODO: mmh, genus changes after setup shouldn't be valid
		genus = ((creatureGenusGene*)g)->genus;
		parentagent->genus = genus + 1;
	} else if (typeid(*g) == typeid(creaturePigmentGene)) {
		creaturePigmentGene& p = *((creaturePigmentGene*)g);
		// TODO: we don't sanity-check
		tintinfo[p.color] = p.amount;
	} else if (typeid(*g) == typeid(creaturePigmentBleedGene)) {
		creaturePigmentBleedGene& p = *((creaturePigmentBleedGene*)g);
		tintinfo[3] = p.rotation;
		tintinfo[4] = p.swap;
	}
}

void Creature::ageCreature() {
	if (stage >= senile) {
		die();
		return;
	} //previously we just returned

	stage = (lifestage)((int)stage + 1);
	processGenes();

	assert(parent);
	parent->creatureAged();
#ifndef _CREATURE_STANDALONE
	world.history->getMoniker(world.history->findMoniker(genome)).addEvent(4, "", ""); // aged event
#endif
}

void Creature::setAsleep(bool a) {
	// TODO: skeletalcreature might need to close eyes? or should that just be done during the skeletal update?
	if (!a && dreaming)
		setDreaming(false);
	asleep = a;
}

void Creature::setDreaming(bool d) {
	if (d && !asleep)
		setAsleep(true);
	dreaming = d;
}

void Creature::born() {
	parent->creatureBorn();

	// TODO: life event?
#ifndef _CREATURE_STANDALONE
	world.history->getMoniker(world.history->findMoniker(genome)).wasBorn();
	world.history->getMoniker(world.history->findMoniker(genome)).addEvent(3, "", ""); // born event, parents..
#endif

	tickage = true;
}

void Creature::die() {
	parent->creatureDied();

	// TODO: life event?
#ifndef _CREATURE_STANDALONE
	world.history->getMoniker(world.history->findMoniker(genome)).hasDied();
	world.history->getMoniker(world.history->findMoniker(genome)).addEvent(7, "", ""); // died event
#endif
	// TODO: disable brain/biochemistry updates

	// force die script
	parentagent->stopScript();
	parentagent->queueScript(72); // Death script in c1, c2 and c2e

	// skeletalcreature eyes, also? see setAsleep comment
	alive = false;
}

void Creature::tick() {
	ticks++;

	if (!alive)
		return;

	if (tickage)
		age++;
}

/*
 * oldCreature contains the shared elements of C1 of C2 (creatures are mostly identical in both games)
 */
oldCreature::oldCreature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a)
	: Creature(g, is_female, _variant, a) {
	biochemticks = 0;
	halflives = 0;

	for (unsigned char& i : floatingloci)
		i = 0;
	for (unsigned char& i : lifestageloci)
		i = 0;
	for (unsigned char& i : involaction)
		i = 0;
	for (unsigned char& chemical : chemicals)
		chemical = 0;

	for (unsigned int& i : involactionlatency)
		i = 0;

	muscleenergy = 0;
	fertile = pregnant = receptive = 0;
	dead = 0;

	brain = 0; // just in case
}

c1Creature::c1Creature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a)
	: oldCreature(g, is_female, _variant, a) {
	assert(g->getVersion() == 1);

	for (unsigned char& sense : senses)
		sense = 0;
	for (unsigned char& i : gaitloci)
		i = 0;
	for (unsigned char& drive : drives)
		drive = 0;

	// TODO: chosenagents size

	brain = new oldBrain(this);
	finishInit();
	brain->init();
}

c2Creature::c2Creature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a)
	: oldCreature(g, is_female, _variant, a) {
	assert(g->getVersion() == 2);

	for (unsigned char& sense : senses)
		sense = 0;
	for (unsigned char& i : gaitloci)
		i = 0;
	for (unsigned char& drive : drives)
		drive = 0;

	mutationchance = 0;
	mutationdegree = 0;

	// TODO: chosenagents size

	brain = new oldBrain(this);
	finishInit();
	brain->init();
}

c2eCreature::c2eCreature(std::shared_ptr<genomeFile> g, bool is_female, unsigned char _variant, CreatureAgent* a)
	: Creature(g, is_female, _variant, a) {
	assert(g->getVersion() == 3);

	for (float& chemical : chemicals)
		chemical = 0.0f;

	// initialise loci
	for (float& i : lifestageloci)
		i = 0.0f;
	muscleenergy = 0.0f;
	for (float& i : floatingloci)
		i = 0.0f;
	fertile = pregnant = ovulate = receptive = chanceofmutation = degreeofmutation = 0.0f;
	dead = 0.0f;
	for (float& i : involaction)
		i = 0.0f;
	for (float& i : gaitloci)
		i = 0.0f;
	for (float& sense : senses)
		sense = 0.0f;
	for (float& drive : drives)
		drive = 0.0f;

	for (unsigned int& i : involactionlatency)
		i = 0;

	halflives = 0;

	// Initialise expression weight tables to zero
	for (int e = 0; e < 6; e++) {
		expressionOverall[e] = 0.0f;
		for (int d = 0; d < 20; d++) {
			expressionWeights[e][d] = 0.0f;
		}
	}

	// Initialise pending STIM buffers to zero
	for (int i = 0; i < 40; i++) {
		pendingVerbStim[i] = 0.0f;
		pendingNounStim[i] = 0.0f;
	}

	if (!catalogue.hasTag("Action Script To Neuron Mappings"))
		throw Exception("c2eCreature was unable to read the 'Action Script To Neuron Mappings' catalogue tag");
	const std::vector<std::string>& mappinginfotag = catalogue.getTag("Action Script To Neuron Mappings");
	for (const auto& i : mappinginfotag)
		mappinginfo.push_back(std::stoi(i));

	// TODO: should we really hard-code this?
	chosenagents.resize(40);

	brain = new c2eBrain(this);
	finishInit();
	brain->init();
}

unsigned int c1Creature::getGait() {
	unsigned int gait = 0;

	for (unsigned int i = 1; i < 8; i++)
		if (gaitloci[i] > gaitloci[gait])
			gait = i;

	return gait;
}

unsigned int c2Creature::getGait() {
	unsigned int gait = 0;

	for (unsigned int i = 1; i < 16; i++)
		if (gaitloci[i] > gaitloci[gait])
			gait = i;

	return gait;
}

unsigned int c2eCreature::getGait() {
	unsigned int gait = 0;

	for (unsigned int i = 1; i < 16; i++)
		if (gaitloci[i] > gaitloci[gait])
			gait = i;

	return gait;
}

void c1Creature::tick() {
	// TODO: should we tick some things even if dead?
	if (!alive)
		return;

	senses[0] = 255; // always-on
	senses[1] = (asleep ? 255 : 0); // asleep
	senses[2] = 0; // air coldness (TODO)
	senses[3] = 0; // air hotness (TODO)
	senses[4] = 0; // light level (TODO)
	senses[5] = 0; // crowdedness (TODO)

	tickBrain();
	tickBiochemistry();

	// lifestage checks
	for (unsigned int i = 0; i < 7; i++) {
		if ((lifestageloci[i] != 0) && (stage == (lifestage)i))
			ageCreature();
	}

	if (dead != 0)
		die();

	Creature::tick();
}

void c2Creature::tick() {
	// TODO: should we tick some things even if dead?
	if (!alive)
		return;

	senses[0] = 255; // always-on
	senses[1] = (asleep ? 255 : 0); // asleep
	senses[2] = 0; // air coldness (TODO)
	senses[3] = 0; // air hotness (TODO)
	senses[4] = 0; // light level (TODO)
	senses[5] = 0; // crowdedness (TODO)
	senses[6] = 0; // radiation (TODO)
	senses[7] = 0; // time of day (TODO)
	senses[8] = 0; // season (TODO)
	senses[9] = 255; // air quality (TODO)
	senses[10] = 0; // slope up (TODO)
	senses[11] = 0; // slope down (TODO)
	senses[12] = 0; // wind towards (TODO)
	senses[13] = 0; // wind behind (TODO)

	tickBrain();
	// TODO: update brain organ every 0.4ms (ie: when brain is processed)!
	tickBiochemistry();

	// lifestage checks
	for (unsigned int i = 0; i < 7; i++) {
		if ((lifestageloci[i] != 0) && (stage == (lifestage)i))
			ageCreature();
	}

	if (dead != 0)
		die();

	Creature::tick();
}

void c2eCreature::updateSensoryFaculty() {
	// Environmental senses not tied to brain lobes (per D-12)
	// senses[0] (always-on) and senses[1] (asleep) are already set in tick()
	// senses[5] crowdedness left as 0.0f for Phase 5 (per research recommendation)
	// senses[9] air quality left as 1.0f for Phase 5
	// Note: brain lobe inputs (visn, smel, verb, noun) are updated inside
	// tickBrain() where they must be set BEFORE brain.tick() (Pitfall 3)
}

void c2eCreature::updateLifeFaculty() {
	// Aggregate organ lifeforces to determine overall creature health (per D-06)
	float totalLifeforce = 0.0f;
	for (auto& organ : organs) {
		totalLifeforce += organ->getShortTermLifeforce();
	}
	// Death: if all organs have zero lifeforce and creature has been alive long enough
	// Only SET dead locus — actual die() call remains at existing location in tick() (per D-04)
	if (!organs.empty() && totalLifeforce <= 0.0f && ticks > 100) {
		dead = 1.0f;
	}
}

void c2eCreature::updateMotorFaculty() {
	// Energy feedback from organ health into biochemistry (per D-16)
	// muscleenergy is an emitter locus (organ=1, tissue=0, locus=0)
	// NOTE: The full motor chain (brain -> gaitloci[] -> getGait() -> setGaitGene())
	// is already functional via the locus/receptor system + SkeletalCreature::creatureTick().
	// This method only provides the energy feedback piece.
	if (organs.empty()) {
		muscleenergy = 0.0f;
	} else {
		float totalLifeforce = 0.0f;
		for (auto& organ : organs) {
			totalLifeforce += organ->getShortTermLifeforce();
		}
		muscleenergy = std::min(1.0f, totalLifeforce / static_cast<float>(organs.size()));
	}
}

void c2eCreature::updateExpressiveFaculty() {
	// Map drives to a facial expression index [0,5] via expression gene weights (per D-11, D-13)
	// For each expression compute: score = sum_d(expressionWeights[e][d] * (drives[d] - 0.5f)) * expressionOverall[e]
	// Choose argmax; default to 0 (EXPR_NORMAL) if all scores <= 0
	SkeletalCreature* sc = dynamic_cast<SkeletalCreature*>(parentagent);
	if (!sc)
		return;

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
	// Clamp to [0, 5] (per Pitfall 6)
	if (bestExprId < 0) bestExprId = 0;
	if (bestExprId > 5) bestExprId = 5;
	sc->setFacialExpression((unsigned int)bestExprId);
}

void c2eCreature::updateReproductiveFaculty() {
	// Ovulate thermostat with hysteresis (per D-07, D-09)
	// OVULATEOFF = 0.314f, OVULATEON = 0.627f
	static const float OVULATEOFF = 0.314f;
	static const float OVULATEON = 0.627f;

	if (myGamete && ovulate < OVULATEOFF) {
		myGamete = false;
	} else if (!myGamete && ovulate > OVULATEON) {
		myGamete = true;
	}
	fertile = myGamete ? 1.0f : 0.0f;

	// Pregnancy state: slot 1 occupied => pregnant = 1.0f (per D-09)
	// genome_slots is on the Agent base class (Agent.h line 98)
	auto it = parentagent->genome_slots.find(1);
	if (it != parentagent->genome_slots.end() && it->second) {
		pregnant = 1.0f;
	} else {
		pregnant = 0.0f;
	}
}

void c2eCreature::updateLinguisticFaculty() {
	// LinguisticFaculty: hearing->vocabulary wiring handled via tickBrain() verb/noun lobes + VOCB CAOS command
	// No direct logic here — verb/noun lobe inputs are driven by pendingVerbStim/pendingNounStim arrays
	// which are populated in handleStimulus() and consumed in tickBrain() (Task 3)
}

void c2eCreature::updateMusicFaculty() {
	// MusicFaculty: Update() is a no-op per lc2e. Mood()/Threat() output state used by MNG audio selection.
}

void c2eCreature::tick() {
	// TODO: should we tick some things even if dead?
	if (!alive)
		return;

	senses[0] = 1.0f; // always-on
	senses[1] = (asleep ? 1.0f : 0.0f); // asleep
	// space for old C2 senses: hotness, coldness, light level
	senses[5] = 0.0f; // crowedness (TODO)
	// space for old C2 senses: radiation, time of day, season
	senses[9] = 1.0f; // air quality (TODO)
	senses[10] = 0.0f; // steepness of upcoming slope (up) (TODO)
	senses[11] = 0.0f; // steepness of upcoming slope (down) (TODO)
	// space for old C2 senses: oncoming wind, wind from behind

	tickBrain();
	tickNeuroEmitters();
	tickBiochemistry();
	// SensoryFaculty: environmental senses (per D-17/D-18, brain lobe inputs are in tickBrain)
	updateSensoryFaculty();
	// MotorFaculty: energy feedback from organ health (per D-17, D-16)
	updateMotorFaculty();
	// Phase 6 faculties: expressive, reproductive, linguistic, music
	updateExpressiveFaculty();
	updateReproductiveFaculty();
	updateLinguisticFaculty();
	updateMusicFaculty();

	// lifestage checks
	for (unsigned int i = 0; i < 7; i++) {
		if ((lifestageloci[i] != 0.0f) && (stage == (lifestage)i))
			ageCreature();
	}

	// LifeFaculty: aggregate organ health -> set dead locus (per D-17)
	updateLifeFaculty();

	if (dead != 0.0f)
		die();

	Creature::tick();
}

void oldCreature::addGene(gene* g) {
	Creature::addGene(g);
	if (typeid(*g) == typeid(bioInitialConcentrationGene)) {
		// initialise chemical levels
		bioInitialConcentrationGene* b = (bioInitialConcentrationGene*)(g);
		chemicals[b->chemical] = b->quantity;
	} else if (typeid(*g) == typeid(bioHalfLivesGene)) {
		bioHalfLivesGene* d = dynamic_cast<bioHalfLivesGene*>(g);
		assert(d);
		halflives = d;
	}
}

void c1Creature::addGene(gene* g) {
	oldCreature::addGene(g);

	if (typeid(*g) == typeid(bioReactionGene)) {
		reactions.push_back(std::shared_ptr<c1Reaction>(new c1Reaction((bioReactionGene*)g)));
	} else if (typeid(*g) == typeid(bioEmitterGene)) {
		emitters.push_back(c1Emitter((bioEmitterGene*)g, this));
	} else if (typeid(*g) == typeid(bioReceptorGene)) {
		receptors.push_back(c1Receptor((bioReceptorGene*)g, this));
	}
}

void c2Creature::addGene(gene* g) {
	oldCreature::addGene(g);

	if (typeid(*g) == typeid(organGene)) {
		// create organ
		organGene* o = dynamic_cast<organGene*>(g);
		assert(o);
		if (!o->isBrain()) { // TODO: handle brain organ
			organs.push_back(std::shared_ptr<c2Organ>(new c2Organ(this, o)));
		}
	} else if (typeid(*g) == typeid(bioReactionGene)) {
		THROW_IFNOT(organs.size() > 0);
		organs.back()->genes.push_back(g);
	} else if (typeid(*g) == typeid(bioEmitterGene)) {
		THROW_IFNOT(organs.size() > 0);
		organs.back()->genes.push_back(g);
	} else if (typeid(*g) == typeid(bioReceptorGene)) {
		THROW_IFNOT(organs.size() > 0);
		organs.back()->genes.push_back(g);
	}
}

void c2eCreature::tickNeuroEmitters() {
	for (auto& ne : neuroemitters) {
		ne.bioTick += ne.data->rate / 255.0f;
		if (ne.bioTick < 1.0f)
			continue;
		ne.bioTick -= 1.0f;

		// Multiply all three neuron activations together
		// Null inputs treated as 1.0 (lc2e myDefaultNeuronInput = 1.0f)
		float activation = 1.0f;
		for (int i = 0; i < 3; i++) {
			if (ne.inputs[i])
				activation *= *ne.inputs[i];
		}

		// Emit each chemical proportional to activation
		for (int o = 0; o < 4; o++) {
			if (ne.data->chemical[o] == 0) continue; // chemical 0 = no chemical
			float amount = (ne.data->quantity[o] / 255.0f) * activation;
			adjustChemical(ne.data->chemical[o], amount);
		}
	}
}

void c2eCreature::addGene(gene* g) {
	Creature::addGene(g);

	if (typeid(*g) == typeid(bioInitialConcentrationGene)) {
		// initialise chemical levels
		bioInitialConcentrationGene* b = (bioInitialConcentrationGene*)(g);
		chemicals[b->chemical] = b->quantity / 255.0f; // TODO: correctness unchecked
	} else if (typeid(*g) == typeid(organGene)) {
		// create organ
		organGene* o = dynamic_cast<organGene*>(g);
		assert(o);
		if (!o->isBrain()) { // TODO: handle brain organ
			organs.push_back(std::shared_ptr<c2eOrgan>(new c2eOrgan(this, o)));
		}
	} else if (typeid(*g) == typeid(bioReactionGene)) {
		THROW_IFNOT(organs.size() > 0);
		organs.back()->genes.push_back(g);
	} else if (typeid(*g) == typeid(bioEmitterGene)) {
		THROW_IFNOT(organs.size() > 0);
		organs.back()->genes.push_back(g);
	} else if (typeid(*g) == typeid(bioReceptorGene)) {
		THROW_IFNOT(organs.size() > 0);
		organs.back()->genes.push_back(g);
	} else if (typeid(*g) == typeid(bioHalfLivesGene)) {
		bioHalfLivesGene* d = dynamic_cast<bioHalfLivesGene*>(g);
		assert(d);
		halflives = d;
	} else if (typeid(*g) == typeid(bioNeuroEmitterGene)) {
		bioNeuroEmitterGene* neg = (bioNeuroEmitterGene*)g;
		// Check for duplicate: replace if all 3 input pointers match (prevents double emission on age-switch re-expression)
		c2eNeuroEmitter newne(neg, this);
		bool replaced = false;
		for (auto& existing : neuroemitters) {
			if (existing.inputs[0] == newne.inputs[0] &&
				existing.inputs[1] == newne.inputs[1] &&
				existing.inputs[2] == newne.inputs[2]) {
				existing = newne;
				replaced = true;
				break;
			}
		}
		if (!replaced)
			neuroemitters.push_back(newne);
	} else if (typeid(*g) == typeid(creatureFacialExpressionGene)) {
		// Load expression gene drive weights into expression tables (per Pitfall 1)
		creatureFacialExpressionGene* eg = (creatureFacialExpressionGene*)g;
		int exprno = (int)eg->expressionno;
		if (exprno >= 0 && exprno <= 5) {
			expressionOverall[exprno] = eg->weight / 255.0f;
			for (int i = 0; i < 4; i++) {
				int driveIndex = (int)eg->drives[i];
				if (driveIndex >= 0 && driveIndex < 20) {
					expressionWeights[exprno][driveIndex] = eg->amounts[i] / 255.0f;
				}
			}
		}
	}
}

void c2eCreature::adjustDrive(unsigned int id, float value) {
	assert(id < 20);
	drives[id] += value;

	if (drives[id] < 0.0f)
		drives[id] = 0.0f;
	else if (drives[id] > 1.0f)
		drives[id] = 1.0f;
}

/* vim: set noet: */
