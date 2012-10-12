#ifndef p7REFERENCE_VITERBI_INCLUDED
#define p7REFERENCE_VITERBI_INCLUDED
#include "p7_config.h"

#include "easel.h"

#include "hmmer.h"

extern int p7_ReferenceViterbi(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_REFMX *rmx, P7_TRACE *opt_tr, float *opt_sc);

#endif /*p7SPARSE_VITERBI_INCLUDED*/
/*****************************************************************
 * @LICENSE@
 * 
 * SVN $URL$
 * SVN $Id$
 *****************************************************************/
