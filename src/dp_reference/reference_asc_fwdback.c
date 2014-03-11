/* Reference implementation of anchor set constrained (ASC) Forward,
 * Backward, and posterior decoding.
 * 
 * All reference implementation code is for development and
 * testing. It is not used in HMMER's main executables. Production
 * code uses sparse dynamic programming.
 * 
 * Contents:
 *    1. ASC Forward
 *    2. ASC Backward
 *    3. ASC Decoding
 *    x. Example
 *    x. Copyright and license information
 */


#include "p7_config.h"

#include "easel.h"

#include "base/p7_profile.h"
#include "base/p7_coords2.h"

#include "misc/logsum.h"

#include "dp_reference/p7_refmx.h"
#include "dp_reference/reference_asc_forward.h"

/*****************************************************************
 * 1. ASC Forward
 *****************************************************************/


/* Function:  p7_ReferenceASCForward()
 * Synopsis:  Calculate an anchor-set-constrained (ASC) Forward score.
 *
 * Purpose:   The anchor set constrained (ASC) Forward algorithm. Given
 *            digital sequence <dsq> of length <L>, profile <gm> to
 *            compare it to, and an anchor set <anch> for <D> domains;
 *            calculate ASC Forward score, and return it in
 *            <*opt_sc>.
 *            
 *            Caller provides two DP matrices <mxu> and <mxd>. They
 *            can be of any allocated size, and they will be
 *            reallocated if needed. Upon return, <mxu> and <mxd>
 *            contain the ASC Forward DP matrices for the UP and DOWN
 *            sectors of the calculation, respectively. In domain
 *            analysis, they will be needed later for posterior
 *            decoding.
 *
 *            Caller must have initialized at least once (per program
 *            invocation) with a <p7_FLogsumInit()> call, because this
 *            function uses <p7_FLogsum()>.
 *            
 *            The two coords in <anch>, <anch[].n1> and <anch[].n2>,
 *            are assigned to (i,k) pairs (in that order). The anchors
 *            in <anch> must be sorted in order of increasing sequence
 *            position <i>.
 *            
 *            <anch> and <D> might be data in a <P7_COORDS2> list
 *            management container: for example, for <P7_COORDS2 *dom>,
 *            you would pass <dom->arr> and <dom->n>.
 *
 * Args:      dsq    : digital target sequence 1..L
 *            L      : length of <dsq>
 *            gm     : profile
 *            anch   : array of (i,k) anchors defining <dsq>'s domain structure
 *            D      : number of anchors in <anch> array -- # of domains
 *            mxu    : UP matrix
 *            mxd    : DOWN matrix
 *            opt_sc : optRETURN - ASC Forward lod score, in nats
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    <eslEMEM> on reallocation failure.
 */
int
p7_ReferenceASCForward(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, const P7_COORD2 *anch, int D,
		       P7_REFMX *mxu, P7_REFMX *mxd, float *opt_sc)
{
  const float *tsc;		/* ptr for stepping thru profile's transition parameters */
  const float *rsc;		/* ptr for stepping thru profile's emission parameters   */
  float *dpp;                   /* ptr into main states ({MID}{LG}) for prev row...      */
  float *dpc;			/*   ... and a current row.                              */
  float *xp;			/* ptr into specials (ENJBLGC) for a previous row...     */
  float *xc;			/*   ... and a current row.                              */
  int   d;			/* counter over domains: 0..D-1                          */
  int   i;			/* counter over sequence positions (rows): 0,1..L        */
  int   k;			/* counter over model positions (columns): 0,1..M        */
  int   s;			/* counter over states                                   */
  float mlv, mgv;		/* tmp variables for ML, MG scores...                    */
  float dlv, dgv;		/*   ... and for DL, DG scores                           */
  float xE;			/* tmp var for accumulating E score for a row            */
  int   iend;			/* tmp var for row index for end of a DOWN sector        */
  int   M = gm->M;		/* for a bit more clarity, less dereference clutter      */
  int   status;

  /* contract checks / arg validation */
  ESL_DASSERT1( ( gm->L == L || gm->L == 0) ); /* length model in profile is either L (usually) or 0 (some unit tests) */

  /* reallocation, if needed */
  if ( (status = p7_refmx_GrowTo(mxu, gm->M, L)) != eslOK) return status;
  if ( (status = p7_refmx_GrowTo(mxd, gm->M, L)) != eslOK) return status;
  mxu->M    = mxd->M    = M;
  mxu->L    = mxd->L    = L;
  mxu->type = p7R_ASC_FWD_UP;
  mxd->type = p7R_ASC_FWD_DOWN;

  /* Initialize i=0..anch[0].i-1 specials. 
   * All specials are stored in DOWN matrix.
   * You can think of this as a DOWN matrix for
   * a boundary condition anchor at 0,M+1: 
   * i.e. i = 0..anch[0].n1-1, k=M+1..M (nothing).
   */
  iend = (D == 0) ? 1 : anch[0].n1;
  for (i = 0; i < iend; i++)
    {
      xc = mxd->dp[i] + (M+1) * p7R_NSCELLS; 
      xc[p7R_E]  = -eslINFINITY;
      xc[p7R_N]  = gm->xsc[p7P_N][p7P_LOOP] * i;
      xc[p7R_J]  = -eslINFINITY;
      xc[p7R_B]  = xc[p7R_N] + gm->xsc[p7P_N][p7P_MOVE];
      xc[p7R_L]  = xc[p7R_B] + gm->xsc[p7P_B][0]; 
      xc[p7R_G]  = xc[p7R_B] + gm->xsc[p7P_B][1]; 
      xc[p7R_C]  = -eslINFINITY;
      xc[p7R_JJ] = -eslINFINITY;
      xc[p7R_CC] = -eslINFINITY;
    }

  /* Iterate over domains d=0..D-1: */
  for (d = 0; d < D; d++)
    {
      /*****************************************************************
       * Part 1. UP matrix sector for domain d
       *    In an UP sector, we can enter the model, but not exit;
       *    so we make use of L,G state values from a previous row.
       *
       *    The UP sector includes:
       *       i = anch[d-1].i+1 to anch[d].i-1 (1..anch[d].i-1 for d==0)
       *       k = 1 to anch[d].k-1
       *
       *    We'll initialize row anch[d-1].i; then do the remaining rows.
       *    
       *    It's possible for the UP matrix to be empty (no cells), when
       *    anch[d].i == anch[d-1].i+1, including the case of anch[0].i = 1.
       *    It's also possible for the UP matrix to only consist of
       *    the initialization column k=0, when anch[d].k == 1.
       *****************************************************************/

      /* Initialization of previous row, anch[d-1].i (or, 0 for d=0) */
      i   = (d == 0 ? 0 : anch[d-1].n1);                                      // i is always our current row index, 0.1..L 
      dpc = mxu->dp[i];                                        
      for (s = 0; s < p7R_NSCELLS * anch[d].n2; s++) *dpc++ = -eslINFINITY;   // initialize previous row above UP matrix (0..anch[d].k-1) to -inf
      /* dpc is now sitting on (anch[d].i-1, anch[d].k): supercell above the anchor. 
       * We rely on that position, if UP matrix has no cells.
       */

      /* Now we recurse for remaining rows, down to anch[d].i-1 row.
       * (It's possible that no such rows exist, depending where that next anchor is.)
       */
      for (i = i+1 ; i < anch[d].n1; i++)
	{
	  rsc = gm->rsc[dsq[i]] + p7P_NR;                           // Start <rsc> at k=1 on row i 
	  tsc = gm->tsc;                                            // Start <tsc> at k=0, where off-by-one {LG}->M transition scores are
	  xp  = mxd->dp[i-1] + (M+1) * p7R_NSCELLS;                 // <xp> set to the specials of the previous row, i-1
	  dpp = mxu->dp[i-1];                                       // Start <dpp> on k=0, which we know has been init'ed to -inf above.
	  dpc = mxu->dp[i];                                         // Start <dpc> on k=0 of row i...
	  for (s = 0; s < p7R_NSCELLS; s++) *dpc++ = -eslINFINITY;  //   ... initialize that cell, and now <dpc> is on k=1.
	  dlv = dgv = -eslINFINITY;

	  for (k = 1; k < anch[d].n2; k++)
	    {
	      mlv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_ML) + *(tsc + p7P_MM),
							   *(dpp+p7R_IL) + *(tsc + p7P_IM)),
						p7_FLogsum(*(dpp+p7R_DL) + *(tsc + p7P_DM),
							      xp[p7R_L]  + *(tsc + p7P_LM)));
	      mgv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_MG) + *(tsc + p7P_MM),
							   *(dpp+p7R_IG) + *(tsc + p7P_IM)),
						p7_FLogsum(*(dpp+p7R_DG) + *(tsc + p7P_DM),
							      xp[p7R_G]  + *(tsc + p7P_GM)));

	      rsc++;              
	      tsc += p7P_NTRANS;  
	      dpp += p7R_NSCELLS;	

	      *dpc++ = *rsc + p7_FLogsum( *(dpp + p7R_ML) + *(tsc + p7P_MI), *(dpp + p7R_IL) + *(tsc + p7P_II));
	      *dpc++ = *rsc + p7_FLogsum( *(dpp + p7R_MG) + *(tsc + p7P_MI), *(dpp + p7R_IG) + *(tsc + p7P_II));
	      rsc++;	

	      *dpc++ = dlv;
	      *dpc++ = dgv;
	      dlv = p7_FLogsum( mlv + *(tsc + p7P_MD), dlv + *(tsc + p7P_DD));
	      dgv = p7_FLogsum( mgv + *(tsc + p7P_MD), dgv + *(tsc + p7P_DD));
	    }
	}

      /* The very last cell we calculated was the cell diagonal from
       * the anchor (i.e. where the DOWN matrix is going to start), and
       * we want that diagonal cell for initializing DOWN.
       * <dpc> just stepped past our desired cell; step back.
       * This works even if the UP matrix was empty.
       */
      dpp = dpc - p7R_NSCELLS;

      /*****************************************************************
       * Part 2. DOWN matrix sector for domain d
       *    In a DOWN sector, we can exit the model, but not enter,
       *    so we collect xE on each row,
       *    and use it to set the specials for that row.
       *    
       *    The DOWN sector includes:
       *       i = anch[d].i to anch[d+1].i-1  (anch[d].i to L, for last domain d=D-1)
       *       k = anch[d].k to M
       *       
       *    We'll initialize the top row with a partial DP calc, then
       *    do the remaining rows with the full calculation.  (With
       *    the UP matrix, we could initialize a prev row to -inf, but
       *    with the DOWN matrices, they are exactly abutting when we
       *    squeeze them down into two-matrix form.) 
       *    
       *    The top row starts with the anchor cell, which is
       *    initialized with one final UP calculation that allows
       *    entry exactly on the anchor.
       *****************************************************************/

      /* Start with anch[d].k-1 on first row, and set all cells to -inf*/
      i   = anch[d].n1;
      tsc = gm->tsc +         (anch[d].n2-1) * p7P_NTRANS;    // Start <tsc> on anch.k, i.e. k-1 relative to start of calculation
      rsc = gm->rsc[dsq[i]] + (anch[d].n2    * p7P_NR);       // <rsc> is on scores for anch.k
      xp  = mxd->dp[i-1] + (M+1) * p7R_NSCELLS;               // <xp> on specials for anch.i-1
      dpc = mxd->dp[i] + (anch[d].n2-1) * p7R_NSCELLS;
      for (s = 0; s < p7R_NSCELLS; s++) *dpc++ = -eslINFINITY;// <dpc> now sits on anch.k

      /* Then calculate the anchor cell (anch.i, anch.k) as an UP calc, using
       * <dpp> which was already set, above. 
       */
      mlv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_ML) + *(tsc + p7P_MM),
						   *(dpp+p7R_IL) + *(tsc + p7P_IM)),
					p7_FLogsum(*(dpp+p7R_DL) + *(tsc + p7P_DM),
						      xp[p7R_L]  + *(tsc + p7P_LM)));
      mgv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_MG) + *(tsc + p7P_MM),
						   *(dpp+p7R_IG) + *(tsc + p7P_IM)),
					p7_FLogsum(*(dpp+p7R_DG) + *(tsc + p7P_DM),
						      xp[p7R_G]  + *(tsc + p7P_GM)));

      tsc   += p7P_NTRANS;
      *dpc++ = -eslINFINITY;	/* IL */
      *dpc++ = -eslINFINITY;	/* IG */
      *dpc++ = -eslINFINITY;	/* DL */
      *dpc++ = -eslINFINITY;	/* DG */
      dlv = mlv + *(tsc + p7P_MD);
      dgv = mgv + *(tsc + p7P_MD);

      /* xE initialization counts exits from the anchor cell we calculated.
       * Unlike the rest of the top row, MG/ML exits from the anchor cell
       * need to be calculated. Also, it has to watch out for the
       * glocal exit case when the anchor cell (unusually) sits on M
       * itself.
       */
      xE  = (anch[d].n2 == M ? p7_FLogsum(mlv, mgv) : mlv);

      /* Initialization of the rest of the top row from k=anch.k+1 to M,
       * which is only reachable on deletion paths from the anchor.
       */
      for (k = anch[d].n2+1; k <= M; k++)
	{
	  *dpc++ = -eslINFINITY; // ML. No entry, and unreachable from other cells too. 
	  *dpc++ = -eslINFINITY; // MG. Ditto.
	  *dpc++ = -eslINFINITY; // IL. Not reachable on top row. 
	  *dpc++ = -eslINFINITY; // IG. Ditto.
	  *dpc++ = dlv;          // DL. Customary delayed store of prev calculation.
	  *dpc++ = dgv;          // DG. Ditto.

	  tsc   += p7P_NTRANS;
	  
	  xE  = (k == M ?                                  // Glocal exit included if k==M.
		 p7_FLogsum( xE, p7_FLogsum( dlv, dgv)) :  // We know all non-anchor-cell M's are -inf on top row, so 
		 p7_FLogsum( xE, dlv));			   //  we don't include M in these sums.
	  
	  dlv    = dlv + *(tsc + p7P_DD);
	  dgv    = dgv + *(tsc + p7P_DD);
	}

      /* dpc now sits on the start of the specials, in mxd */
      xc = dpc;
      xc[p7R_E]  = xE;
      xc[p7R_N]  = -eslINFINITY; 
      xc[p7R_J]  = (d == D-1 ? -eslINFINITY : xc[p7R_E] + gm->xsc[p7P_E][p7P_LOOP]);
      xc[p7R_B]  = xc[p7R_J] + gm->xsc[p7P_J][p7P_MOVE];
      xc[p7R_L]  = xc[p7R_B] + gm->xsc[p7P_B][0]; 
      xc[p7R_G]  = xc[p7R_B] + gm->xsc[p7P_B][1]; 
      xc[p7R_C]  = (d == D-1 ? xc[p7R_E] + gm->xsc[p7P_E][p7P_LOOP] : -eslINFINITY);
      xc[p7R_JJ] = -eslINFINITY;
      xc[p7R_CC] = -eslINFINITY;

      /* Now we can do the remaining rows in the Down sector of domain d. */
      iend = (d < D-1 ? anch[d+1].n1 : L+1);
      for (i = i+1 ; i < iend; i++)
	{
	  rsc = gm->rsc[dsq[i]] + anch[d].n2     * p7P_NR;         // Start <rsc> on (x_i, anchor_k, MAT) */
	  tsc = gm->tsc         + (anch[d].n2-1) * p7P_NTRANS;	   // Start <tsc> on (anchor_k-1), to pick up LMk,GMk entries 
	  dpp = mxd->dp[i-1]    + (anch[d].n2-1) * p7R_NSCELLS;	   // Start <dpp> on (i-1, anchor_k-1) 
	  dpc = mxd->dp[i]      + (anch[d].n2-1) * p7R_NSCELLS;	   // Start <dpc> on (i, anchor_k-1)... 
	  for (s = 0; s < p7R_NSCELLS; s++) *dpc++ = -eslINFINITY; //  ... and initialize the k-1 cells to -inf... 
                                                           	   //  ... so, now dpc is on anchor_k.
	  dlv = dgv = xE = -eslINFINITY;

  	  for (k = anch[d].n2; k <= M; k++) 
	    {				  
	      mlv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_ML) + *(tsc + p7P_MM),
							   *(dpp+p7R_IL) + *(tsc + p7P_IM)),
						           *(dpp+p7R_DL) + *(tsc + p7P_DM));
	      mgv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_MG) + *(tsc + p7P_MM),
							   *(dpp+p7R_IG) + *(tsc + p7P_IM)),
					 	           *(dpp+p7R_DG) + *(tsc + p7P_DM));

	      rsc++;                // rsc advances to insert score for position k 
	      tsc += p7P_NTRANS;    // tsc advances to transitions in states k     
	      dpp += p7R_NSCELLS;   // dpp advances to cells for states k          

	      *dpc++ = *rsc + p7_FLogsum( *(dpp + p7R_ML) + *(tsc + p7P_MI), *(dpp + p7R_IL) + *(tsc + p7P_II));
	      *dpc++ = *rsc + p7_FLogsum( *(dpp + p7R_MG) + *(tsc + p7P_MI), *(dpp + p7R_IG) + *(tsc + p7P_II));
	      rsc++;		    // rsc advances to next match state emission  

	      xE  = (k == M ?
		     p7_FLogsum( xE, p7_FLogsum( p7_FLogsum(mlv, dlv), p7_FLogsum(mgv, dgv))) : // k=M includes glocal exits  
		     p7_FLogsum( xE, p7_FLogsum(mlv, dlv)));                                    // k<M allows local exit only 

	      *dpc++ = dlv;                                                    // DL. Customary delayed store.
	      *dpc++ = dgv;                                                    //   ... ditto for DG store.
	      dlv = p7_FLogsum( mlv + *(tsc + p7P_MD), dlv + *(tsc + p7P_DD)); // Precalculation of DL for next k.
	      dgv = p7_FLogsum( mgv + *(tsc + p7P_MD), dgv + *(tsc + p7P_DD)); //   ... ditto for DG calculation.
	    }

	  /*****************************************************************
	   *  Having finished and stored the DOWN calculation on row i, with value xE,
           *  we can calculate and store the specials - also in the DOWN matrix.
           *    dpc[] is already on the special state storage.
	   *****************************************************************/

	  xc = dpc;
	  xp = dpp + p7R_NSCELLS;	
	  xc[p7R_E]  = xE;		
	  xc[p7R_N]  = -eslINFINITY; 
	  xc[p7R_J]  = (d == D-1 ? -eslINFINITY : p7_FLogsum( xp[p7R_J] + gm->xsc[p7P_J][p7P_LOOP], xc[p7R_E] + gm->xsc[p7P_E][p7P_LOOP]));
	  xc[p7R_B]  = xc[p7R_J] + gm->xsc[p7P_J][p7P_MOVE]; 
	  xc[p7R_L]  = xc[p7R_B] + gm->xsc[p7P_B][0]; 
	  xc[p7R_G]  = xc[p7R_B] + gm->xsc[p7P_B][1]; 
	  xc[p7R_C]  = (d == D-1 ? p7_FLogsum( xp[p7R_C] + gm->xsc[p7P_C][p7P_LOOP], xc[p7R_E] + gm->xsc[p7P_E][p7P_LOOP]) : -eslINFINITY);
	  xc[p7R_JJ] = -eslINFINITY;                                                                           
	  xc[p7R_CC] = -eslINFINITY;       
	} /* end loop over rows i of DOWN sector for domain d */

    } /* end loop over domains d=0..D-1; DP calculation complete. */

  /* As we leave the DP recursion, <xc> is still sitting on the
   * special states for the last row L... even for the edge case
   * of D==0 (and the edge case L=0 which must also have D==0).
   */
  if (opt_sc) *opt_sc = xc[p7R_C] + gm->xsc[p7P_C][p7P_MOVE]; /* C->T */
  return eslOK;
}
/*-------------------- end, ASC Forward -------------------------*/

/*****************************************************************
 * 2. ASC Backward
 *****************************************************************/

/* Function:  p7_ReferenceASCBackward()
 * Synopsis:  Calculate an anchor-set-constrained (ASC) Backward score.
 *
 * Purpose:   The anchor set constrained (ASC) Backward algorithm.
 *            Given digital sequence <dsq> of length <L>, profile <gm> to
 *            compare it to, and an anchor set <anch> for <D> domains;
 *            calculate ASC Backward score, and return it in
 *            <*opt_sc>.
 *            
 *            Caller provides two DP matrices <abu> and <abd>. They
 *            can be of any allocated size, and they will be
 *            reallocated if needed. Upon return, <abu> and <abd>
 *            contain the ASC Backward DP matrices for the UP and DOWN
 *            sectors of the calculation, respectively. In domain
 *            analysis, they will be needed later for posterior
 *            decoding.
 * 
 *            Caller must have initialized at least once (per program
 *            invocation) with a <p7_FLogsumInit()> call, because this
 *            function uses <p7_FLogsum()>.
 *            
 *            The two coords in <anch>, <anch[].n1> and <anch[].n2>,
 *            are assigned to (i,k) pairs (in that order). The anchors
 *            in <anch> must be sorted in order of increasing sequence
 *            position <i>.
 *            
 *            <anch> and <D> might be data in a <P7_COORDS2> list
 *            management container: for example, for <P7_COORDS2 *dom>,
 *            you would pass <dom->arr> and <dom->n>.
 *
 * Args:      dsq    : digital target sequence 1..L
 *            L      : length of <dsq>
 *            gm     : profile
 *            anch   : array of (i,k) anchors defining <dsq>'s domain structure
 *            D      : number of anchors in <anch> array -- # of domains
 *            abu    : UP matrix
 *            abd    : DOWN matrix
 *            opt_sc : optRETURN - ASC Backward lod score, in nats
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    <eslEMEM> on reallocation failure.
 */
int
p7_ReferenceASCBackward(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, const P7_COORD2 *anch, int D,
			P7_REFMX *abu, P7_REFMX *abd, float *opt_sc)
{
  const float *tsc;		/* ptr into transition scores of <gm> */
  const float *rsc;		/* ptr into emission scores of <gm> for residue dsq[i] on current row i  */
  const float *rsn;		/* ptr into emission scores of <gm> for residue dsq[i+1] on next row i+1 */
  float *dpc, *dpn;		/* ptrs into DP matrix for current row i, next row i+1  */
  float *xc;			/* ptr to specials on current row; specials are stored in DOWN, <abd> */
  int    d;                   	/* counter over domains 0..D-1 */
  int    i;			/* counter over sequence positions 0.1..L (DP rows) */
  int    k;			/* counter over model positions 0.1..M (DP columns) */
  int    iend;
  float  mgc, mlc;
  float  mgn, mln;
  float  dgn, dln;
  float  ign, iln;
  float  xE;
  float  xG,  xL;
  float  xC, xJ, xN;
  int    M = gm->M;
  int    status;

  /* contract checks / arg validation */
  ESL_DASSERT1( ( gm->L == L || gm->L == 0) ); /* length model in profile is either L (usually) or 0 (some unit tests) */

  /* reallocation, if needed */
  if ( (status = p7_refmx_GrowTo(abu, gm->M, L)) != eslOK) return status;
  if ( (status = p7_refmx_GrowTo(abd, gm->M, L)) != eslOK) return status;
  abu->M    = abd->M    = M;
  abu->L    = abd->L    = L;
  abu->type = p7R_ASC_BCK_UP;
  abd->type = p7R_ASC_BCK_DOWN;

  iend = (D == 0 ? 0 : anch[D-1].n1);
  for (i = L; i >= iend; i--)
    {
      xc = abd->dp[i] + (M+1) * p7R_NSCELLS;
      xc[p7R_CC] = -eslINFINITY;
      xc[p7R_JJ] = -eslINFINITY;
      xc[p7R_C]  = xC = (i == L ? gm->xsc[p7P_C][p7P_MOVE] : xC + gm->xsc[p7P_C][p7P_LOOP]);
      xc[p7R_G]  = -eslINFINITY;
      xc[p7R_L]  = -eslINFINITY;
      xc[p7R_B]  = -eslINFINITY;
      xc[p7R_J]  = -eslINFINITY;
      xc[p7R_N]  = -eslINFINITY;
      xc[p7R_E]  = xC + gm->xsc[p7P_E][p7P_MOVE];
    }

  /* The code below is designed to be easily convertible to one-row memory efficient DP, if needed */
  for (d = D-1; d >= 0; d--)
    {
      /* DOWN matrix.
       *   i = anch[d].i .. anch[d+1].i-1
       *   k = anch[d].k .. M
       *   calculated Backward. 
       * In the DOWN matrix, paths can end from the model, but not start in it,
       * so we evaluate {MD}->E transitions backward, but we don't evaluate 
       * B->{LG}->Mk
       */
      iend = (d == D-1 ? L : anch[d+1].n1-1);
      for (i = iend; i >= anch[d].n1; i--)
	{
	  rsn  = (i == iend ? NULL : gm->rsc[dsq[i+1]] + M * p7P_NR);   // residue scores on next row; start at M
	  tsc  = gm->tsc + M * p7P_NTRANS;                              // transition scores: start at M
	  dpc  = abd->dp[i]   + M * p7R_NSCELLS;                        // current row of DP matrix: start at M
	  xE   = dpc[p7R_NSCELLS+p7R_E];                                // pick up the xE score; specials start at M+1, hence the p7R_NSCELLS bump here
	  dpn  = (i == iend ? NULL : abd->dp[i+1] + M * p7R_NSCELLS);   // next row of DP matrix: start at M

	  mgn = dgn = -eslINFINITY;
	  mln = dln = -eslINFINITY;
	  ign = iln = -eslINFINITY;
	  xG  = xL  = -eslINFINITY;

	  for (k = M; k >= anch[d].n2; k--)
	    {
	      if (i != iend) {           // in one-row memory-efficient dp, dpc could be same as dpn, so:
		ign = dpn[p7R_IG];       // pick up I scores, before storing anything in these cells
		iln = dpn[p7R_IL];       // if insert scores were non-zero, we would add rsn[p7R_I] here
	      }

	      /* M calculations. Storage deferred for one-row reasons. */
	      mgc = (k == M ? xE :                                // at k=M, MG->E is possible, and it happens to be the only transition that's possible
		     p7_FLogsum( p7_FLogsum(mgn + tsc[p7P_MM],    // mgn (+ *rsn) was picked up in last k loop, so now it's i+1,k+1
					    ign + tsc[p7P_MI]),   // ign was just picked up, so it's i+1,k
        				    dgn + tsc[p7P_MD]));  // dgn is remembered from prev loop, so now it's i,k+1
	      mlc =  p7_FLogsum( p7_FLogsum(mln + tsc[p7P_MM],   
					    iln + tsc[p7P_MI]),
				 p7_FLogsum(dln + tsc[p7P_MD],
					    xE));
		     
	      dpc[p7R_DG] = dgn = (k == M ?  xE :  p7_FLogsum(mgn + tsc[p7P_DM], dgn + tsc[p7P_DD]));
	      dpc[p7R_DL] = dln =  p7_FLogsum( xE, p7_FLogsum(mln + tsc[p7P_DM], dln + tsc[p7P_DD]));
	      
	      dpc[p7R_IG] = p7_FLogsum(mgn + tsc[p7P_IM], ign + tsc[p7P_II]);
	      dpc[p7R_IL] = p7_FLogsum(mln + tsc[p7P_IM], iln + tsc[p7P_II]);
	      
	      if (i != iend) {              // pick up M[i+1][k] values, add residue emission to them;
		mgn =  dpn[p7R_MG] + *rsn;  // when we loop around, these become M[i+1][k+1] values we need for DP
		mln =  dpn[p7R_ML] + *rsn;
		rsn -= p7P_NR;
		dpn -= p7R_NSCELLS;
	      } 

	      dpc[p7R_MG] = mgc;           // now that we've picked up mgn/mln, safe to store MG,ML
	      dpc[p7R_ML] = mlc;
	      
	      tsc -= p7P_NTRANS;           
	      dpc -= p7R_NSCELLS;
	    }
	}
      /* mgc/mlc are the scores in the anchor cell anch[d].i,k */
      /* tsc is on anch[d].k-1 */
      rsc = gm->rsc[ dsq[anch[d].n1]] + anch[d].n2 * p7P_NR;
      mgn = mgc + *rsc;  
      mln = mlc + *rsc;
      xG  = mgn + tsc[p7P_GM];
      xL  = mln + tsc[p7P_LM];

      xJ = xN = -eslINFINITY;

      /* UP matrix */
      iend = (d == 0 ? 1 : anch[d-1].n1+1);
      for (i = anch[d].n1-1; i >= iend; i--)
	{
	  xc = abd->dp[i] + (M+1) * p7R_NSCELLS;   // on specials, which are in DOWN matrix
	  xc[p7R_CC] = -eslINFINITY;  // CC,JJ are only used in decoding matrices
	  xc[p7R_JJ] = -eslINFINITY;
	  xc[p7R_C]  = -eslINFINITY;  // C is now unreachable, when anchor set constrained.
	  xc[p7R_G]  = xG;            // xG was accumulated during prev row; G->Mk wing unfolded
	  xc[p7R_L]  = xL;            // xL accumulated on prev row
	  xc[p7R_B]  = p7_FLogsum(xG + gm->xsc[p7P_B][1],  xL + gm->xsc[p7P_B][0]); 
	  xc[p7R_J]  = xJ = (d == 0 ? -eslINFINITY : p7_FLogsum(xJ + gm->xsc[p7P_J][p7P_LOOP], xc[p7R_B] + gm->xsc[p7P_J][p7P_MOVE]));
	  xc[p7R_N]  = xN = (d  > 0 ? -eslINFINITY : p7_FLogsum(xN + gm->xsc[p7P_N][p7P_LOOP], xc[p7R_B] + gm->xsc[p7P_N][p7P_MOVE]));
	  xc[p7R_E]  = xc[p7R_J] + gm->xsc[p7P_E][p7P_LOOP];  

	  tsc = gm->tsc    + (anch[d].n2-1) * p7P_NTRANS;                                    // transition scores: start at anch[d].k-1
	  dpc = abu->dp[i] + (anch[d].n2-1) * p7R_NSCELLS;                                   // on anch[d].k-1
	  dpn = (i == anch[d].n1-1 ? NULL : abu->dp[i+1] + (anch[d].n2 - 1) * p7R_NSCELLS);  // on anch[d].k-1
	  rsc = gm->rsc[dsq[i]] + (anch[d].n2-1) * p7P_NR;
	  rsn = (i == anch[d].n1-1 ? NULL : gm->rsc[dsq[i+1]] + (anch[d].n2-1) * p7P_NR);

	  xG  = xL  = -eslINFINITY; 
	  dgn = dln = -eslINFINITY;
	  ign = iln = -eslINFINITY;
	  if (i < anch[d].n1-1) mgn = mln = -eslINFINITY; /* allow mgn/mln to carry over from anchor cell */

	  /* The recursion is the same as for the DOWN matrix, so only differences are commented on: */
	  for (k = anch[d].n2-1; k >= 1; k--)
	    {
	      if (i < anch[d].n1-1) {           
		ign = dpn[p7R_IG];       
		iln = dpn[p7R_IL];       
	      }

	      /* M calculations include no E contributions: can't do M->E in UP matrix */
	      mgc =  p7_FLogsum( p7_FLogsum(mgn + tsc[p7P_MM],    
					    ign + tsc[p7P_MI]),   
				            dgn + tsc[p7P_MD]);
	      mlc =  p7_FLogsum( p7_FLogsum(mln + tsc[p7P_MM],   
					    iln + tsc[p7P_MI]),
				            dln + tsc[p7P_MD]);
		     
	      xG = p7_FLogsum(xG, mgc + *rsc + tsc[p7P_GM - p7P_NTRANS]);
	      xL = p7_FLogsum(xL, mlc + *rsc + tsc[p7P_LM - p7P_NTRANS]);
	      rsc -= p7P_NR;

	      /* same for no D->E contributions in UP matrix */
	      dpc[p7R_DG] = dgn =  p7_FLogsum(mgn + tsc[p7P_DM], dgn + tsc[p7P_DD]);
	      dpc[p7R_DL] = dln =  p7_FLogsum(mln + tsc[p7P_DM], dln + tsc[p7P_DD]);
	      
	      dpc[p7R_IG] = p7_FLogsum(mgn + tsc[p7P_IM], ign + tsc[p7P_II]);
	      dpc[p7R_IL] = p7_FLogsum(mln + tsc[p7P_IM], iln + tsc[p7P_II]);
	      
	      if (i < anch[d].n1-1) {       
		mgn =  dpn[p7R_MG] + *rsn;  // when we loop around, these become M[i+1][k+1] values we need for DP
		mln =  dpn[p7R_ML] + *rsn;
		rsn -= p7P_NR;
		dpn -= p7R_NSCELLS;
	      } else mgn = mln = -eslINFINITY;

	      dpc[p7R_MG] = mgc;           // now that we've picked up mgn/mln, safe to store MG,ML
	      dpc[p7R_ML] = mlc;
	      
	      tsc -= p7P_NTRANS;           
	      dpc -= p7R_NSCELLS;
	    }


	} /* end backwards loop over i for UP matrix d */
      /* i is now on anch[d-1].i, or 0 */

      xc = abd->dp[i] + (M+1) * p7R_NSCELLS;   // on specials, which are in DOWN matrix
      xc[p7R_CC] = -eslINFINITY;  // CC,JJ are only used in decoding matrices
      xc[p7R_JJ] = -eslINFINITY;
      xc[p7R_C]  = -eslINFINITY;  // C is now unreachable, when anchor set constrained.
      xc[p7R_G]  = xG;            // xG was accumulated during prev row; G->Mk wing unfolded
      xc[p7R_L]  = xL;            // xL accumulated on prev row
      xc[p7R_B]  = p7_FLogsum(xG + gm->xsc[p7P_B][1],  xL + gm->xsc[p7P_B][0]); 
      xc[p7R_J]  = xJ = (d == 0 ? -eslINFINITY : p7_FLogsum(xJ + gm->xsc[p7P_J][p7P_LOOP], xc[p7R_B] + gm->xsc[p7P_J][p7P_MOVE]));
      xc[p7R_N]  = xN = (d  > 0 ? -eslINFINITY : p7_FLogsum(xN + gm->xsc[p7P_N][p7P_LOOP], xc[p7R_B] + gm->xsc[p7P_N][p7P_MOVE]));
      xc[p7R_E]  = xc[p7R_J] + gm->xsc[p7P_E][p7P_LOOP];  

    } /* end loop over domains d */

  if (opt_sc) *opt_sc = xN;
  return eslOK;
}

/*****************************************************************
 * 3. ASC Decoding
 *****************************************************************/

/* Function:  p7_ReferenceASCDecoding()
 * Synopsis:  Anchor-set-constrained (ASC) posterior decoding.
 *
 * Purpose:   The anchor set constrained (ASC) posterior decoding
 *            algorithm.  Given digital sequence <dsq> of length <L>,
 *            profile <gm> to compare it to, an anchor set <anch> for
 *            <D> domains, the already calculated ASC Forward UP and
 *            DOWN matrices <afu> and <afd> and ASC Backward UP and
 *            DOWN matrices <abu> and <abd>; do posterior decoding, to
 *            create the decoding UP and DOWN matrices <apu> and
 *            <apd>.
 *            
 *            The caller may provide empty matrices for <apu> and
 *            <apd>.  They can be of any allocated size, and they will
 *            be reallocated as needed.
 *            
 *            Alternatively, the caller can overwrite two of the
 *            matrices by passing <abu> for <apu>, and <afd> for
 *            <apd>. That is, the call would look like <(... afu, afd,
 *            abu, abd, abu, afd)>.  The reason that you can only
 *            overwrite those two matrices is that <afu> and <abd> are
 *            needed for endpoint definition by mass trace in a later
 *            step in domain postprocessing.
 *            
 *            Caller must have initialized at least once (per program                                                                             
 *            invocation) with a <p7_FLogsumInit()> call, because this                                                                            
 *            function uses <p7_FLogsum()>.                                                                                                       
 *
 *            The two coords in <anch>, <anch[].n1> and <anch[].n2>,                                                                              
 *            are assigned to (i,k) pairs (in that order). The anchors                                                                            
 *            in <anch> must be sorted in order of increasing sequence                                                                            
 *            position <i>.                                                                                                                       
 *
 *            <anch> and <D> might be data in a <P7_COORDS2> list                                                                                 
 *            management container: for example, for <P7_COORDS2 *dom>,                                                                           
 *            you would pass <dom->arr> and <dom->n>.                                                                                             
 *
 * Args:      dsq  : digital target sequence 1..L
 *            L    : length of <dsq>
 *            gm   : profile
 *            anch : array of (i,k) anchors defining <dsq>'s domain structure
 *            D    : number of anchors in <anch> array = # of domains in <dsq>
 *            afu  : ASC Forward UP matrix
 *            afd  : ASC Forward DOWN matrix
 *            abu  : ASC Backward UP matrix
 *            abd  : ASC Backward DOWN matrix
 *            apu  : RESULT : ASC Decoding UP matrix   (can be <abu>)
 *            apd  : RESULT : ASC Decoding DOWN matrix (can be <afd>)
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    <eslEMEM> on reallocation failure.
 */
int
p7_ReferenceASCDecoding(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, const P7_COORD2 *anch, int D, 
			const P7_REFMX *afu, P7_REFMX *afd, P7_REFMX *abu, const P7_REFMX *abd, P7_REFMX *apu, P7_REFMX *apd)
{
  const float *tsc = gm->tsc;	/* activates TSC() convenience macro, used in G->Mk wing unfolding */
  const float *rsc;		/* ptr to current row's residue emission scores in <gm>            */
  int          d, i, k, s;	/* indices for D domains, L residues, M nodes, and 6 states        */
  int          iend;		/* tmp var for start or end of a chunk in UP or DOWN matrices      */
  float        totsc;		/* overall Backward (=Forward) score, our normalization factor     */
  float        denom;		/* sum of pp for all emitting states for this residue, for renorm  */
  float       *fwdp;		/* ptr into row of Forward DP matrix, UP or DOWN (<afu> or <afd>)  */
  float       *bckp;		/* ptr into row of Backward DP matrix, UP or DOWN (<abu>, <abd>)   */
  float       *ppp;		/* ptr into row of Decoding DP matrix, <apu> or <apd>              */
  float        delta;		/* piece of probability alloted to Dj in G->D1..Dk-1->Mk wing      */
  float        xJ, xC;		/* used to keep J,C fwd scores from prev row, for decoding JJ/CC   */
  float        xG;		/* for clarity, tmp var, G(i-1) pulled out for wing unfolding      */
  const int    M = gm->M;	/* for clarity, pull out model's size                              */
  int          status;

  /* Contract checks / argument validation */
  ESL_DASSERT1( (afu->type == p7R_ASC_FWD_UP)   );
  ESL_DASSERT1( (afd->type == p7R_ASC_FWD_DOWN) );
  ESL_DASSERT1( (abu->type == p7R_ASC_BCK_UP)   );
  ESL_DASSERT1( (abd->type == p7R_ASC_BCK_DOWN) );
  ESL_DASSERT1( (afu->L == L && afd->L == L && abu->L == L && abd->L == L) );
  ESL_DASSERT1( (afu->M == M && afd->M == M && abu->M == M && abd->M == M) );
  
  /* Reallocation, if needed. 
   * Caller is allowed to overwrite abu -> apu, afd -> apd
   */
  if ( apu != abu && ( status = p7_refmx_GrowTo(apu, M, L)) != eslOK) return status;
  if ( apd != afd && ( status = p7_refmx_GrowTo(apd, M, L)) != eslOK) return status;
  apu->L = apd->L = L;
  apu->M = apd->M = M;
  apu->type = p7R_ASC_DECODE_UP;
  apd->type = p7R_ASC_DECODE_DOWN;


  /* Initialize specials on rows 1..anch[0].i-1 
   * We've above the first anchor, so only S->N->B->LG is possible in specials. 
   * We pick up totsc from row 0 of backwards.
   */
  for (i = 0; i < anch[0].n1; i++)
    {
      fwdp  = afd->dp[i] + (M+1) * p7R_NSCELLS;
      bckp  = abd->dp[i] + (M+1) * p7R_NSCELLS;
      ppp   = apd->dp[i] + (M+1) * p7R_NSCELLS;
      denom = 0.0;

      ppp[p7R_JJ] = 0.0;	
      ppp[p7R_CC] = 0.0; 
      ppp[p7R_E]  = 0.0; 
      ppp[p7R_N]  = (i == 0 ? 1.0 : expf(fwdp[p7R_N] + bckp[p7R_N] - totsc));
      ppp[p7R_J]  = 0.0;  
      ppp[p7R_B]  = expf(fwdp[p7R_B] + bckp[p7R_B] - totsc);
      ppp[p7R_L]  = expf(fwdp[p7R_L] + bckp[p7R_L] - totsc);
      ppp[p7R_G]  = expf(fwdp[p7R_G] + bckp[p7R_G] - totsc); 
      ppp[p7R_C]  = 0.0;

      if (i == 0) totsc = bckp[p7R_N]; 
      else        *(apu->dp[i]) = ppp[p7R_N];  // that's a hack. We stash denom (here, p7R_N) in k=0,ML slot of UP, which is unused
    }


  for (d = 0; d < D; d++)
    {
      /* UP matrix 
       */
      iend = (d = 0 ? 1 : anch[d-1].n1 + 1);
      for (i = iend; i < anch[d].n1; i++)
	{
	  /* Wing retraction of G->D1..Dk-1->MGk paths.
	   * 
	   * In Forward/Backward we used G->MGk directly, but in
           * decoding we need the pp's of the D's. Each Dj in the
           * PREVIOUS row gets an added correction <delta>, which is
           * the sum of all G->Mk paths that run through it, j<k.
           * This step is the only reason we need <rsc>, hence <dsq>,
           * in this function.
	   */
	  bckp  = abu->dp[i]      + (anch[d].n2-1) * p7R_NSCELLS;  // bckp on i, k0-1
	  ppp   = apu->dp[i-1]    + (anch[d].n2-1) * p7R_NSCELLS;  // ppp starts on i+1, k0-1 (PREVIOUS row)
	  rsc   = gm->rsc[dsq[i]] + (anch[d].n2-1) * p7P_NR;
    	  xG    = *(afd->dp[i-1] + (M+1) * p7R_NSCELLS + p7R_G);   // I don't see any good way of avoiding this reach out into memory
	  delta = 0.0;
    
	  for (k = anch[d].n2-1; k >= 1; k--)
	    {
	      ppp[p7R_DG] += delta;
	      delta       += expf(xG + TSC(p7P_GM, k-1) + *rsc + bckp[p7R_MG] - totsc);

	      ppp  -= p7R_NSCELLS;
	      bckp -= p7R_NSCELLS;
	      rsc  -= p7P_NR;
	    }

	  fwdp  = afu->dp[i] + p7R_NSCELLS;
	  bckp  = abu->dp[i] + p7R_NSCELLS;
	  ppp   = apu->dp[i];
	  denom = *ppp;		/* pick up what we stashed earlier; we're now going to finish row i and renormalize if needed */
	  for (s = 0; s < p7R_NSCELLS; s++) *ppp++ = 0.0;

	  /* Main decoding recursion:
	   * [ ML MG IL IG DL DG ] 
	   */
	  for (k = 1; k < anch[d].n2; k++)
	    {
	      ppp[p7R_ML] = expf(fwdp[p7R_ML] + bckp[p7R_ML] - totsc); denom += ppp[p7R_ML];
	      ppp[p7R_MG] = expf(fwdp[p7R_MG] + bckp[p7R_MG] - totsc); denom += ppp[p7R_MG];
	      ppp[p7R_IL] = expf(fwdp[p7R_IL] + bckp[p7R_IL] - totsc); denom += ppp[p7R_IL];
	      ppp[p7R_IG] = expf(fwdp[p7R_IG] + bckp[p7R_IG] - totsc); denom += ppp[p7R_IG];
	      ppp[p7R_DL] = expf(fwdp[p7R_DL] + bckp[p7R_DL] - totsc);
	      ppp[p7R_DG] = expf(fwdp[p7R_DG] + bckp[p7R_DG] - totsc);

	      fwdp += p7R_NSCELLS;
	      bckp += p7R_NSCELLS;
	      ppp  += p7R_NSCELLS;
	    }

#ifndef p7REFERENCE_ASC_DECODING_TESTDRIVE
	  denom = 1.0 / denom;		    // multiplication may be faster than division
	  ppp   = apu->dp[i] + p7R_NSCELLS;                                          // that's k=1 in UP
	  for (s = 0; s < (anch[d].n2-1) * p7R_NSCELLS; s++) *ppp++ *= denom;        // UP matrix row i renormalized
	  ppp   = apd->dp[i] + (anch[d].n2) * p7R_NSCELLS;                           // that's k0 in DOWN
	  for (s = 0; s < (M - anch[d].n2 + 1) * p7R_NSCELLS; s++) *ppp++ *= denom;  // DOWN matrix row i renormalized
	  for (s = 0; s < p7R_NXCELLS; s++) ppp[s] *= denom;
#endif
	} /* end loop over i's in UP chunk for domain d */



      /* DOWN matrix */
      xJ = xC = -eslINFINITY;
      iend = (d == D-1 ? L+1 : anch[d+1].n1);
      for (i = anch[d].n1; i < iend; i++)
	{
	  fwdp  = afd->dp[i] + anch[d].n2 * p7R_NSCELLS;
	  bckp  = abd->dp[i] + anch[d].n2 * p7R_NSCELLS;
	  ppp   = apd->dp[i] + anch[d].n2 * p7R_NSCELLS;
	  denom = 0.0;
	  
	  for (k = anch[d].n2; k <= M; k++)
	    {
	      ppp[p7R_ML] = expf(fwdp[p7R_ML] + bckp[p7R_ML] - totsc); denom += ppp[p7R_ML];
	      ppp[p7R_MG] = expf(fwdp[p7R_MG] + bckp[p7R_MG] - totsc); denom += ppp[p7R_MG];
	      ppp[p7R_IL] = expf(fwdp[p7R_IL] + bckp[p7R_IL] - totsc); denom += ppp[p7R_IL];
	      ppp[p7R_IG] = expf(fwdp[p7R_IG] + bckp[p7R_IG] - totsc); denom += ppp[p7R_IG];
	      ppp[p7R_DL] = expf(fwdp[p7R_DL] + bckp[p7R_DL] - totsc);
	      ppp[p7R_DG] = expf(fwdp[p7R_DG] + bckp[p7R_DG] - totsc);
	    }
	  /* fwdp, bckp, ppp now all sit at M+1, start of specials */

	  ppp[p7R_JJ] = (d == D-1 ? 0.0 : expf(xJ + gm->xsc[p7P_J][p7P_LOOP] + bckp[p7R_J] - totsc)); xJ = fwdp[p7R_J]; denom += ppp[p7R_JJ];
	  ppp[p7R_CC] = (d  < D-1 ? 0.0 : expf(xC + gm->xsc[p7P_C][p7P_LOOP] + bckp[p7R_C] - totsc)); xC = fwdp[p7R_C]; denom += ppp[p7R_CC];
	  ppp[p7R_E]  = expf(fwdp[p7R_E] + bckp[p7R_E] - totsc); 
	  ppp[p7R_N]  = 0.0;
	  ppp[p7R_J]  = (d == D-1 ? 0.0 : expf(fwdp[p7R_J] + bckp[p7R_J] - totsc));
	  ppp[p7R_B]  = expf(fwdp[p7R_B] + bckp[p7R_B] - totsc); 
	  ppp[p7R_L]  = expf(fwdp[p7R_L] + bckp[p7R_L] - totsc);
	  ppp[p7R_G]  = expf(fwdp[p7R_G] + bckp[p7R_G] - totsc);  
	  ppp[p7R_C]  = (d  < D-1 ? 0.0 : expf(fwdp[p7R_C] + bckp[p7R_C] - totsc));

	  
	  if (d < D-1) *(apu->dp[i]) = denom;  // hack: stash denom, which we'll pick up again when we do the next UP matrix.
#ifndef p7REFERENCE_ASC_DECODING_TESTDRIVE
	  if (d == D-1) 
	    { // UP matrices only go through anch[D-1].i-1, so for last DOWN chunk, we need to renormalize.
	      denom = 1.0 / denom;
	      ppp = apd->dp[i] + (anch[d].n2) * p7R_NSCELLS;                           // that's k0 in DOWN
	      for (s = 0; s < (M - anch[d].n2 + 1) * p7R_NSCELLS; s++) *ppp++ *= denom; 
	      for (s = 0; s < p7R_NXCELLS; s++) ppp[s] *= denom;
	    }
#endif
	} /* end loop over i's in DOWN chunk for domain d*/

    } /* end loop over domains d=0..D-1 */
  return eslOK;
}


/*****************************************************************
 * x. Example
 *****************************************************************/
#ifdef p7REFERENCE_ASC_FORWARD_EXAMPLE
#include "p7_config.h"

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_getopts.h"
#include "esl_sq.h"
#include "esl_sqio.h"

#include "hmmer.h"

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range  toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "show brief help on version and usage",             0 },
  { "-v",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "stop after doing the Viterbi anchors, no manual",  0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options] <hmmfile> <seqfile> <ndom> [<i0> <k0>]...";
static char banner[] = "example of ASC Forward reference implementation";

int 
main(int argc, char **argv)
{
  ESL_GETOPTS    *go      = p7_CreateDefaultApp(options, -1, argc, argv, banner, usage);
  char           *hmmfile = esl_opt_GetArg(go, 1);
  char           *seqfile = esl_opt_GetArg(go, 2);
  ESL_ALPHABET   *abc     = NULL;
  P7_HMMFILE     *hfp     = NULL;
  P7_HMM         *hmm     = NULL;
  P7_BG          *bg      = NULL;
  P7_PROFILE     *gm      = NULL;
  ESL_SQ         *sq      = NULL;
  ESL_SQFILE     *sqfp    = NULL;
  int             format  = eslSQFILE_UNKNOWN;
  P7_REFMX       *vit     = p7_refmx_Create(100, 100);
  P7_REFMX       *fwd     = p7_refmx_Create(100, 100);
  P7_REFMX       *bck     = p7_refmx_Create(100, 100);
  P7_REFMX       *pp      = p7_refmx_Create(100, 100);
  P7_REFMX       *mxu     = p7_refmx_Create(100, 100);
  P7_REFMX       *mxd     = p7_refmx_Create(100, 100);
  P7_TRACE       *tr      = p7_trace_Create();
  P7_COORDS2     *anchv   = p7_coords2_Create(0,0);
  P7_COORDS2     *anchm   = p7_coords2_Create(0,0);
  int             D;
  int             d,i;
  float           fsc, vsc, asc, asc_b;
  int             status;

  /* Read in one HMM */
  if (p7_hmmfile_OpenE(hmmfile, NULL, &hfp, NULL) != eslOK) p7_Fail("Failed to open HMM file %s", hmmfile);
  if (p7_hmmfile_Read(hfp, &abc, &hmm)            != eslOK) p7_Fail("Failed to read HMM");
  p7_hmmfile_Close(hfp);
 
  /* Open sequence file */
  sq     = esl_sq_CreateDigital(abc);
  status = esl_sqfile_Open(seqfile, format, NULL, &sqfp);
  if      (status == eslENOTFOUND) p7_Fail("No such file.");
  else if (status == eslEFORMAT)   p7_Fail("Format unrecognized.");
  else if (status == eslEINVAL)    p7_Fail("Can't autodetect stdin or .gz.");
  else if (status != eslOK)        p7_Fail("Open failed, code %d.", status);
 
  /* Get a sequence */
  status = esl_sqio_Read(sqfp, sq);
  if      (status == eslEFORMAT) p7_Fail("Parse failed (sequence file %s)\n%s\n", sqfp->filename, sqfp->get_error(sqfp));     
  else if (status != eslOK)      p7_Fail("Unexpected error %d reading sequence file %s", status, sqfp->filename);

  /* Configure a profile from the HMM */
  bg = p7_bg_Create(abc);
  gm = p7_profile_Create(hmm->M, abc);
  p7_profile_Config   (gm, hmm, bg);
  p7_bg_SetLength     (bg, sq->n);
  p7_profile_SetLength(gm, sq->n);

  /* Read anchor coords from command line */
  D = strtol( esl_opt_GetArg(go, 3), NULL, 10);
  p7_coords2_GrowTo(anchm, D);
  for (i = 4, d = 0; d < D; d++)
    {
      anchm->arr[d].n1 = strtol( esl_opt_GetArg(go, i), NULL, 10); i++;
      anchm->arr[d].n2 = strtol( esl_opt_GetArg(go, i), NULL, 10); i++;
    }
  anchm->n    = D;
  anchm->dim1 = sq->n;
  anchm->dim2 = gm->M;


  p7_ReferenceViterbi (sq->dsq, sq->n, gm, vit, tr, &vsc);
  p7_ReferenceForward (sq->dsq, sq->n, gm, fwd, &fsc);   
  p7_ReferenceBackward(sq->dsq, sq->n, gm, bck, NULL);   
  p7_ReferenceDecoding(sq->dsq, sq->n, gm, fwd, bck, pp);   

  p7_refmx_DumpBestDecoding(stdout, sq->dsq, sq->n, gm, pp);
  //p7_trace_Dump(stdout, tr);

  p7_ref_anchors_SetFromTrace(pp, tr, anchv);
  p7_ReferenceASCForward(sq->dsq, sq->n, gm, anchv->arr, anchv->n, mxu, mxd, &asc);

  //p7_refmx_Dump(stdout, mxu);
  //p7_refmx_Dump(stdout, mxd);

  p7_refmx_Reuse(mxu);
  p7_refmx_Reuse(mxd);
  p7_ReferenceASCBackward(sq->dsq, sq->n, gm, anchv->arr, anchv->n, mxu, mxd, &asc_b);

  p7_refmx_Dump(stdout, mxu);
  p7_refmx_Dump(stdout, mxd);

  printf("%-20s VIT   %6.2f %6.2f %6.2f %6.2f %8.4g ", sq->name, vsc, asc, asc_b, fsc, exp(asc-fsc));
  printf("%2d ", anchv->n);
  for (d = 0; d < anchv->n; d++) printf("%4d %4d ", anchv->arr[d].n1, anchv->arr[d].n2);
  printf("\n");



  if (! esl_opt_GetBoolean(go, "-v")) 
    {
      p7_refmx_Reuse(mxu);
      p7_refmx_Reuse(mxd);
      p7_ReferenceASCForward(sq->dsq, sq->n, gm, anchm->arr, anchm->n, mxu, mxd, &asc);

      printf("%-20s YOURS %6s %6.2f %6.2f %8.4g ", sq->name, "", asc, fsc, exp(asc-fsc));
      printf("%2d ", anchm->n);
      for (d = 0; d < anchm->n; d++) printf("%4d %4d ", anchm->arr[d].n1, anchm->arr[d].n2);
      printf("\n");
    }

  esl_sqfile_Close(sqfp);
  esl_sq_Destroy(sq);
  p7_coords2_Destroy(anchm);
  p7_coords2_Destroy(anchv);
  p7_trace_Destroy(tr);
  p7_refmx_Destroy(mxu);
  p7_refmx_Destroy(mxd);
  p7_refmx_Destroy(pp);
  p7_refmx_Destroy(bck);
  p7_refmx_Destroy(fwd);
  p7_refmx_Destroy(vit);
  p7_profile_Destroy(gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  esl_getopts_Destroy(go);
  return 0;
}
#endif /*p7REFERENCE_ASC_FWD_EXAMPLE*/




/*****************************************************************
 * @LICENSE@
 * 
 * SVN $Id$
 * SVN $URL$
 *****************************************************************/
