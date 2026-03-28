# openc2e Fork — Changes Documentation

## Overview

This fork completes openc2e into a functional Creatures 3 / Docking Station runtime. Starting from the upstream codebase (which had a working engine shell but incomplete creature simulation), this work implements the full creature lifecycle: brain computation, biochemistry bridge, learning, genetics, all seven faculties, CAOS creature commands, and debugging tools.

**Result:** Norns live, learn, breed, and die on original C3/DS game data.

## What Was Built (v1.0 Milestone)

### Phase 1: Foundation
**Files modified:** `c2eBrain.cpp`, `c2eBrain.h`, `Biochemistry.cpp`, `caosVM_creatures.cpp`, `caosVM_genetics.cpp`

- **SVRule opcodes 37-42** (leakage, rest state, input gain, persistence, signal noise, winner-takes-all) — previously returned `warnUnimplementedSVRule()`, now compute correctly
- **SVRule opcodes 57-62** (reward/punishment threshold, rate, use) — `ReinforcementDetails` struct added to `c2eBrain.h` for per-tract reward/punishment configuration
- **Biochemistry flag fixes:** EM_REMOVE ordering bug (locus cleared only after successful emission, not before threshold check), receptor flag semantics (RE_REDUCE = bit 1, confirmed no RE_INVERTED in C3)
- **Chemical index audit:** All 7 hardcoded indices justified from Materia Medica.catalogue and Brain.catalogue (clean-room compliance)
- **SDLMixer thread-safety:** 10 `SDL_LockAudio` guards added to main-thread audio channel access functions

### Phase 2: NeuroEmitter Bridge
**Files modified:** `c2eCreature.h`, `Creature.cpp`, `Biochemistry.cpp`, `c2eBrain.h`

- **Brain locus bug fix:** `getLocusPointer()` line 379 — variable `o` changed to `l` for neuronid/stateno calculation. Previously ALL brain locus accesses returned neuron 0, state 0 regardless of requested locus
- **`c2eNeuroEmitter` struct:** New struct processing `bioNeuroEmitterGene` data — maps 3 lobe/neuron sources to 4 chemical outputs with configurable emission rates
- **Tick cycle wiring:** `tickNeuroEmitters()` inserted between `tickBrain()` and `tickBiochemistry()` — brain computes, neuro-emitters sample, biochemistry processes
- **Closed-loop verification:** Chemical → receptor → neuron → emitter → chemical forms measurable cycle with no disconnected paths

### Phase 3: Learning & Aging
**Files modified:** `Creature.cpp`, `Creature.h`, `CreatureAI.cpp`, `c2eBrain.cpp`, `c2eBrain.h`
**Files created:** `GeneSwitch.h`

- **Instinct wipe() fix:** `processInstinct()` now clears only neuron activation state (`variables[0] = 0`), NOT dendrite long-term weights. Previously `lobe->wipe()` destroyed all learned behavior during every instinct replay
- **Gene switch modes:** `shouldProcessGene()` extended with `SWITCH_UPTOAGE`, `SWITCH_ALWAYS`, `SWITCH_EMBRYO` modes. `GeneSwitch` enum extracted to standalone header to avoid AgentRef.h dependency in tests
- **Age-dependent susceptibility:** Computed dynamically from `getStage()` each tick, scales reward/punishment reinforcement effect
- **STW→LTW migration:** Runs per dendrite per tract per tick as continuous background process via `stw_to_ltw_rate`

### Phase 4: Genetics & Breeding
**Files modified:** `caosVM_genetics.cpp`, `Creature.cpp`
**Files created:** `GeneticAlgorithms.cpp`, `GeneticAlgorithms.h`

- **Crossover algorithm:** Operates on parsed gene-object vector (not raw bytes). ~1/50 probability per boundary, alternating parent selection, respects gene boundaries (no mid-gene splits)
- **Mutation:** Power-curve masking (`dP = pow(dRandom, dDegree)`), applied to gene data bytes only (never headers). Per-byte mutation chance scaled by gene `mutationweighting`
- **Cutting errors:** ~1/80 rate per gene — duplication (gene copied twice) and excision (gene skipped). Respects DUP/CUT/MUT header flags independently
- **GENE CROS/CLON/KILL CAOS commands:** Full implementations with moniker lineage tracking via `historyManager`
- **gene::clone()** added as pure virtual — enables gene duplication during crossover

### Phase 5: Core Faculties
**Files modified:** `Creature.cpp`, `c2eCreature.h`, `CreatureAI.cpp`

- **`updateLifeFaculty()`:** Aggregates organ `getShortTermLifeforce()`, sets `dead = 1.0f` when total depletes (after tick > 100 guard)
- **`updateMotorFaculty()`:** Computes `muscleenergy` from organ lifeforce average — energy feedback into biochemistry. Pre-existing gait chain (gaitloci → getGait → setGaitGene → gaitTick) left intact
- **`updateSensoryFaculty()`:** Smell lobe wired to room CA channels (`world.map->roomAt()` → `r->ca[i]` → `smellobe->setNeuronInput()`). Verb/noun lobes activated with zero defaults
- **Faculty tick order:** All three called from `c2eCreature::tick()` AFTER `tickBiochemistry()` per design decision D-17

### Phase 6: Full Faculties & CAOS
**Files modified:** `Creature.cpp`, `c2eCreature.h`, `CreatureAI.cpp`, `caosVM_creatures.cpp`, `c2eBrain.h`

- **`updateExpressiveFaculty()`:** Drive-to-expression argmax with `expressionWeights` table loaded from `creatureFacialExpressionGene` via `addGene()`. Sets `facialexpression` on SkeletalCreature
- **`updateReproductiveFaculty()`:** Ovulate thermostat (OVULATEOFF=0.314f, OVULATEON=0.627f) reading existing reproductive loci (fertile, pregnant, ovulate, receptive)
- **`updateLinguisticFaculty()`:** Hearing buffer (`pendingVerbStim`/`pendingNounStim`) populated by `handleStimulus()`, consumed by `tickBrain()` — replaces Phase 5 zero-default placeholders
- **`updateMusicFaculty()`:** No-op update per lc2e reference (MusicFaculty::Update() is empty in original)
- **14 CAOS commands:** EXPR (facial expression get/set), VOCB (force-learn vocabulary), INJR (organ injury), MATE (crossover-based fertilization with fertile/receptive checks), SPNL/KISS (social stimuli), BRN: SETN/SETD/SETL/SETT (brain value set), BRN: DMPB/DMPD/DMPL/DMPT (brain state dump via fmt::print)
- **HEAR/SMLL sensor variables:** Category-based agent lookup matching v_SEEN pattern
- **STIM SHOU/SIGN/TACT:** Upgraded from stubs to functional stimulus broadcasts
- **SAYN:** Minimal implementation triggering involuntary speech bubble script (script 71)

### Phase 7: Integration & Tooling
**Files modified:** `BrainViewer.cpp`, `CreatureGrapher.cpp`, `MainMenu.cpp`, `Openc2eImGui.cpp`, `CMakeLists.txt`
**Files created:** `GeneInspector.cpp`, `GeneInspector.h`

- **Gene Inspector:** New ImGui window — 6-column scrollable table (index, typeName, name, life stage, generation, flags M/D/C/R) reading creature genome via `getGenome()->genes`
- **BrainViewer enhancements:** Runtime `SliderInt` for neuron variable (0-7) and dendrite variable selection. Per-lobe RGB colors from gene data for boundary rectangles
- **CreatureGrapher improvements:** Current chemical value text readout, no-creature guard with early return
- **Lifecycle tests:** 16 tests across 6 suites (death guard, tick ordering, reinforcement learning, birth, HiDPI audit)

### Bug Fixes (outside phases)
- **Creature walking:** `SkeletalCreature::creatureTick()` — gaitTick only changed animation, not position. Added `x -= speed` / `x += speed` based on direction
- **C3 tool access:** `IsHatcheryEnabled()` and `IsAgentInjectorEnabled()` returned false for `engine.version == 3`. Changed to `>= 1`
- **GeneticsTest ASAN:** Stale object files caused vtable size mismatch after `gene::clone()` addition. Full rebuild resolved. `Mutation_PowerCurve` trial count increased 20→200 to eliminate flaky false negatives

## New Files

| File | Purpose |
|------|---------|
| `creatures/GeneSwitch.h` | Standalone enum for gene switch modes (avoids AgentRef.h in tests) |
| `creatures/GeneticAlgorithms.cpp` | Crossover, mutation, cutting error algorithms |
| `creatures/GeneticAlgorithms.h` | Header for genetic algorithm functions |
| `openc2eimgui/GeneInspector.cpp` | ImGui genome viewer window |
| `openc2eimgui/GeneInspector.h` | GeneInspector declarations |
| `tests/SVRuleTest.cpp` | SVRule opcode unit tests |
| `tests/NeuroEmitterTest.cpp` | NeuroEmitter bridge tests |
| `tests/InstinctTest.cpp` | Instinct replay and wipe() fix tests |
| `tests/LearningTest.cpp` | Reward/punishment and STW→LTW tests |
| `tests/AgingTest.cpp` | Gene switch mode and age transition tests |
| `tests/GeneticsTest.cpp` | Crossover, mutation, cutting error, GENE CROS/CLON tests |
| `tests/FacultyTest.cpp` | All 7 faculty tests + CAOS side-effect tests + HEAR/SMLL tests |
| `tests/GeneInspectorTest.cpp` | Gene header access and parsing tests |
| `tests/LifecycleTest.cpp` | Lifecycle integration tests (birth, tick order, death, learning) |

## Test Coverage

342 automated tests total (up from ~190 in upstream):
- SVRuleTest: Brain opcode verification
- NeuroEmitterTest: Brain↔biochemistry bridge
- InstinctTest: REM sleep instinct replay
- LearningTest: Reward/punishment reinforcement
- AgingTest: Gene switch modes and age transitions
- GeneticsTest: Crossover, mutation, cutting errors, GENE commands
- FacultyTest: All 7 faculties + CAOS command side-effects + sensors
- GeneInspectorTest: Genome parsing
- LifecycleTest: End-to-end lifecycle state transitions

## Architecture Decisions

- **Clean-room:** lc2e source used as algorithm reference only — all code is original
- **Method groups over classes:** Faculties implemented as `updateXFaculty()` methods on `c2eCreature`, not a Faculty class hierarchy
- **Test pattern:** Direct struct manipulation (TestableX mirrors) — avoids full Creature/World dependency chain in unit tests
- **Tick order:** tickBrain → tickNeuroEmitters → tickBiochemistry → updateSensoryFaculty → updateMotorFaculty → updateLifeFaculty → (other faculties) → lifestage checks → dead check
- **All CAOS commands** via `parsedocs.py` → `writecmds.py` codegen pipeline — no hand-written dispatch entries

## Running

```bash
cd openc2e-fork
mkdir -p build && cd build
cmake .. && cmake --build . -j4
./openc2e --data-path /path/to/c3-gamedata-clean/
```

Spawn a Norn via CAOS port:
```bash
printf 'new: simp 1 1 50 "blnk" 1 0 0\ngene load targ 1 "n*"\nnewc 4 targ 1 1 0\nmvto 1200 1000\nouts "Norn spawned"\n' | nc -w 8 localhost 20001
```

Run tests:
```bash
cd build && ctest --output-on-failure
```
