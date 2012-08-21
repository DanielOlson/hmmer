#ifndef p7SPARSE_VITERBI_INCLUDED
#define p7SPARSE_VITERBI_INCLUDED

#include "p7_config.h"

#include "easel.h"

#include "hmmer.h"
#include "p7_sparsemx.h"

extern int p7_SparseViterbi(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_SPARSEMX *sx,  P7_TRACE *opt_tr, float *opt_sc);

#endif /*p7SPARSE_VITERBI_INCLUDED*/
/*****************************************************************
 * @LICENSE@
 * 
 * SVN $URL$
 * SVN $Id$
 *****************************************************************/
