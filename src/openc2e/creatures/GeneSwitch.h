/*
 * GeneSwitch.h
 * openc2e
 *
 * Controls how a gene's switchontime is compared against the creature's
 * current life stage during processGenes(). Derived from lc2e Genome.h:160-175
 * (clean-room reimplementation — AGE-01 through AGE-03).
 */

#pragma once

//! Controls how a gene's switchontime is compared against the creature's life stage.
enum class GeneSwitch {
	Age = 0,    //!< Activates at the exact life stage matching switchontime (default)
	Always = 1, //!< Activates at every life stage regardless of switchontime
	Embryo = 2, //!< Activates only at baby (embryonic expression)
	UpToAge = 3 //!< Activates when switchontime <= current stage (cumulative)
};

/* vim: set noet: */
