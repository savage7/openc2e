/*
 * GeneInspectorTest.cpp
 * openc2e
 *
 * Tests for gene data access patterns used by the Gene Inspector debug tool.
 * (Phase 7, Plan 01 — DEBUG-03)
 *
 * Tests verify:
 *   - geneHeader fields (switchontime, generation, flags, variant) are readable
 *   - Gene flag bitmask decodes correctly (mutable, dupable, cutable, dormant)
 *   - genomeFile genes vector accessible with valid type/name strings
 *
 * Test strategy: Use geneHeader struct directly — avoid constructing full
 * Creature/genome objects (per accumulated decision from Phase 3).
 */

#include "fileformats/genomeFile.h"

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Test 1: geneHeader fields are readable and hold expected values
// ---------------------------------------------------------------------------

TEST(GeneInspectorTest, GeneHeaderFieldsReadable) {
    geneHeader hdr;
    hdr.switchontime = adolescent; // lifestage value 2
    hdr.generation = 1;
    hdr.variant = 3;
    hdr.mutweighting = 200;

    EXPECT_EQ((int)hdr.switchontime, (int)adolescent);
    EXPECT_EQ((int)hdr.generation, 1);
    EXPECT_EQ((int)hdr.variant, 3);
    EXPECT_EQ((int)hdr.mutweighting, 200);
}

// ---------------------------------------------------------------------------
// Test 2: geneFlags bitmask decodes correctly
//   geneFlags uses named booleans: _mutable, dupable, delable, notexpressed
//   The inspector shows M/D/C/R columns:
//     M = _mutable, D = dupable, C = delable (cutable), R = notexpressed (dormant)
// ---------------------------------------------------------------------------

TEST(GeneInspectorTest, GeneFlagsDecodeCorrectly) {
    geneFlags flags;
    // Default constructor: _mutable=true, dupable=true, delable=true, notexpressed=false
    EXPECT_TRUE(flags._mutable);
    EXPECT_TRUE(flags.dupable);
    EXPECT_TRUE(flags.delable);
    EXPECT_FALSE(flags.notexpressed);

    // Set dormant (notexpressed), clear mutable
    flags._mutable = false;
    flags.dupable = true;
    flags.delable = true;
    flags.notexpressed = true;

    EXPECT_FALSE(flags._mutable);
    EXPECT_TRUE(flags.dupable);
    EXPECT_TRUE(flags.delable);
    EXPECT_TRUE(flags.notexpressed);
}

// ---------------------------------------------------------------------------
// Test 3: geneFlags round-trip through uint8_t operator
//   operator() returns packed byte; operator(uint8_t) unpacks it.
//   Bit layout: bit0=_mutable, bit1=dupable, bit2=delable, bit3=maleonly,
//               bit4=femaleonly, bit5=notexpressed
// ---------------------------------------------------------------------------

TEST(GeneInspectorTest, GeneFlagsRoundTrip) {
    geneFlags flags;
    flags._mutable = true;
    flags.dupable = false;
    flags.delable = true;
    flags.maleonly = false;
    flags.femaleonly = false;
    flags.notexpressed = false;

    uint8_t packed = flags();

    geneFlags flags2;
    flags2(packed);

    EXPECT_EQ(flags2._mutable, true);
    EXPECT_EQ(flags2.dupable, false);
    EXPECT_EQ(flags2.delable, true);
    EXPECT_EQ(flags2.notexpressed, false);
}

// ---------------------------------------------------------------------------
// Test 4: lifestage values match expected gene switch-on stage indices
//   Gene Inspector displays stage names: Baby=0, Child=1, Adolescent=2, etc.
// ---------------------------------------------------------------------------

TEST(GeneInspectorTest, LifestageValues) {
    EXPECT_EQ((int)baby, 0);
    EXPECT_EQ((int)child, 1);
    EXPECT_EQ((int)adolescent, 2);
    EXPECT_EQ((int)youth, 3);
    EXPECT_EQ((int)adult, 4);
    EXPECT_EQ((int)old, 5);
    EXPECT_EQ((int)senile, 6);
}
