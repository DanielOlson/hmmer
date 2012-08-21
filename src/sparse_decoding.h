#ifndef p7SPARSE_DECODING_INCLUDED
#define p7SPARSE_DECODING_INCLUDED
#include "p7_config.h"

#include "easel.h"

#include "hmmer.h"
#include "p7_sparsemx.h"

extern int p7_SparseDecoding(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, const P7_SPARSEMX *sxf, P7_SPARSEMX *sxb, P7_SPARSEMX *sxd);

#endif /*p7SPARSE_DECODING_INCLUDED*/
/*****************************************************************
 * @LICENSE@
 * 
 * SVN $URL$
 * SVN $Id$
 *****************************************************************/
