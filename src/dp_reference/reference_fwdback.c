/* Reference implementation of Forward/Backward alignment, posterior
 * decoding, and gamma-centroid alignment, with a dual-mode glocal/local
 * model.
 *
 * This code is for testing. It is not used in HMMER3's main executables.
 *
 * Reference implementation of reference_fwdback/p7_refmx closely
 * mirrors banded implementation in banded_fwdback/p7_bandmx.  The
 * banded implementation is the production version, more complicated.
 *   
 * Contents:  
 *   1. Forward.
 *   2. Backward.
 *   3. Alignment (MEG, gamma-centroid)
 *   4. Benchmark driver.
 *   5. Unit tests.
 *   6. Test driver.
 *   7. Example.
 *   8. Copyright and license information.
 */

#include "p7_config.h"

#include "easel.h"
#include "esl_vectorops.h"

#include "base/p7_profile.h"
#include "base/p7_trace.h"

#include "misc/logsum.h"

#include "dp_reference/p7_refmx.h"
#include "dp_reference/reference_fwdback.h"
#include "dp_reference/reference_trace.h"

/*****************************************************************
 * 1. Forward 
 *****************************************************************/

/* Function:  p7_ReferenceForward()
 * Synopsis:  Reference implementation of the Forward algorithm.
 *
 * Purpose:   The Forward algorithm, comparing profile <gm> to target
 *            sequence <dsq> of length <L>. Caller provides an
 *            allocated <P7_REFMX> DP matrix <rmx>; this matrix will
 *            be reallocated if needed, so it can be reused from a
 *            previous calculation, including a smaller one.
 *            
 *            Caller also has initialized with a <p7_FLogsumInit()>
 *            call, because this function will use <p7_FLogsum()>.
 *            
 *            Upon successful return, the raw Forward score (in nats)
 *            is optionally returned in <*opt_sc>, and the DP matrix
 *            <rmx> is filled.
 *
 * Args:      dsq    : digital target sequence of length <L>
 *            L      : length of the target sequence
 *            gm     : query profile 
 *            rmx    : RESULT: DP matrix 
 *            opt_sc : optRETURN: raw Forward score in nats
 *
 * Returns:   <eslOK> on success. <rmx> contains the Forward matrix;
 *            its internals may have been reallocated.
 *            
 * Throws:    <eslEMEM> if reallocation is attempted and fails.           
 *
 * Notes:     This function makes assumptions about the order
 *            of the state indices in p7_refmx.h:
 *              main states: ML MG IL IG DL DG
 *              specials:    E N J B L G C JJ CC
 *              
 *            If L=0, the score is -infinity, by construction; HMMER
 *            profiles generate sequences of L>=1.
 */
int
p7_ReferenceForward(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_REFMX *rmx, float *opt_sc)
{
  float *dpc, *dpp;
  const float *tsc;		/* ptr for stepping thru profile's transition parameters */
  const float *rsc;		/* ptr for stepping thru profile's emission parameters   */
  int    M = gm->M;
  int    i, k, s;
  float  mlv, mgv;	      /* ML,MG cell values on current row   */
  float  dlv, dgv; 	      /* pushed-ahead DL,DG cell k+1 values */
  float  xE, xL, xG;
  int    status;		
  
  /* contract checks / arg validation */
  ESL_DASSERT1( ( gm->L == L || gm->L == 0) ); /* length model in profile is either L (usually) or 0 (some unit tests) */

  /* reallocation, if needed */
  if ( (status = p7_refmx_GrowTo(rmx, gm->M, L)) != eslOK) return status;
  rmx->M    = M;
  rmx->L    = L;
  rmx->type = p7R_FORWARD;

  /* Initialization of the zero row. */
  dpc = rmx->dp[0];
  for (s = 0; s < (M+1) * p7R_NSCELLS; s++) *dpc++ = -eslINFINITY; // all M,I,D; k=0..M
  dpc[p7R_E]      = -eslINFINITY;	                           
  dpc[p7R_N]      = 0.0;			                   
  dpc[p7R_J]      = -eslINFINITY;                                  
  dpc[p7R_B]      = gm->xsc[p7P_N][p7P_MOVE];                      
  dpc[p7R_L] = xL = gm->xsc[p7P_N][p7P_MOVE] + gm->xsc[p7P_B][0];  
  dpc[p7R_G] = xG = gm->xsc[p7P_N][p7P_MOVE] + gm->xsc[p7P_B][1];  
  dpc[p7R_C]      = -eslINFINITY;                                  
  dpc[p7R_JJ]     = -eslINFINITY;                             
  dpc[p7R_CC]     = -eslINFINITY;                             
  /* *dpc is on the specials' subrow; must be there to make L=0 case work, where loop below is skipped */

  /* Main DP recursion */
  for (i = 1; i <= L; i++)
    {
      /* Initialization for a new row */
      rsc = gm->rsc[dsq[i]] + p7P_NR;	/* this ptr steps through the row's emission scores 1..M. skip k=0 */
      tsc = gm->tsc;			/* this ptr steps through profile's transition scores 0..M         */

      dpp = rmx->dp[i-1];               /* previous row dpp is already set, and at k=0 */
      dpc = rmx->dp[i];                 /* current DP row, skip k=0, start at k=1.  */
      for (s = 0; s < p7R_NSCELLS; s++) *dpc++ = -eslINFINITY;

      dlv = dgv = -eslINFINITY;
      xE  =       -eslINFINITY;

      /* Main inner loop of the recursion */
      for (k = 1; k < M; k++)
	{
	  /* match states MLk, MGk */
	  mlv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_ML) + *(tsc + p7P_MM),
						       *(dpp+p7R_IL) + *(tsc + p7P_IM)),
					    p7_FLogsum(*(dpp+p7R_DL) + *(tsc + p7P_DM),
						       xL            + *(tsc + p7P_LM)));

	  mgv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_MG) + *(tsc + p7P_MM),
						       *(dpp+p7R_IG) + *(tsc + p7P_IM)),
 					    p7_FLogsum(*(dpp+p7R_DG) + *(tsc + p7P_DM),
						       xG            + *(tsc + p7P_GM)));

	  rsc++;                /* rsc advances to insert score for position k */
	  tsc += p7P_NTRANS;    /* tsc advances to transitions in states k     */
	  dpp += p7R_NSCELLS;	/* dpp advances to cells for states k          */

	  /* Insert state calculations ILk, IGk. */
	  *dpc++ = *rsc + p7_FLogsum( *(dpp + p7R_ML) + *(tsc + p7P_MI), *(dpp + p7R_IL) + *(tsc + p7P_II));
	  *dpc++ = *rsc + p7_FLogsum( *(dpp + p7R_MG) + *(tsc + p7P_MI), *(dpp + p7R_IG) + *(tsc + p7P_II));
	  rsc++;		/* rsc advances to next match state emission   */

	  /* E state update; local paths only, DLk->E, MLk->E; transition prob 1.0 in implicit probability model */
	  xE  = p7_FLogsum( p7_FLogsum(mlv, dlv), xE);

	  /* Delete state, deferred storage trick */
	  *dpc++ = dlv;
	  *dpc++ = dgv;
	  dlv = p7_FLogsum( mlv + *(tsc + p7P_MD), dlv + *(tsc + p7P_DD));
	  dgv = p7_FLogsum( mgv + *(tsc + p7P_MD), dgv + *(tsc + p7P_DD));
	}

      /* k=M node is unrolled and handled separately. No I state, and glocal exits. */
      mlv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_ML) + *(tsc + p7P_MM),
						   *(dpp+p7R_IL) + *(tsc + p7P_IM)),
					p7_FLogsum(*(dpp+p7R_DL) + *(tsc + p7P_DM),
						   xL            + *(tsc + p7P_LM)));

      mgv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7R_MG) + *(tsc + p7P_MM),
						   *(dpp+p7R_IG) + *(tsc + p7P_IM)),
					p7_FLogsum(*(dpp+p7R_DG) + *(tsc + p7P_DM),
						   xG            + *(tsc + p7P_GM)));
      dpp  += p7R_NSCELLS; 

      /* I_M state doesn't exist      */
      *dpc++ = -eslINFINITY;	/* IL */
      *dpc++ = -eslINFINITY;	/* IG */

      /* E state update now includes glocal exits: transition prob 1.0 from MG_m, DG_m */
      xE  = p7_FLogsum( xE, p7_FLogsum( p7_FLogsum(mlv, dlv), p7_FLogsum(mgv, dgv)));
      
      /* D_M state: deferred storage only */
      *dpc++ = dlv;
      *dpc++ = dgv;
    
      /* row i is now finished, and dpc[] is positioned exactly on first special state, E */
      dpp += p7R_NSCELLS;    /* now dpp[] is also positioned exactly on first special, E */
      
      dpc[p7R_E]      = xE;		
      dpc[p7R_N]      =             dpp[p7R_N] + gm->xsc[p7P_N][p7P_LOOP]; 
      dpc[p7R_J]      = p7_FLogsum( dpp[p7R_J] + gm->xsc[p7P_J][p7P_LOOP], dpc[p7R_E] + gm->xsc[p7P_E][p7P_LOOP]); 
      dpc[p7R_B]      = p7_FLogsum( dpc[p7R_N] + gm->xsc[p7P_N][p7P_MOVE], dpc[p7R_J] + gm->xsc[p7P_J][p7P_MOVE]); 
      dpc[p7R_L] = xL =             dpc[p7R_B] + gm->xsc[p7P_B][0]; 
      dpc[p7R_G] = xG =             dpc[p7R_B] + gm->xsc[p7P_B][1]; 
      dpc[p7R_C]      = p7_FLogsum( dpp[p7R_C] + gm->xsc[p7P_C][p7P_LOOP], dpc[p7R_E] + gm->xsc[p7P_E][p7P_MOVE]);
      dpc[p7R_JJ]     = -eslINFINITY;                                                                           
      dpc[p7R_CC]     = -eslINFINITY;                                                                           
    }
  /* Done with all rows i. As we leave, dpc is still sitting on the final special value for i=L ... including even the L=0 case */
  
  if (opt_sc) *opt_sc = dpc[p7R_C] + gm->xsc[p7P_C][p7P_MOVE]; /* C->T */
  return eslOK;
}
/*-----------  end, Forwards implementation ---------------------*/



/*****************************************************************
 * 2. Backward
 *****************************************************************/

/* Function:  p7_ReferenceBackward()
 * Synopsis:  Backward, dual-mode, quadratic memory, generic profile
 *
 * Purpose:   The Backward algorithm, comparing profile <gm> to target
 *            sequence <dsq> of length <L>. Caller provides an
 *            allocated <P7_REFMX> DP matrix <rmx>; this matrix will
 *            be reallocated if needed, so it can be reused from
 *            a previous calculation, including a smaller one.
 *            
 *            Caller also has initialized with a <p7_FLogsumInit()>
 *            call; this function will use <p7_FLogsum()>.
 *            
 *            Upon successful return, the raw Backward score (in nats)
 *            is optionally returned in <*opt_sc>, and the DP matrix
 *            <rmx> is filled in.
 *
 * Args:      dsq    : digital target sequence of length <L>
 *            L      : length of the target sequence
 *            gm     : query profile 
 *            rmx    : allocated DP matrix
 *            opt_sc : optRETURN: raw Backward score in nats
 *
 * Returns:   <eslOK> on success. <rmx> contains the Forward matrix;
 *            its internals may have been reallocated.
 *            
 * Throws:    <eslEMEM> if reallocation is attempted and fails.           
 *
 * Notes:     In <gm->rsc>, assumes p7P_NR = 2 and order [M I]
 *            In <gm->tsc>, does not make assumptions about p7P_NTRANS or order of values
 *            in <rmx->dp[i]>, assumes p7R_NSCELLS=6 in order [ ML MG IL IG DL DG]
 *                             assumes p7R_NXCELLS=7 in order [ E N J B L G C ]
 *                             
 *            Order of evaluation in the code is pretty carefully
 *            arranged to guarantee that dpc,dpn could be pointing
 *            into the same row of memory in a memory-efficient
 *            one-row DP implementation... even though this particular
 *            function, working with a <rmx>, knows it has rows
 *            <0,1..L>. This is so this code could be cribbed for a
 *            one-row implementation.
 */
int
p7_ReferenceBackward(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_REFMX *rmx, float *opt_sc)
{
  float *dpc;			/* ptr into current DP row, rmx->dp[i]                    */
  float *dpn;	            	/* ptr into next DP row, rmx->dp[i+1]                     */   // dpc, dpn could point to same row, in a single-row implementation.
  const float *rsc;		/* ptr to current row's residue score x_i vector in <gm>  */
  const float *rsn;		/* ptr to next row's residue score x_{i+1} vector in <gm> */
  const float *tsc;		/* ptr to model transition score vector gm->tsc[]         */
  float dgc, dlc;	        /* DG,DL tmp values on current row, [i,?,DG], [i,?,DL]    */
  float mgc, mlc;
  float mgn, mln;
  float ign, iln;
  float xE, xG, xL;	        /* temp vars for special state values             */
  int   i;			/* counter over sequence positions 1..L */
  int   k;			/* counter over model positions 1..M    */
  const int M  = gm->M;
  int   status;

 /* contract checks / arg validation */
  ESL_DASSERT1( ( gm->L == L || gm->L == 0) ); /* length model in profile is either L (usually) or 0 (some unit tests) */

  /* reallocation, if needed */
  if ( (status = p7_refmx_GrowTo(rmx, gm->M, L)) != eslOK) return status;
  rmx->M    = M;
  rmx->L    = L;
  rmx->type = p7R_BACKWARD;

  /* Initialize row L. */
  /* Specials are in order ENJBLGC,JJ,CC: step backwards thru them */
  dpc  = rmx->dp[L] + (M+1)*p7R_NSCELLS;
  rsc  = gm->rsc[dsq[L]] + M*p7P_NR;
  tsc  = gm->tsc + (M-1)*p7P_NTRANS;

  dpc[p7R_CC]      = -eslINFINITY;                 
  dpc[p7R_JJ]      = -eslINFINITY;                 
  dpc[p7R_C]       = gm->xsc[p7P_C][p7P_MOVE];   
  dpc[p7R_G]       = -eslINFINITY;                 
  dpc[p7R_L]       = -eslINFINITY;	           
  dpc[p7R_B]       = -eslINFINITY;	           
  dpc[p7R_J]       = -eslINFINITY;	           
  dpc[p7R_N]       = -eslINFINITY;	           
  dpc[p7R_E]  = xE = dpc[p7R_C] + gm->xsc[p7P_E][p7P_MOVE]; 
  dpc -= p7R_NSCELLS;	     /* moves dpc[] to supercell M */

  xG   = xE + *rsc + *(tsc + p7P_GM);
  xL   = xE + *rsc + *(tsc + p7P_LM);
  dpc[p7R_DG] = dgc = xE;		/* DG: D_M->E (transition prob 1.0)  */
  dpc[p7R_DL] = dlc = xE;		/* DL: ditto */
  dpc[p7R_IG]       = -eslINFINITY;	/* IG_M: no such state: always init'ed to -inf */
  dpc[p7R_IL]       = -eslINFINITY;	/* IL_M: no such state: always init'ed to -inf */
  dpc[p7R_MG]       = xE;		/* MG: M_M->E (transition prob 1.0)  */
  dpc[p7R_ML]       = xE;		/* ML: ditto */
  dpc -= p7R_NSCELLS;
  rsc -= p7P_NR;

  /* initialize main cells [k=1..M-1] on row i=L*/
  for (k = M-1; k >= 1; k--)
    {
      mgc =                 dgc + tsc[p7P_MD];
      mlc =  p7_FLogsum(xE, dlc + tsc[p7P_MD]);

      xG   = p7_FLogsum(xG, mgc + *rsc + *(tsc + p7P_GM - p7P_NTRANS)); /* off-by-one: tGMk stored as [k-1,GM] */
      xL   = p7_FLogsum(xL, mlc + *rsc + *(tsc + p7P_LM - p7P_NTRANS));
      rsc -= p7P_NR;

      dpc[p7R_DG] = dgc =                dgc + tsc[p7P_DD];  /* DG: only D->D path is possible */
      dpc[p7R_DL] = dlc = p7_FLogsum(xE, dlc + tsc[p7P_DD]); /* DL: Dk->Dk+1 or Dk->E */
      dpc[p7R_IG]       = -eslINFINITY;  	                    /* IG impossible w/o residues following it */
      dpc[p7R_IL]       = -eslINFINITY;	                    /* IL, ditto */
      dpc[p7R_MG]       = mgc;
      dpc[p7R_ML]       = mlc;
      tsc -= p7P_NTRANS;
      dpc -= p7R_NSCELLS;
    }
  /* k=0 cells are -inf */


  /* The main recursion over rows i=L-1 down to 1. (residues x_{L-1} down to x_1) */
  for (i = L-1; i >= 1; i--)
    {
                                                        /* ...xG,xL inherited from previous loop...               */
      rsn = gm->rsc[dsq[i+1]] + M * p7P_NR;        	/* residue x_{i+1} scores in *next* row:  start at end, M */
      rsc = gm->rsc[dsq[i]]   + M * p7P_NR;	        /* residue x_{i} scores in *current* row: start at end, M */
      dpc = rmx->dp[i]   + (M+1)*p7R_NSCELLS;   	/* dpc is on start of current row's specials              */
      dpn = rmx->dp[i+1] + (M+1)*p7R_NSCELLS;	        /* dpn is on start of next row's specials                 */
      tsc = gm->tsc + M * p7P_NTRANS;		        /* tsc is on t[M,0]: vector [MM IM DM LM GM MD DD MI II]  */

      /* Calculation of the special states. */
      /* dpc is on dp[i] special row, will now step backwards thru [E N J B L G C JJ CC] */
      dpc[p7R_CC]      = -eslINFINITY;			       /* CC unused */
      dpc[p7R_JJ]      = -eslINFINITY;			       /* JJ unused */
      dpc[p7R_C]       = dpn[p7R_C] + gm->xsc[p7P_C][p7P_LOOP]; /* C = C<-C */
      dpc[p7R_G]  = xG;     /* G was calculated during prev row (G->Mk wing unfolded) */
      dpc[p7R_L]  = xL;     /* L was calculated during prev row */
      dpc[p7R_B]       = p7_FLogsum(dpc[p7R_G] + gm->xsc[p7P_B][1],    /* B<-G */
				    dpc[p7R_L] + gm->xsc[p7P_B][0]);   /* B<-L */
      dpc[p7R_J]       = p7_FLogsum(dpn[p7R_J] + gm->xsc[p7P_J][p7P_LOOP],   /* J<-J */
				    dpc[p7R_B] + gm->xsc[p7P_J][p7P_MOVE]);  /* J<-B */
      dpc[p7R_N]       = p7_FLogsum(dpn[p7R_N] + gm->xsc[p7P_N][p7P_LOOP],   /* N<-N */
				    dpc[p7R_B] + gm->xsc[p7P_N][p7P_MOVE]);  /* N<-B */
      dpc[p7R_E]  = xE = p7_FLogsum(dpc[p7R_C] + gm->xsc[p7P_E][p7P_MOVE],
				    dpc[p7R_J] + gm->xsc[p7P_E][p7P_LOOP]);
      dpc -= p7R_NSCELLS;	/* dpc now on [i,M] supercell   */
      dpn -= p7R_NSCELLS;	/* dpn now on [i+1,M] supercell */
      

      /* Initialization of the k=M states */
      /* dpc on [i,k=M], init at k=M, step back thru [ ML MG IL IG DL DG] */
      /* dpn on [i+1,k=M] */
      mgn = *rsn + dpn[p7R_MG];	/* pick up MG(i+1,k=M) + s(x_i+1,k=M, M) */
      mln = *rsn + dpn[p7R_ML];	/* pick up ML(i+1,k=M) + s(x_i+1,k=M, M) */
      rsn--;			/* rsn now on s(x_i+1, k=M-1, I)         */

      xG     = xE + *rsc + *(tsc + p7P_GM - p7P_NTRANS); /* t[k-1][GM] is G->Mk wing-folded entry, recall off-by-one storage   */
      xL     = xE + *rsc + *(tsc + p7P_LM - p7P_NTRANS); /* t[k-1][LM] is L->Mk uniform local entry */
      rsc -= p7P_NR;		/* rsc now on s[x_{i},M-1,M] */
      tsc -= p7P_NTRANS;	/* tsc now on t[M-1,0]       */

      dpc[p7R_DG] = dgc = xE;		/* DGm->E */
      dpc[p7R_DL] = dlc = xE;		/* DLm->E */
      dpc[p7R_IG]       = -eslINFINITY;	/* IGm nonexistent */
      dpc[p7R_IL]       = -eslINFINITY;	/* ILm nonexistent */
      dpc[p7R_MG]       = xE;		/* MGm->E */
      dpc[p7R_ML]       = xE;		/* MLm->E */
      dpc -= p7R_NSCELLS;		/* dpc now on [i,M-1]   */
      dpn -= p7R_NSCELLS;		/* dpn now on [i+1,M-1] */

      /* The main recursion over model positions k=M-1 down to 1. */
      for (k = M-1; k >= 1; k--)
	{
                             	    /* rsn is on residue score [x_{i+1},k,I]    */
	  ign = dpn[p7R_IG];        /* pick up IG value from dp[i+1]            */ // if inserts had nonzero score: + *rsn 
	  iln = dpn[p7R_IL];	    /* pick up IL value                         */ // if inserts had nonzero score: + *rsn
	  rsn--;		    /* skip residue score for I (zero)          */ 

                                                                 /* tsc is on tsc[k,0] */
	  mgc =  p7_FLogsum( p7_FLogsum(mgn + *(tsc + p7P_MM),   /* mgn = [i+1,k+1,MG] */
					ign + *(tsc + p7P_MI)),  /* ign = [i+1,k,  IG] */
 			                dgc + *(tsc + p7P_MD));  /* dgc = [i,  k+1,DG] */

	  mlc =  p7_FLogsum( p7_FLogsum(mln + *(tsc + p7P_MM),   /* mln = [i+1,k+1,ML] */
					iln + *(tsc + p7P_MI)),  /* iln = [i+1,k,  IL] */
			     p7_FLogsum(dlc + *(tsc + p7P_MD),   /* dlc = [i,  k+1,DL] */
					xE));                    /* ML->E trans = 1.0  */

	  xG   = p7_FLogsum(xG, mgc + *rsc + *(tsc + p7P_GM - p7P_NTRANS)); /* t[k-1][GM] is G->Mk wing-retracted glocal entry */
	  xL   = p7_FLogsum(xL, mlc + *rsc + *(tsc + p7P_LM - p7P_NTRANS)); /* t[k-1][LM] is L->Mk uniform local entry         */
	  rsc -= p7P_NR;				       /* rsc now on s[x_i, k-1, M] */

	  /* dpc is on [i,k] and will now step backwards thru: [ ML MG IL IG DL DG ] */
	  dpc[p7R_DG] = dgc = p7_FLogsum( mgn + *(tsc + p7P_DM),   /* dgc picked up for next loop of k */
					  dgc + *(tsc + p7P_DD));
	  dpc[p7R_DL] = dlc = p7_FLogsum( p7_FLogsum( mln + *(tsc + p7P_DM),   /* dlc picked up for next loop of k */
						      dlc + *(tsc + p7P_DD)),
					  xE);

	  dpc[p7R_IG] = p7_FLogsum( mgn + *(tsc + p7P_IM),
				    ign + *(tsc + p7P_II));
	  dpc[p7R_IL] = p7_FLogsum( mln + *(tsc + p7P_IM),
				    iln + *(tsc + p7P_II));

	  mgn = *rsn + dpn[p7R_MG];	/* pick up M[i+1,k]; add score[x_i+1,k,M] */
	  mln = *rsn + dpn[p7R_ML];
	  rsn--;		/* rsn is now on score[i+1,k-1,I] */

	  dpc[p7R_MG] = mgc;		/* delayed store of [i,k,MG] value enables dpc,dpn to point into same single row */
	  dpc[p7R_ML] = mlc;

	  tsc -= p7P_NTRANS;
	  dpn -= p7R_NSCELLS;
	  dpc -= p7R_NSCELLS;

	  /* as we loop around now and decrement k:
           *   dpn is on [i+1,k-1] which becomes [i+1,k] 
           *   dpc is on [i,k-1]   which becomes [i,k] 
	   *   tsc is on tsc[k-1,0]   which becomes tsc[k,0]
	   *   rsn is on s[i+1,k-1,I] which becomes s[i+1,k,I]
	   *   rsc is on s[i,  k-1,M] which becomes s[i,k,M]
	   *   dgc is [i,k,DG],   which becomes [i,k+1,DG] value  (and analog. for dlc,DL)
	   *   mgn is [i+1,k,MG], which becomes [i+1,k+1,MG] value (and analog. for ML)
	   */
	} /* end of loop over model positions k */

      /* k=0 cells are -inf */

      /* xG,xL values are now ready for next row */
    } /* end of loop over rows i. */

  /* now on row i=0. Only N,B,G,L states are ultimately reachable on this initial
   * row. We calculate C,J,E anyway, to match sparse implementation exactly,
   * even though Decoding will see -inf for these cells in Fwd matrix, and decode
   * them to impossible. G,L values are already done. 
   */
  
  dpc = rmx->dp[0] + (M+1)*p7R_NSCELLS;	/* dpc is on start of row 0 specials  */
  dpn = rmx->dp[1] + (M+1)*p7R_NSCELLS; /* dpn is on start of row 1 specials  */

  dpc[p7R_CC] = -eslINFINITY;                                        
  dpc[p7R_JJ] = -eslINFINITY;                                        
  dpc[p7R_C]  = dpn[p7R_C] + gm->xsc[p7P_C][p7P_LOOP];
  dpc[p7R_G]  = xG;                                                  
  dpc[p7R_L]  = xL;                                                  
  dpc[p7R_B]  = p7_FLogsum( dpc[p7R_G] + gm->xsc[p7P_B][1],        dpc[p7R_L] + gm->xsc[p7P_B][0]);   
  dpc[p7R_J]  = p7_FLogsum( dpn[p7R_J] + gm->xsc[p7P_J][p7P_LOOP], dpc[p7R_B] + gm->xsc[p7P_J][p7P_MOVE]);  
  dpc[p7R_N]  = p7_FLogsum( dpn[p7R_N] + gm->xsc[p7P_N][p7P_LOOP], dpc[p7R_B] + gm->xsc[p7P_N][p7P_MOVE]); 
  dpc[p7R_E]  = p7_FLogsum( dpc[p7R_C] + gm->xsc[p7P_E][p7P_MOVE], dpc[p7R_J] + gm->xsc[p7P_E][p7P_LOOP]);

  /* for complete cleanliness: set all the main states on row 0 to -inf */
  dpc = rmx->dp[0] + p7R_NSCELLS;
  for (i = 0; i < M*p7R_NSCELLS; i++)
    *dpc++ = -eslINFINITY;

  if (opt_sc) *opt_sc = dpc[p7R_N];
  return eslOK;
}
/*-------------- end, backwards implementation ------------------*/



/*****************************************************************
 * 3. Alignment (maximum expected gain, gamma-centroid)
 *****************************************************************/


/* Function:  p7_ReferenceAlign()
 * Synopsis:  Reference implementation of gamma-centroid alignment
 *
 * Purpose:   Compute a gamma-centroid alignment of the query
 *            profile <gm> to a target sequence, given the computed posterior 
 *            decoding matrix <pp> for that query/target comparison,
 *            using DP matrix <rmx> for storage during the alignment
 *            computation. <rmx> will be reallocated if needed, so it
 *            can be reused from a previous computation, even a smaller one.
 *
 *            Return the alignment traceback in
 *            <tr>, storage provided and initialized by caller, and grown
 *            here as needed. Also optionally return the gain score,
 *            the total sum of pp - (1-(1+gamma)) for each state 
 *            in the state path.
 *            
 *            <gamma> is the parameter of gamma-centroid alignment.
 *            Higher <gamma> increases sensitivity; lower <gamma>
 *            increases specificity.  Given posterior probabilities
 *            pp(i,x) for every state x at every target position i in
 *            the DP matrix, the algorithm finds a state path
 *            consistent with the model that maximizes the sum of
 *            pp(i,x) - 1/(1+gamma).  Thus states with posterior
 *            probabilities < 1/(1+gamma) will be penalized, and those
 *            > 1/(1+gamma) are rewarded.
 *
 * Args:      gm       - query profile
 *            gamma    - gamma-centroid parameter
 *            pp       - posterior decoding matrix, previously calculated
 *            rmx      - RESULT: filled alignment DP matrix
 *            tr       - RESULT: alignment traceback
 *            opt_gain - optRETURN: gain score       
 *
 * Returns:   <eslOK> on success
 *
 * Xref:      [HamadaAsai11] for details of gamma-centroid estimators
 *            SRE:J9/137 for notes on extending Hamada/Asai gamma-centroid
 *                       from simple residue ij alignment to full state path
 *            [Kall05] for more on why the delta function on transitions is needed
 */
int
p7_ReferenceAlign(const P7_PROFILE *gm, float gamma, const P7_REFMX *pp, P7_REFMX *rmx, P7_TRACE *tr, float *opt_gain)
{
  float       *dpp;		     /* ptr into previous DP row in <rmx> */
  float       *dpc;		     /* ptr into current DP row in <rmx> */
  const float *tsc;		     /* ptr into transition scores in <gm> */
  const float *ppp;		     /* ptr into decoded posterior probs in <pp> */
  const int    L    = pp->L;
  const int    M    = pp->M;
  float        dlv, dgv;
  float        mlv, mgv;
  float        xE,xG,xL;
  int          i,k,s;
  float        gammaterm = -1.0f/(1.0f+gamma);
  int          status;

  /* Contract checks / argument validation */
  ESL_DASSERT1( (pp->type == p7R_DECODING) );
  ESL_DASSERT1( (pp->M    == gm->M) );

  /* DP matrix reallocation, if needed */
  if ( (status = p7_refmx_GrowTo(rmx, gm->M, L)) != eslOK) return status;
  rmx->M    = M;
  rmx->L    = L;
  rmx->type = p7R_ALIGNMENT;

  /* Initialization of the zero row. 
   * Main states    [ ML MG IL IG DL DG] 0..M; then special states [E N J B L G C] 
   * Gamma-centroid values can be negative, so "impossible" is -eslINFINITY.
   * P7_DELTAT() causes impossible transitions to result in impossible-scoring paths
   */
  dpc = rmx->dp[0];
  for (s = 0; s < (M+1) * p7R_NSCELLS; s++) *dpc++ = -eslINFINITY;   /* all M,I,D; k=0..M */
  ppp = pp->dp[0] + (M+1)*p7R_NSCELLS;
  dpc[p7R_E]       = -eslINFINITY;	      
  dpc[p7R_N]       = 2.0f + 2.*gammaterm;  /* S->N always have pp=1.0 at start of any trace, by construction. */
  dpc[p7R_J]       = -eslINFINITY;                     
  dpc[p7R_B]       = ppp[p7R_B] + gammaterm + P7_DELTAT( dpc[p7R_N], gm->xsc[p7P_N][p7P_LOOP]);
  dpc[p7R_L]  = xL = ppp[p7R_L] + gammaterm + P7_DELTAT( dpc[p7R_B], gm->xsc[p7P_B][0]);
  dpc[p7R_G]  = xG = ppp[p7R_G] + gammaterm + P7_DELTAT( dpc[p7R_B], gm->xsc[p7P_B][1]); 
  dpc[p7R_C]       = -eslINFINITY;		    
  dpc[p7R_JJ]      = -eslINFINITY;	/* JJ - unused in alignment, only used in decoding */
  dpc[p7R_CC]      = -eslINFINITY;	/* CC - ditto */

  /* Main DP recursion for rows 1..L */
  for (i = 1; i <= L; i++)
    {
      ppp = pp->dp[i] + p7R_NSCELLS;  /* positioned at pp[i,k=1,ML]     */
      tsc = gm->tsc;		      /* model on k=0 transitions (k-1 as we enter loop) */
      dpp = rmx->dp[i-1];	      /* prev row on k=0 (k-1 as we enter loop) */
      dpc = rmx->dp[i];		      /* dpc will be on k=1 as we enter the loop */
      for (s = 0; s < p7R_NSCELLS; s++) *dpc++ = -eslINFINITY;

      dlv = dgv = xE = -eslINFINITY;

      for (k = 1; k <= M; k++)
	{ /* main states [ ML MG IL IG DL DG] */ 	 
	  mlv = dpc[p7R_ML] = ppp[p7R_ML] + gammaterm + 
	    ESL_MAX( ESL_MAX( P7_DELTAT(dpp[p7R_ML], tsc[p7P_MM]),
			      P7_DELTAT(dpp[p7R_IL], tsc[p7P_IM])),
		     ESL_MAX( P7_DELTAT(dpp[p7R_DL], tsc[p7P_DM]),
			      P7_DELTAT( xL,         tsc[p7P_LM])));

	  mgv = dpc[p7R_MG] = ppp[p7R_MG] + gammaterm + 
	    ESL_MAX( ESL_MAX( P7_DELTAT(dpp[p7R_MG], tsc[p7P_MM]),
			      P7_DELTAT(dpp[p7R_IG], tsc[p7P_IM])),
		     ESL_MAX( P7_DELTAT(dpp[p7R_DG], tsc[p7P_DM]),
			      P7_DELTAT( xG,         tsc[p7P_GM])));

	  tsc += p7P_NTRANS;	/* transition scores now on gm->tsc[k] transitions */
	  dpp += p7R_NSCELLS;	/* prev row now on dp[i-1][k] cells       */

	  /* IL/IG calculation. */
	  dpc[p7R_IL] = ppp[p7R_IL] + gammaterm + 
	    ESL_MAX( P7_DELTAT(dpp[p7R_ML], tsc[p7P_MI]),
		     P7_DELTAT(dpp[p7R_IL], tsc[p7P_II]));

	  dpc[p7R_IG] = ppp[p7R_IG] + gammaterm + 
	    ESL_MAX( P7_DELTAT(dpp[p7R_MG], tsc[p7P_MI]),
		     P7_DELTAT(dpp[p7R_IG], tsc[p7P_II]));

	  /* E state update with ML->E and DL->E local exits k<M */
	  xE = ESL_MAX(xE, ESL_MAX(mlv, dlv));

	  /* Delete states: delayed storage trick */
	  dpc[p7R_DL] = dlv + ppp[p7R_DL] + gammaterm;
	  dpc[p7R_DG] = dgv + ppp[p7R_DG] + gammaterm;

	  dlv = ESL_MAX( P7_DELTAT( mlv, tsc[p7P_MD]), P7_DELTAT( dlv, tsc[p7P_DD]));
	  dgv = ESL_MAX( P7_DELTAT( mgv, tsc[p7P_MD]), P7_DELTAT( dgv, tsc[p7P_DD]));

	  dpc += p7R_NSCELLS;
	  ppp += p7R_NSCELLS;
	}
      /* IL/IG prohibited at k=M; boundary conditions on tsc make that so (txn into Im =-inf) */

      /* Special states [E N J B L G C] */
      /* row i main states finished; dpc, ppp are now on first special state, E */
      dpp += p7R_NSCELLS;	/* and now dpp is too */
      
      dpc[p7R_E]      = ppp[p7R_E] + gammaterm + ESL_MAX( xE, ESL_MAX(mgv, dgv));
      dpc[p7R_N]      = ppp[p7R_N] + gammaterm +          P7_DELTAT(dpp[p7R_N], gm->xsc[p7P_N][p7P_LOOP]);
      dpc[p7R_J]      = ppp[p7R_J] + gammaterm + ESL_MAX( P7_DELTAT(dpp[p7R_J], gm->xsc[p7P_J][p7P_LOOP]), P7_DELTAT(dpc[p7R_E], gm->xsc[p7P_E][p7P_LOOP]));
      dpc[p7R_B]      = ppp[p7R_B] + gammaterm + ESL_MAX( P7_DELTAT(dpc[p7R_N], gm->xsc[p7P_N][p7P_MOVE]), P7_DELTAT(dpc[p7R_J], gm->xsc[p7P_J][p7P_MOVE]));
      dpc[p7R_L] = xL = ppp[p7R_L] + gammaterm +          P7_DELTAT(dpc[p7R_B], gm->xsc[p7P_B][0]);
      dpc[p7R_G] = xG = ppp[p7R_G] + gammaterm +          P7_DELTAT(dpc[p7R_B], gm->xsc[p7P_B][1]);
      dpc[p7R_C]      = ppp[p7R_C] + gammaterm + ESL_MAX( P7_DELTAT(dpp[p7R_C], gm->xsc[p7P_C][p7P_LOOP]), P7_DELTAT(dpc[p7R_E], gm->xsc[p7P_E][p7P_MOVE]));
      dpc[p7R_JJ]     = -eslINFINITY;
      dpc[p7R_CC]     = -eslINFINITY;
    }

  rmx->type = p7R_ALIGNMENT;
  if (opt_gain) *opt_gain = dpc[p7R_C] + 1.0f + gammaterm; /* C->T; T has pp=1.0f */
  return p7_reference_trace_MGE(gm, rmx, tr);
}
/*--------------- end, gamma-centroid alignment  ---------------*/



/*****************************************************************
 * 4. Benchmark driver.
 *****************************************************************/
#ifdef p7REFERENCE_FWDBACK_BENCHMARK
#include "p7_config.h"

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_getopts.h"
#include "esl_random.h"
#include "esl_randomseq.h"
#include "esl_stopwatch.h"

#include "hmmer.h"

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "show brief help on version and usage",           0 },
  { "-s",        eslARG_INT,     "42", NULL, NULL,  NULL,  NULL, NULL, "set random number seed to <n>",                  0 },
  { "-L",        eslARG_INT,    "400", NULL, "n>0", NULL,  NULL, NULL, "length of random target seqs",                   0 },
  { "-N",        eslARG_INT,   "2000", NULL, "n>0", NULL,  NULL, NULL, "number of random target seqs",                   0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options] <hmmfile>";
static char banner[] = "benchmark driver for generic dual-mode Forward/Backward";

int 
main(int argc, char **argv)
{
  ESL_GETOPTS    *go      = p7_CreateDefaultApp(options, 1, argc, argv, banner, usage);
  char           *hmmfile = esl_opt_GetArg(go, 1);
  ESL_STOPWATCH  *w       = esl_stopwatch_Create();
  ESL_RANDOMNESS *r       = esl_randomness_CreateFast(esl_opt_GetInteger(go, "-s"));
  ESL_ALPHABET   *abc     = NULL;
  P7_HMMFILE     *hfp     = NULL;
  P7_HMM         *hmm     = NULL;
  P7_BG          *bg      = NULL;
  P7_PROFILE     *gm      = NULL;
  P7_REFMX       *rx      = NULL;
  P7_TRACE       *tr      = p7_trace_Create();
  int             L       = esl_opt_GetInteger(go, "-L");
  int             N       = esl_opt_GetInteger(go, "-N");
  ESL_DSQ        *dsq     = malloc(sizeof(ESL_DSQ) * (L+2));
  int             i;
  float           sc;
  double          base_time, F_time, B_time, V_time;
  double          F_speed, B_speed, V_speed;

  p7_Init();

  if (p7_hmmfile_OpenE(hmmfile, NULL, &hfp, NULL) != eslOK) p7_Fail("Failed to open HMM file %s", hmmfile);
  if (p7_hmmfile_Read(hfp, &abc, &hmm)            != eslOK) p7_Fail("Failed to read HMM");

  bg = p7_bg_Create(abc);
  p7_bg_SetLength(bg, L);

  gm = p7_profile_Create(hmm->M, abc);
  p7_profile_Config(gm, hmm, bg);
  p7_profile_SetLength(gm, L);

  rx = p7_refmx_Create(gm->M, L);

  /* Baseline time. */
  esl_stopwatch_Start(w);
  for (i = 0; i < N; i++) esl_rsq_xfIID(r, bg->f, abc->K, L, dsq);
  esl_stopwatch_Stop(w);
  base_time = w->user;

  /* Forward benchmark time. */
  esl_stopwatch_Start(w);
  for (i = 0; i < N; i++)
    {
      esl_rsq_xfIID(r, bg->f, abc->K, L, dsq);
      p7_ReferenceForward (dsq, L, gm, rx, &sc);
      p7_refmx_Reuse(rx);
    }
  esl_stopwatch_Stop(w);
  F_time  = w->user - base_time;
  F_speed = (double) N * (double) L * (double) gm->M * 1e-6 / F_time;

  /* Backward */
  esl_stopwatch_Start(w);
  for (i = 0; i < N; i++)
    {
      esl_rsq_xfIID(r, bg->f, abc->K, L, dsq);
      p7_ReferenceBackward (dsq, L, gm, rx, &sc);
      p7_refmx_Reuse(rx);
    }
  esl_stopwatch_Stop(w);
  B_time  = w->user - base_time;
  B_speed = (double) N * (double) L * (double) gm->M * 1e-6 / B_time;

  /* Viterbi */
  esl_stopwatch_Start(w);
  for (i = 0; i < N; i++)
    {
      esl_rsq_xfIID(r, bg->f, abc->K, L, dsq);
      p7_ReferenceViterbi (dsq, L, gm, rx, tr, &sc);
      p7_refmx_Reuse(rx);
      p7_trace_Reuse(tr);
    }
  esl_stopwatch_Stop(w);
  V_time  = w->user - base_time;
  V_speed = (double) N * (double) L * (double) gm->M * 1e-6 / V_time;

  printf("# %s (M= %d)\n", gm->name, gm->M);
  printf("# Reference Forward:  %8.1f Mc/s\n", F_speed);
  printf("# Reference Backward: %8.1f Mc/s\n", B_speed);
  printf("# Reference Viterbi:  %8.1f Mc/s\n", V_speed);

  free(dsq);
  p7_refmx_Destroy(rx);
  p7_trace_Destroy(tr);
  p7_profile_Destroy(gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  p7_hmmfile_Close(hfp);
  esl_alphabet_Destroy(abc);
  esl_stopwatch_Destroy(w);
  esl_randomness_Destroy(r);
  esl_getopts_Destroy(go);
  return 0;
}
#endif /*p7REFERENCE_FWDBACK_BENCHMARK*/
/*----------------- end, benchmark ------------------------------*/



/*****************************************************************
 * 5. Unit tests
 *****************************************************************/
#ifdef p7REFERENCE_FWDBACK_TESTDRIVE
#include "esl_dirichlet.h"
#include "esl_getopts.h"
#include "esl_random.h"
#include "esl_randomseq.h"

#include "base/p7_bg.h"

#include "build/modelsample.h"
#include "search/modelconfig.h"

#include "misc/emit.h"

#include "dp_reference/reference_viterbi.h"

/* The "randomseq" test compares randomly sampled profile to 
 * random sequences, and tests:
 * 
 * 1. Forward score is always >= Viterbi score.
 * 2. Forward score = Backwards score
 * 3. Forward, Backwards, Viterbi matrices pass Validate()
 *
 * Works for any mode, any random sequence length.
 *
 * We also generally should get a negative expected bit score on
 * random sequences, but to guarantee that, we'd have to sample
 * sequences from the null model (including their length), rather than
 * sampling fixed-length sequences.
 */
static void
utest_randomseq(ESL_RANDOMNESS *rng, int alphatype, int M, int L, int N)
{
  char          msg[] = "reference fwdback randomseq unit test failed";
  ESL_ALPHABET *abc   = esl_alphabet_Create(alphatype);
  ESL_DSQ      *dsq   = malloc(sizeof(ESL_DSQ) * (L+2));
  P7_BG        *bg    = p7_bg_Create(abc);
  P7_HMM       *hmm   = NULL;
  P7_PROFILE   *gm    = p7_profile_Create(M, abc);
  P7_REFMX     *vit   = p7_refmx_Create(M, L);
  P7_REFMX     *fwd   = p7_refmx_Create(M, L);
  P7_REFMX     *bck   = p7_refmx_Create(M, L);
  float         tol1  = ( p7_logsum_IsSlowExact() ? 0.0001 : 0.01 );
  float         vsc, fsc, bsc;
  int           idx;
  char          errbuf[eslERRBUFSIZE];
  
  if ( p7_hmm_Sample(rng, M, abc, &hmm) != eslOK) esl_fatal(msg);
  if ( p7_profile_Config(gm, hmm, bg)   != eslOK) esl_fatal(msg);
  if ( p7_profile_SetLength(gm, L)      != eslOK) esl_fatal(msg);

  for (idx = 0; idx < N; idx++)
    {
      if (esl_rsq_xfIID(rng, bg->f, gm->abc->K, L, dsq)      != eslOK) esl_fatal(msg);
      if (p7_ReferenceForward (dsq, L, gm, fwd,       &fsc)  != eslOK) esl_fatal(msg);
      if (p7_ReferenceBackward(dsq, L, gm, bck,       &bsc)  != eslOK) esl_fatal(msg);
      if (p7_ReferenceViterbi (dsq, L, gm, vit, NULL, &vsc)  != eslOK) esl_fatal(msg);

      /* matrices pass Validate() */
      if (p7_refmx_Validate(fwd, errbuf)                     != eslOK) esl_fatal("%s\n  %s", msg, errbuf);
      if (p7_refmx_Validate(bck, errbuf)                     != eslOK) esl_fatal("%s\n  %s", msg, errbuf);
      if (p7_refmx_Validate(vit, errbuf)                     != eslOK) esl_fatal("%s\n  %s", msg, errbuf);

      //printf("fsc = %.4f  bsc = %.4f  difference = %g\n", fsc, bsc, fabs(fsc-bsc));

      if (! isfinite(vsc))                          esl_fatal(msg);
      if (! isfinite(fsc))                          esl_fatal(msg);
      if (! isfinite(bsc))                          esl_fatal(msg);
      if (vsc > fsc)                                esl_fatal(msg); // fwd >= vit score
      if (esl_FCompareAbs(fsc, bsc, tol1) != eslOK) esl_fatal(msg); // fwd = bck score

      p7_refmx_Reuse(bck);
      p7_refmx_Reuse(fwd);
      p7_refmx_Reuse(vit);
    }
  free(dsq);
  p7_refmx_Destroy(bck);
  p7_refmx_Destroy(fwd);
  p7_refmx_Destroy(vit);
  p7_profile_Destroy(gm);
  p7_hmm_Destroy(hmm);
  p7_bg_Destroy(bg);
  esl_alphabet_Destroy(abc);
}

/* The "generation" test is like the randomseq test, except we sample
 * 'positive' sequences likely to have high scores, by emitting
 * sequences from the profile. We test:
 *   1. Forward score >= trace score of emitted sequence
 *   2. Positive expectation of bit scores.
 * and, like randomseq, we test:
 *   3. Forward score >= Viterbi score
 *   4. Forward score == Backward score
 *   5. All matrices Validate() (fwd, bck, vit)
 */  
static void
utest_generation(ESL_RANDOMNESS *rng, int alphatype, int M, int L, int N)
{
  char          msg[] = "reference fwdback generation unit test failed";
  ESL_ALPHABET *abc   = esl_alphabet_Create(alphatype);
  ESL_SQ       *sq    = esl_sq_CreateDigital(abc);
  P7_BG        *bg    = p7_bg_Create(abc);
  P7_HMM       *hmm   = NULL;
  P7_PROFILE   *gm    = p7_profile_Create(M, abc);
  P7_REFMX     *vit   = p7_refmx_Create(M, L);
  P7_REFMX     *fwd   = p7_refmx_Create(M, L);
  P7_REFMX     *bck   = p7_refmx_Create(M, L);
  P7_TRACE     *tr    = p7_trace_Create();
  float         avgsc = 0.0f;
  float         tol1  = ( p7_logsum_IsSlowExact() ? 0.0001 : 0.01 );
  float         tsc, vsc, fsc, bsc, nullsc;
  int           idx;
  char          errbuf[eslERRBUFSIZE];
  
  if ( p7_hmm_Sample(rng, M, abc, &hmm) != eslOK) esl_fatal(msg);
  if ( p7_profile_Config(gm, hmm, bg)   != eslOK) esl_fatal(msg);

  for (idx = 0; idx < N; idx++)
    {
      /* Emit sequence from model, using a length model of <L>;
       * restrict the emitted sequence length to 3x (L+M), arbitrarily, to 
       * keep it down to something reasonable.
       */
      if ( p7_profile_SetLength(gm, L)      != eslOK) esl_fatal(msg);
      do {
	esl_sq_Reuse(sq);
	if (p7_ProfileEmit(rng, hmm, gm, bg, sq, tr)             != eslOK) esl_fatal(msg);
      } while (sq->n > (gm->M+gm->L) * 3); 
      if (p7_trace_Validate(tr, gm->abc, sq->dsq, errbuf)        != eslOK) esl_fatal("%s:\n%s", msg, errbuf);
      if (p7_trace_Score   (tr, sq->dsq, gm, &tsc)               != eslOK) esl_fatal(msg);

      /* Reset the length model to the actual length sq->n, then 
       * score it with fwd, bck, vit 
       */
      if (p7_profile_SetLength(gm, sq->n)                            != eslOK) esl_fatal(msg);
      if (p7_ReferenceForward (sq->dsq, sq->n, gm, fwd,       &fsc)  != eslOK) esl_fatal(msg);
      if (p7_ReferenceBackward(sq->dsq, sq->n, gm, bck,       &bsc)  != eslOK) esl_fatal(msg);
      if (p7_ReferenceViterbi (sq->dsq, sq->n, gm, vit, NULL, &vsc)  != eslOK) esl_fatal(msg);

      /* matrices pass Validate() */
      if (p7_refmx_Validate(fwd, errbuf)                     != eslOK) esl_fatal("%s\n  %s", msg, errbuf);
      if (p7_refmx_Validate(bck, errbuf)                     != eslOK) esl_fatal("%s\n  %s", msg, errbuf);
      if (p7_refmx_Validate(vit, errbuf)                     != eslOK) esl_fatal("%s\n  %s", msg, errbuf);
      
      if (p7_bg_NullOne(bg, sq->dsq, sq->n, &nullsc)         != eslOK) esl_fatal(msg);
      avgsc += (fsc - nullsc) / eslCONST_LOG2;

      if (! isfinite(vsc))                          esl_fatal(msg);
      if (! isfinite(fsc))                          esl_fatal(msg);
      if (! isfinite(bsc))                          esl_fatal(msg);
      if (tsc > fsc)                                esl_fatal(msg); // fwd >= emitted trace score
      if (vsc > fsc)                                esl_fatal(msg); // fwd >= vit score
      if (esl_FCompareAbs(fsc, bsc, tol1) != eslOK) esl_fatal(msg); // fwd = bck score

      esl_sq_Reuse(sq);
      p7_trace_Reuse(tr);
      p7_refmx_Reuse(bck);
      p7_refmx_Reuse(fwd);
      p7_refmx_Reuse(vit);
    }

  avgsc /= N;
  if (avgsc < 0.0f) esl_fatal(msg); // positive expectation

  esl_sq_Destroy(sq);
  p7_trace_Destroy(tr);
  p7_refmx_Destroy(bck);
  p7_refmx_Destroy(fwd);
  p7_refmx_Destroy(vit);
  p7_profile_Destroy(gm);
  p7_hmm_Destroy(hmm);
  p7_bg_Destroy(bg);
  esl_alphabet_Destroy(abc);
}




/* The "duality" test uses the fact that for unihit models, the
 * dual-mode score should be equal to FLogsum(local_score +
 * glocal_score) - 1 bit.
 */
static void
utest_duality(ESL_RANDOMNESS *rng, int alphatype, int M, int L, int N)
{
  char          msg[]  = "generic_fwdback_dual : duality unit test failed";
  ESL_DSQ      *dsq    = NULL;
  ESL_ALPHABET *abc    = NULL;
  P7_HMM       *hmm    = NULL;
  P7_BG        *bg     = NULL;
  P7_PROFILE   *gmd    = NULL;
  P7_PROFILE   *gml    = NULL;
  P7_PROFILE   *gmg    = NULL;
  P7_REFMX      *rmx    = NULL;
  float         dual_sc, local_sc, glocal_sc, combined_sc;
  int           idx;

  if ((abc = esl_alphabet_Create(eslAMINO))   == NULL)  esl_fatal(msg);
  if ( p7_hmm_Sample(rng, M, abc, &hmm)       != eslOK) esl_fatal(msg);
  if ((bg = p7_bg_Create(abc))                == NULL)  esl_fatal(msg);

  if (( gmd = p7_profile_Create(hmm->M, abc) )   == NULL)  esl_fatal(msg);
  if (( gml = p7_profile_Create(hmm->M, abc) )   == NULL)  esl_fatal(msg);
  if (( gmg = p7_profile_Create(hmm->M, abc) )   == NULL)  esl_fatal(msg);

  if ( p7_profile_ConfigCustom(gmd, hmm, bg, L, 0.0, 0.5)   != eslOK) esl_fatal(msg); /* unihit, dual mode        */
  if ( p7_profile_ConfigCustom(gml, hmm, bg, L, 0.0, 0.0)   != eslOK) esl_fatal(msg); /* unihit, local-only mode  */
  if ( p7_profile_ConfigCustom(gmg, hmm, bg, L, 0.0, 1.0)   != eslOK) esl_fatal(msg); /* unihit, glocal-only mode */

  if (( dsq = malloc(sizeof(ESL_DSQ) * (L+2))) == NULL)  esl_fatal(msg);
  if (( rmx = p7_refmx_Create(hmm->M, L))      == NULL)  esl_fatal(msg);

  for (idx = 0; idx < N; idx++)
    {
      if ( esl_rsq_xfIID(rng, bg->f, abc->K, L, dsq) != eslOK) esl_fatal(msg);

      if ( p7_ReferenceForward(dsq, L, gmd, rmx, &dual_sc)   != eslOK) esl_fatal(msg);
      if ( p7_refmx_Reuse(rmx)                               != eslOK) esl_fatal(msg);

      if ( p7_ReferenceForward(dsq, L, gml, rmx, &local_sc)  != eslOK) esl_fatal(msg);
      if ( p7_refmx_Reuse(rmx)                               != eslOK) esl_fatal(msg);

      if ( p7_ReferenceForward(dsq, L, gmg, rmx, &glocal_sc) != eslOK) esl_fatal(msg);
      if ( p7_refmx_Reuse(rmx)                               != eslOK) esl_fatal(msg);

      combined_sc = p7_FLogsum(local_sc, glocal_sc) - eslCONST_LOG2;

      if (fabs(dual_sc-combined_sc) > 0.001)  esl_fatal(msg);
    }
  
  p7_refmx_Destroy(rmx);
  p7_profile_Destroy(gmg);
  p7_profile_Destroy(gml);
  p7_profile_Destroy(gmd);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  free(dsq);
}


/* The "enumeration" test samples a random enumerable HMM. This HMM
 * has tII transitions all zero, so the generated sequence space
 * ranges from L=0..2M+1 for the HMM. It uses this to create an
 * enumerable profile, by using a unihit L=0 configuration; this
 * profile generates all sequences of lengths L=1..2M-1. (The
 * differences from the HMM are 1) the I0 and Im states are normalized
 * away, and 2) the B->DDD->E mute path that generates zero residues
 * is also normalized away.)
 * 
 * The profile is configured in dual local/glocal mode, so that the
 * test will exercise all paths (except II transitions) in dual-mode
 * DP calculations.
 * 
 * Then the test enumerates all those sequences, scores them with
 * p7_ReferenceForward(), obtains P(seq | profile) from the score, and
 * sums P(seq | profile) over the enumerable space of profiles.  This
 * sum should be 1.0, within floating point tolerance.
 * 
 * All P(seq | profile) terms need to be >> DBL_EPSILON for the
 * summation to work. This means M must be very small -- perhaps on
 * the order of ~5. Small M also helps the enumeration run quickly.
 * Even a short M suffices to detect most conceivable failure modes in
 * a DP implementation.
 * 
 * To speed up the enumeration we use a tiny alphabet, <eslCOINS>.
 * Incidentally, this also helps us test this rarely-used Easel
 * alphabet, and whether HMMER can deal with non-bio alphabets.
 *
 * The enumeration test in generic_fwdback.c is similar, but uses
 * a different enumeration: p7_hmm_SampleEnumerable() instead of
 * p7_hmm_SampleEnumerable2(). p7_hmm_SampleEnumerable() sets all
 * transitions to insert to 0, so it enumerates a smaller seq space of
 * L=0..M (no inserts are possible at all.)
 */
static void
utest_enumeration(ESL_RANDOMNESS *rng)
{
  char          msg[] = "enumeration unit test failed";
  ESL_ALPHABET *abc   = esl_alphabet_Create(eslCOINS); // using the eslCOINS alphabet helps keep the enumeration's size down.
  P7_HMM       *hmm   = NULL;
  P7_BG        *bg    = NULL;
  ESL_DSQ      *dsq   = NULL;
  P7_PROFILE   *gm    = NULL;
  P7_REFMX     *rmx   = NULL;
  int           M     = 8;      // you can change this, but you need to keep it small, or the test will take a long time.
  int           maxL  = 2*M-1;	
  int           i, L;
  float         fsc;
  float         bg_ll;
  double        fp;
  double        total_p = 0.0;

  if ( p7_hmm_SampleEnumerable2(rng, M, abc, &hmm) != eslOK) esl_fatal(msg);
  if (( bg = p7_bg_Create(abc))                    == NULL)  esl_fatal(msg);
  if (( gm = p7_profile_Create(hmm->M, abc))       == NULL)  esl_fatal(msg);
  
                                         /* L, nj,  pglocal:  L=0 unihit dual-mode */
  if ( p7_profile_ConfigCustom(gm, hmm, bg, 0, 0.0, 0.5) != eslOK) esl_fatal(msg);

  if (( dsq = malloc(sizeof(ESL_DSQ) * (maxL+3))) == NULL)  esl_fatal(msg); /* 1..2*M-1, +2 for sentinels at 0, 2*M, +1 for the maxL+1 test seq */
  if (( rmx = p7_refmx_Create(hmm->M, maxL+1))     == NULL)  esl_fatal(msg); /* +1 because of the maxL+1 final test */

  /* By design, the profile neglects a small amount of probability
   * mass for the L=0 case (a mute path through G->D1...Dm->E) because
   * it removes the mute path using wing retraction. To test that the
   * total sum is 1.0, we have to add that small mass back. 
   */
  if (p7_profile_GetMutePathLogProb(gm, &total_p) != eslOK) esl_fatal(msg);
  total_p = exp(total_p) * 0.5;	/* 0.5 matches <pglocal> above */
  //printf("mute path probability = %g\n", total_p);

  /* L=0 included just to test that an L=0 sequence does indeed get a score of -inf, as it should */
  for (L = 0; L <= maxL; L++)
    {
      /* initialize dsq[1..L] at "0000..." */
      dsq[0] = dsq[L+1] = eslDSQ_SENTINEL;
      for (i = 1; i <= L; i++) dsq[i] = 0;

      /* enumerate and score all sequences of length L */
      while (1)	
	{
	  if ( p7_ReferenceForward(dsq, L, gm, rmx, &fsc) != eslOK) esl_fatal(msg);
	  
	  /* calculate bg log likelihood component of the scores */
	  for (bg_ll = 0., i = 1; i <= L; i++)  bg_ll += log(bg->f[dsq[i]]);
	  
	  /* convert to probability P(seq|model), adding the bg LL back to the LLR */
	  fp =  exp(fsc + bg_ll);
	  total_p += fp;

	  /* Increment dsq to next seq, like a reversed odometer; works for any alphabet */
	  for (i = 1; i <= L; i++) 
	    if (dsq[i] < abc->K-1) { dsq[i]++; break; } else { dsq[i] = 0; }
	  if (i > L) break;	/* we're done enumerating sequences */

	  p7_refmx_Reuse(rmx);
	}
    }

  /* That sum is subject to significant numerical error because of
   * discretization error in FLogsum(); don't expect it to be too close.
   */
  //printf("enumeration test: total_p = %g\n", total_p);
  if (total_p < 0.999 || total_p > 1.001) esl_fatal(msg);

  /* And any sequence of length L > maxL should get score -infinity. */
  if ( esl_rsq_xfIID(rng, bg->f, abc->K, maxL+1, dsq) != eslOK) esl_fatal(msg);
  if ( p7_ReferenceForward(dsq, maxL+1, gm, rmx, &fsc)    != eslOK) esl_fatal(msg);
  if ( fsc != -eslINFINITY) esl_fatal(msg);                                    

  p7_refmx_Destroy(rmx);
  p7_profile_Destroy(gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  free(dsq);
}

/* The "singlepath" test creates a special profile with only a single
 * possible parse (including its emitted residues): it samples an HMM
 * whose transitions and emissions can only be 1.0 or 0.0, and it
 * creates a uniglocal L=0 profile from it. Then it samples that 
 * path (and sequence), and runs Forward, Backward, and Viterbi on it.
 * It tests:
 *    1. Generated trace is the same as the Viterbi trace.
 *    2. Generated trace score is the same as Viterbi score.
 *    3. Forward score = Backward score.
 *    4. Forward score = score of generated trace
 */
static void
utest_singlepath(ESL_RANDOMNESS *rng, int alphatype, int M)
{
  char          msg[] = "reference fwdback singlepath unit test failed";
  ESL_ALPHABET *abc   = esl_alphabet_Create(alphatype);
  P7_BG        *bg    = p7_bg_Create(abc);
  P7_HMM       *hmm   = NULL;
  P7_PROFILE   *gm    = p7_profile_Create(M, abc);
  ESL_SQ       *sq    = esl_sq_CreateDigital(abc);
  P7_TRACE     *gtr   = p7_trace_Create();           /* generated trace */
  P7_TRACE     *vtr   = p7_trace_Create();	     /* Viterbi trace   */
  P7_REFMX     *fwd   = NULL;
  P7_REFMX     *bck   = NULL;
  P7_REFMX     *vit   = NULL;
  float         tsc, vsc, fsc, bsc;
  float         tol   = 1e-4;

  /* Create a profile that has only a single possible path (including
   * emissions) thru it; requires configuring in uniglocal mode w/ L=0
   */
  if ( p7_hmm_SampleSinglePathed(rng, M, abc, &hmm) != eslOK) esl_fatal(msg);
  if ( p7_profile_ConfigUniglocal(gm, hmm, bg, 0)   != eslOK) esl_fatal(msg);

  /* Sample that sequence and path */
  if ( p7_ProfileEmit(rng, hmm, gm, bg, sq, gtr)    != eslOK) esl_fatal(msg);
  
  /* Allocate matrices */
  if ( (fwd = p7_refmx_Create(M, sq->n))            == NULL)  esl_fatal(msg);
  if ( (bck = p7_refmx_Create(M, sq->n))            == NULL)  esl_fatal(msg);
  if ( (vit = p7_refmx_Create(M, sq->n))            == NULL)  esl_fatal(msg);

  /* Run DP routines, collect scores that should all match  */
  if (p7_trace_Score(gtr, sq->dsq, gm, &tsc)                    != eslOK) esl_fatal(msg);
  if (p7_ReferenceForward (sq->dsq, sq->n, gm, fwd,      &fsc)  != eslOK) esl_fatal(msg);
  if (p7_ReferenceBackward(sq->dsq, sq->n, gm, bck,      &bsc)  != eslOK) esl_fatal(msg);
  if (p7_ReferenceViterbi (sq->dsq, sq->n, gm, vit, vtr, &vsc)  != eslOK) esl_fatal(msg);  

  if (esl_FCompareAbs(tsc, vsc, tol)   != eslOK) esl_fatal(msg);
  if (esl_FCompareAbs(fsc, bsc, tol)   != eslOK) esl_fatal(msg);
  if (esl_FCompareAbs(fsc, tsc, tol)   != eslOK) esl_fatal(msg);
  if (p7_trace_Compare(gtr, vtr, 0.0f) != eslOK) esl_fatal(msg); // 0.0 is <pptol> arg, unused, because neither trace has PP annotation

  p7_refmx_Destroy(fwd);
  p7_refmx_Destroy(bck);
  p7_refmx_Destroy(vit);
  esl_sq_Destroy(sq);
  p7_trace_Destroy(gtr);
  p7_trace_Destroy(vtr);
  p7_profile_Destroy(gm);
  p7_hmm_Destroy(hmm);
  p7_bg_Destroy(bg);
  esl_alphabet_Destroy(abc);
}


/*
 * The "brute" test creates a handcrafted HMM and profile from 20 free
 * parameters, enumerates all possible paths for some sequences of
 * length 1..4, and calculates viterbi and forward/backward scores by
 * brute force maximization and summation. It compares these to the
 * reference viterbi, forward, and backward scores.
 * 
 * (The following section of code is long, with several supporting
 * functions and a structure.)
 */
/* See state diagrams SRE:J9/101 for more information */
struct p7_brute_utest_s {
  double a;      	/* hmm->t[0][p7H_MM] */
  double b;      	/* hmm->t[1][p7H_MM] */
  double c;      	/* hmm->t[2][p7H_MM] */
  double d;      	/* hmm->t[3][p7H_MM] */
  double e;      	/* hmm->t[0][p7H_MI] */
  double f;      	/* hmm->t[1][p7H_MI] */
  double g;      	/* hmm->t[2][p7H_MI] */
  double h;      	/* hmm->t[0][p7H_IM] */
  double i;      	/* hmm->t[1][p7H_IM] */
  double j;      	/* hmm->t[2][p7H_IM] */
  double k;      	/* hmm->t[3][p7H_IM] */
  double l;      	/* hmm->t[1][p7H_DD] */
  double m;      	/* hmm->t[2][p7H_DD] */

  double n;             /* N->B   exp(gm->xsc[p7P_N][p7P_MOVE]) */
  double p;             /* E->C   exp(gm->xsc[p7P_E][p7P_MOVE]) */
  double q;             /* C->T   exp(gm->xsc[p7P_C][p7P_MOVE]) */
  double r;             /* J->B   exp(gm->xsc[p7P_J][p7P_MOVE]) */  
  double s;		/* B->G   exp(gm->xsc[p7P_B][1]         */

  double alpha;  	/* hmm->mat[k][x=A] emission for all match states */
  double beta;  	/* hmm->ins[k][x=A] emission for all insert states */

  double LMk[4];	/* L->Mk entries, constructed from transitions when brute profile is configured. */
};

static void
set_LMk(struct p7_brute_utest_s *prm)
{
  double Z = 0.0;
  int    k;

  /* Recapitulate the occ[k] calculation */
  prm->LMk[0] = 0.0;
  prm->LMk[1] = prm->a + prm->e; /* G->M1 */
  prm->LMk[2] = prm->LMk[1] * (prm->b + prm->f) + (1.-prm->LMk[1]) * (1.-prm->l);
  prm->LMk[3] = prm->LMk[2] * (prm->c + prm->g) + (1.-prm->LMk[2]) * (1.-prm->m);

  /* Z weights occ[k] by how many times k can start a substring.
   * If all occ[k]=1, Z would be M(M+1)/2, and entry dist would be 2/(M(M+1)) for all k
   */
  for (k = 1; k <= 3; k++)
    Z += prm->LMk[k] * (4 - k);	/* e.g. M-k+1 */
  if (Z > 0.0) esl_vec_DScale(prm->LMk, 4, 1.0/Z); /* I suppose Z could be 0, if all probability went into the mute cycle */
}

/* set_bruteparams()
 * create a brute model with known (not sampled) parameters;
 * most useful for debugging, since we can accumulate knowledge
 * of expected path probabilities.
 */
static void
set_bruteparams(struct p7_brute_utest_s *prm)
{
  prm->a = 0.8;       /* hmm->t[0][p7H_MM] */
  prm->b = 0.7;       /* hmm->t[1][p7H_MM] */
  prm->c = 0.1;       /* hmm->t[2][p7H_MM] */
  prm->d = 0.6;       /* hmm->t[3][p7H_MM] */
  prm->e = 0.05;      /* hmm->t[0][p7H_MI] */
  prm->f = 0.2;       /* hmm->t[1][p7H_MI] */
  prm->g = 0.88;      /* hmm->t[2][p7H_MI] */
  prm->h = 0.90;      /* hmm->t[0][p7H_IM] */
  prm->i = 0.92;      /* hmm->t[1][p7H_IM] */
  prm->j = 0.94;      /* hmm->t[2][p7H_IM] */
  prm->k = 0.96;      /* hmm->t[3][p7H_IM] */
  prm->l = 0.57;      /* hmm->t[1][p7H_DD] */
  prm->m = 0.59;      /* hmm->t[2][p7H_DD] */

#if 0
  /* Setting n,p,q,r to 1.0 makes the core model account
   * for the entire target seq: <= 19 paths are possible,
   * SNB->core->ECT.
   */
  prm->n = 1.0;      /* N->B   exp(gm->xsc[p7P_N][p7P_MOVE]) */
  prm->p = 1.0;      /* E->C   exp(gm->xsc[p7P_E][p7P_MOVE]) */
  prm->q = 1.0;      /* C->T   exp(gm->xsc[p7P_C][p7P_MOVE]) */
  prm->r = 1.0;      /* J->B   exp(gm->xsc[p7P_J][p7P_MOVE]) */
#endif

  prm->n = 0.41;      /* N->B   exp(gm->xsc[p7P_N][p7P_MOVE]) */
  prm->p = 0.43;      /* E->C   exp(gm->xsc[p7P_E][p7P_MOVE]) */
  prm->q = 0.45;      /* C->T   exp(gm->xsc[p7P_C][p7P_MOVE]) */
  prm->r = 0.47;      /* J->B   exp(gm->xsc[p7P_J][p7P_MOVE]) */
  prm->s = 0.50;      /* B->G   exp(gm->xsc[p7P_B][1]         */

  prm->alpha = 0.7;    /* hmm->mat[k][A] for all k  */
  prm->beta  = 0.25;   /* hmm->ins[k][A] for all k [MUST be 0.25, equal to background; H3 assumes insert score 0; xref J5/118 */

  set_LMk(prm);
  return;
}

static void
sample_zeropeppered_probvector(ESL_RANDOMNESS *r, double *p, int n)
{
  esl_dirichlet_DSampleUniform(r, n, p);
  if (esl_rnd_Roll(r, 2))	/* 50% of the time, we throw a zero into the sampled p's */
    {
      p[esl_rnd_Roll(r, n)] = 0.0;
      esl_vec_DNorm(p, n);
    }
}

static void
sample_bruteparams(ESL_RANDOMNESS *r, struct p7_brute_utest_s *prm)
{
  double tmp[3];

  /* make sure we can get M->M w/ nonzero prob, but pepper zeros elsewhere */
  do { sample_zeropeppered_probvector(r, tmp, 3);  prm->a = tmp[0]; prm->e = tmp[1]; } while (prm->a == 0.0);
  do { sample_zeropeppered_probvector(r, tmp, 3);  prm->b = tmp[0]; prm->f = tmp[1]; } while (prm->b == 0.0);
  do { sample_zeropeppered_probvector(r, tmp, 3);  prm->c = tmp[0]; prm->g = tmp[1]; } while (prm->c == 0.0);
  do { sample_zeropeppered_probvector(r, tmp, 2);  prm->d = tmp[0]; }                  while (prm->d == 0.0);

  /* pepper any D, I transition. [3][II] cannot be 1.0 (k param)*/
  sample_zeropeppered_probvector(r, tmp, 2);  prm->h = tmp[0];
  sample_zeropeppered_probvector(r, tmp, 2);  prm->i = tmp[0];
  sample_zeropeppered_probvector(r, tmp, 2);  prm->j = tmp[0];
  do { sample_zeropeppered_probvector(r, tmp, 2);  prm->k = tmp[0]; } while (prm->k == 1.0);
  sample_zeropeppered_probvector(r, tmp, 2);  prm->l = tmp[0];
  sample_zeropeppered_probvector(r, tmp, 2);  prm->m = tmp[0];

  /* make sure N,E,C move probs are nonzero, pepper otherwise */
  do { sample_zeropeppered_probvector(r, tmp, 2);  prm->n = tmp[0]; } while (prm->n == 0.0);
  do { sample_zeropeppered_probvector(r, tmp, 2);  prm->p = tmp[0]; } while (prm->p == 0.0);
  do { sample_zeropeppered_probvector(r, tmp, 2);  prm->q = tmp[0]; } while (prm->q == 0.0);

  /* J can be peppered */
  sample_zeropeppered_probvector(r, tmp, 2);  prm->r = tmp[0];

  /* B->{GL} can be peppered */
  sample_zeropeppered_probvector(r, tmp, 2);  prm->s = tmp[0];

  /* make sure x=A emissions for match, insert are nonzero */
  prm->alpha = esl_rnd_UniformPositive(r);
  prm->beta  = 0.25;		/* MUST match background; H3 assumes isc=0; xref J5/118 */
  set_LMk(prm);
  return;
}


static void
create_brute_models(struct p7_brute_utest_s *prm, ESL_ALPHABET *abc, P7_BG *bg, P7_HMM **ret_hmm, P7_PROFILE **ret_gm) 
{
  int         M   = 3;
  P7_HMM     *hmm = p7_hmm_Create    (M, abc);
  P7_PROFILE *gm  = p7_profile_Create(M, abc);
  char       *logmsg = "[created by brute test]";
  int         k;

  hmm->t[0][p7H_MM] = prm->a;
  hmm->t[0][p7H_MI] = prm->e;
  hmm->t[0][p7H_MD] = ESL_MAX(0.0f, (1.0 - (prm->a+prm->e))); /* fp roundoff error can give 1-(a+e)<0, though we "know" a+e<=1 */
  hmm->t[0][p7H_IM] = prm->h;
  hmm->t[0][p7H_II] = ESL_MAX(0.0f, (1.0 - prm->h));
  hmm->t[0][p7H_DM] = 1.0;	/* D0 doesn't exist; 1.0 is a convention */
  hmm->t[0][p7H_DD] = 0.0;	/* D0 doesn't exist; 0.0 is a convention */
  hmm->t[1][p7H_MM] = prm->b;
  hmm->t[1][p7H_MI] = prm->f;
  hmm->t[1][p7H_MD] = ESL_MAX(0.0f, (1.0 - (prm->b+prm->f)));
  hmm->t[1][p7H_IM] = prm->i;
  hmm->t[1][p7H_II] = ESL_MAX(0.0f, (1.0 - prm->i));
  hmm->t[1][p7H_DM] = ESL_MAX(0.0f, (1.0 - prm->l));
  hmm->t[1][p7H_DD] = prm->l;
  hmm->t[2][p7H_MM] = prm->c;
  hmm->t[2][p7H_MI] = prm->g;
  hmm->t[2][p7H_MD] = ESL_MAX(0.0f, (1.0 - (prm->c+prm->g)));
  hmm->t[2][p7H_IM] = prm->j;
  hmm->t[2][p7H_II] = ESL_MAX(0.0f, (1.0 - prm->j));
  hmm->t[2][p7H_DM] = ESL_MAX(0.0f, (1.0 - prm->m));
  hmm->t[2][p7H_DD] = prm->m;
  hmm->t[3][p7H_MM] = prm->d;	               /* M3->E */
  hmm->t[3][p7H_MI] = ESL_MAX(0.0f, (1.0 - prm->d));
  hmm->t[3][p7H_MD] = 0.0;	               /* no D_M+1 state to move to */
  hmm->t[3][p7H_IM] = prm->k;
  hmm->t[3][p7H_II] = ESL_MAX(0.0f, (1.0 - prm->k));
  hmm->t[3][p7H_DM] = 1.0;	               /* forced transition to E */
  hmm->t[3][p7H_DD] = 0.0;

  for (k = 1; k <= M; k++) {
    esl_vec_FSet(hmm->mat[k], abc->K, ESL_MAX(0.0f, (1.0-prm->alpha)/(float)(abc->K-1)));
    hmm->mat[k][0] = prm->alpha;
  }
  for (k = 0; k <= M; k++) {
    esl_vec_FSet(hmm->ins[k], abc->K, ESL_MAX(0.0f, (1.0-prm->beta)/(float)(abc->K-1)));
    hmm->ins[k][0] = prm->beta;
  }
  
  /* Add mandatory annotation */
  p7_hmm_SetName(hmm, "brute-utest");
  p7_hmm_AppendComlog(hmm, 1, &logmsg);
  hmm->nseq     = 0;
  hmm->eff_nseq = 0;
  p7_hmm_SetCtime(hmm);
  p7_hmm_SetConsensus(hmm, NULL);
  hmm->checksum = 0;
  
  p7_profile_Config(gm, hmm, bg);
  /* Replace profile's configured length and multihit modeling. */
  gm->xsc[p7P_N][p7P_MOVE] = log(prm->n);
  gm->xsc[p7P_N][p7P_LOOP] = log(ESL_MAX(0.0f, (1. - prm->n)));
  gm->xsc[p7P_E][p7P_MOVE] = log(prm->p);
  gm->xsc[p7P_E][p7P_LOOP] = log(ESL_MAX(0.0f, (1. - prm->p)));
  gm->xsc[p7P_C][p7P_MOVE] = log(prm->q);
  gm->xsc[p7P_C][p7P_LOOP] = log(ESL_MAX(0.0f, (1. - prm->q)));
  gm->xsc[p7P_J][p7P_MOVE] = log(prm->r);
  gm->xsc[p7P_J][p7P_LOOP] = log(ESL_MAX(0.0f, (1. - prm->r)));
  gm->xsc[p7P_B][0]        = log(ESL_MAX(0.0f, (1. - prm->s))); 
  gm->xsc[p7P_B][1]        = log(prm->s); 
  gm->xsc[p7P_G][0]        = log(prm->a + prm->e);
  gm->xsc[p7P_G][1]        = log(ESL_MAX(0.0f, (1. - (prm->a+prm->e))));

  *ret_hmm = hmm;
  *ret_gm  = gm;
}


/* score_brute()
 * Enumerates all paths combinatorially (by brute force),
 * calculating their Viterbi or Forward log-odds scores either
 * by max or by summation, for A+ (polyA) sequences of lengths
 * 0..4; returns those scores in sc[0..4].
 */
static void
score_brute(struct p7_brute_utest_s *prm, P7_BG *bg, int do_viterbi, double sc[5])
{

  double a   = prm->a;       double g   = prm->g;       double n   = prm->n;
  double b   = prm->b;       double i   = prm->i;       double p   = prm->p;
  double c   = prm->c;       double j   = prm->j;       double q   = prm->q;
  double e   = prm->e;       double l   = prm->l;       double r   = prm->r;
  double f   = prm->f;       double m   = prm->m;       double s   = prm->s;
  double LM1 = prm->LMk[1];  double LM2 = prm->LMk[2];  double LM3 = prm->LMk[3];
  double msc = prm->alpha / (double) bg->f[0];
  double isc = prm->beta  / (double) bg->f[0];

  double onem_s  = ESL_MAX(0.0, 1.-s);      /* guard against roundoff errors making 1-(x) < 0  */
  double onem_ae = ESL_MAX(0.0, 1.-(a+e)); 
  double onem_bf = ESL_MAX(0.0, 1.-(b+f)); 
  double onem_l  = ESL_MAX(0.0, 1.-l) ;
  double onem_cg = ESL_MAX(0.0, 1.-(c+g)); 
  double onem_m  = ESL_MAX(0.0, 1.-m); 
  double onem_i  = ESL_MAX(0.0, 1.-i); 
  double onem_j  = ESL_MAX(0.0, 1.-j); 
  double onem_n  = ESL_MAX(0.0, 1.-n); 
  double onem_p  = ESL_MAX(0.0, 1.-p); 
  double onem_q  = ESL_MAX(0.0, 1.-q); 
  double onem_r  = ESL_MAX(0.0, 1.-r); 

  double cp[32];	/* "core paths": odds of the 32 paths thru the core model                    */
  double cL[5];		/* summed/maxed paths for core model accounting for substring of length 0..4 */
  double jp[21];	/* "J paths": odds of the 21 paths that use 1 or more cp's and J state(s)    */
  double jL[5];		/* summed odds of core+J accounting for seq of length 0..4                   */
  double ap[10];	/* odds of 10 paths through flanking states, accounting for 0..3 residues    */
  double aL[4];		/* summed odds of flanks accounting for 0..3 residues                        */

  /* 1. There are 32 possible paths thru the core model.
   *    9 with n=1 residues cp[0..8];  7 with n=2, cp[9..15];
   *    7 with n=3, cp[16..22]; and 9 with n=4, cp[23..31]
   *    These are odds ratio calculations (because msc is an odds ratio); they can be > 1
   */
  cp[0] = msc * onem_s * LM1;	                              // n=1  L M1 
  cp[1] = msc * onem_s * LM2;                                 // n=1  L M2 
  cp[2] = msc * onem_s * LM3;                                 // n=1  L M3 
  cp[3] = msc * onem_s * LM1 * onem_bf;                    // n=1  L M1 D2 
  cp[4] = msc * onem_s * LM2 * onem_cg;                    // n=1  L M2 D3
  cp[5] = msc * onem_s * LM1 * onem_bf * m;                // n=1  L M1 D2 D3
  cp[6] = msc * s * (a+e) * onem_bf * m;                    // n=1  G M1 D2 D3
  cp[7] = msc * s * onem_ae * onem_l * onem_cg;         // n=1  G D1 M2 D3
  cp[8] = msc * s * onem_ae * l * onem_m;                  // n=1  G D1 D2 M3

  cp[9]  = msc * msc * onem_s * LM1 * b;                      // n=2  L M1 M2
  cp[10] = msc * msc * onem_s * LM2 * c;                      // n=2  L M2 M3
  cp[11] = msc * msc * onem_s * LM1 * b * onem_cg;         // n=2  L M1 M2 D3
  cp[12] = msc * msc * onem_s * LM1 * onem_bf * onem_m;    // n=2  L M1 D2 M3
  cp[13] = msc * msc * s * (a+e) * b * onem_cg;            // n=2  G M1 M2 M3
  cp[14] = msc * msc * s * (a+e) * onem_bf * onem_m;        // n=2  G M1 D2 M3
  cp[15] = msc * msc * s * onem_ae * onem_l * c;           // n=2  G D1 M2 M3
  
  cp[16] = msc * msc * msc * onem_s * LM1 * b * c;              // n=3  L M1 M2 M3
  cp[17] = msc * isc * msc * onem_s * LM1 * f * i * onem_cg; // n=3  L M1 I1 M2 D3
  cp[18] = msc * isc * msc * onem_s * LM1 * f * i;              // n=3  L M1 I1 M2
  cp[19] = msc * isc * msc * onem_s * LM2 * g * j;              // n=3  L M2 I2 M3
  cp[20] = msc * msc * msc * s * (a+e) * b * c;                 // n=3  G M1 M2 M3
  cp[21] = msc * isc * msc * s * (a+e) * f * i * onem_cg;    // n=3  G M1 I1 M2 D3
  cp[22] = msc * isc * msc * s * onem_ae * onem_l * g * j;   // n=3  G D1 M2 I2 M3

  cp[23] = msc * isc * msc * msc * onem_s * LM1 * f * i * c;                     // n=4  L M1 I1 M2 M3
  cp[24] = msc * isc * isc * msc * onem_s * LM1 * f * onem_i * i * onem_cg;   // n=4  L M1 I1 I1 M2 D3
  cp[25] = msc * msc * isc * msc * onem_s * LM1 * b * g * j;                     // n=4  L M1 M2 I2 M3
  cp[26] = msc * isc * isc * msc * onem_s * LM1 * f * onem_i * i;                // n=4  L M1 I1 I1 M2
  cp[27] = msc * isc * isc * msc * onem_s * LM2 * g * onem_j * j;                // n=4  L M2 I2 I2 M3
  cp[28] = msc * isc * msc * msc * s * (a+e) * f * i * c;                        // n=4  G M1 I1 M2 M3
  cp[29] = msc * isc * isc * msc * s * (a+e) * f * onem_i * i * onem_cg;      // n=4  G M1 I1 I1 M2 D3
  cp[30] = msc * msc * isc * msc * s * (a+e) * b * g * j;                        // n=4  G M1 M2 I2 M3
  cp[31] = msc * isc * isc * msc * s * onem_ae * onem_l * g * onem_j * j;     // n=4  G D1 M2 I2 I2 M3

  /* 2. Now sum/max (Fwd/Vit) the odds ratios of each length 1..4 */
  cL[0] = 0.;
  cL[1] = (do_viterbi ? esl_vec_DMax(cp,    9) : esl_vec_DSum(cp,    9));
  cL[2] = (do_viterbi ? esl_vec_DMax(cp+9,  7) : esl_vec_DSum(cp+9,  7));
  cL[3] = (do_viterbi ? esl_vec_DMax(cp+16, 7) : esl_vec_DSum(cp+16, 7));
  cL[4] = (do_viterbi ? esl_vec_DMax(cp+23, 9) : esl_vec_DSum(cp+23, 9));


  /* 3. J state introduces a combiexplosion of paths accounting for
   *    jL={1..4} total residues in one or more passes through the
   *    core model: 21 such paths.
   */
  jp[0]  = cL[4];			                                          // [4]                    (n=4, 0 in J) 
  jp[1]  = cL[3] * onem_p * r * cL[1];                                            // [3] J [1]              (n=4, 0 in J) 
  jp[2]  = cL[2] * onem_p * r * cL[2];                                            // [2] J [2]              (n=4, 0 in J) 
  jp[3]  = cL[2] * onem_p * r * cL[1] * onem_p * r * cL[1];                       // [2] J [1] J [1]        (n=4, 0 in J) 
  jp[4]  = cL[1] * onem_p * r * cL[3];                                            // [1] J [3]              (n=4, 0 in J) 
  jp[5]  = cL[1] * onem_p * r * cL[2] * onem_p * r * cL[1];                       // [1] J [2] J [1]        (n=4, 0 in J) 
  jp[6]  = cL[1] * onem_p * r * cL[1] * onem_p * r * cL[2];                       // [1] J [1] J [2]        (n=4, 0 in J) 
  jp[7]  = cL[1] * onem_p * r * cL[1] * onem_p * r * cL[1] * onem_p * r * cL[1];  // [1] J [1] J [1] J [1]  (n=4, 0 in J) 
  jp[8]  = cL[2] * onem_p * onem_r * r * cL[1];                                   // [2] JJ [1]             (n=4, 1 in J) 
  jp[9]  = cL[1] * onem_p * onem_r * r * cL[2];                                   // [1] JJ [2]             (n=4, 1 in J) 
  jp[10] = cL[1] * onem_p * onem_r * r * cL[1] * onem_p * r * cL[1];              // [1] JJ [1] J [1]       (n=4, 1 in J) 
  jp[11] = cL[1] * onem_p * r * cL[1] * onem_p * onem_r * r * cL[1];              // [1] J [1] JJ [1]       (n=4, 1 in J) 
  jp[12] = cL[1] * onem_p * onem_r * onem_r * r * cL[1];                          // [1] JJJ [1]            (n=4, 2 in J) 

  jp[13] = cL[3];		                                                  // [3]                    (n=3, 0 in J) 
  jp[14] = cL[2] * onem_p * r * cL[1];                                            // [2] J [1]              (n=3, 0 in J) 
  jp[15] = cL[1] * onem_p * r * cL[2];                                            // [1] J [2]              (n=3, 0 in J) 
  jp[16] = cL[1] * onem_p * r * cL[1] * onem_p * r * cL[1];                       // [1] J [1] J [1]        (n=3, 0 in J) 
  jp[17] = cL[1] * onem_p * onem_r * r * cL[1];                                   // [1] JJ [1]             (n=3, 1 in J) 

  jp[18] = cL[2];                                                                 // [2]                    (n=2, 0 in J) 
  jp[19] = cL[1] * onem_p * r * cL[1];                                            // [1] J [1]              (n=2, 0 in J) 
  
  jp[20] = cL[1];		                                                  // [1]                    (n=1, 0 in J) 

  /* 4. Sum/max to get total path probabilities of jL={1..4} segments */
  jL[0] = 0.;
  jL[1] = jp[20];
  jL[2] = do_viterbi ? esl_vec_DMax(jp + 18, 2) : esl_vec_DSum(jp + 18, 2);
  jL[3] = do_viterbi ? esl_vec_DMax(jp + 13, 5) : esl_vec_DSum(jp + 13, 5);
  jL[4] = do_viterbi ? esl_vec_DMax(jp,     13) : esl_vec_DSum(jp,     13);

  /* 5. Probabilities of 10 possible paths accounting for 0..3 residues in the flanks.
   */
  ap[0] = n * p * q;
  ap[1] = onem_n * n * p * q;
  ap[2] = n * p * onem_q * q;
  ap[3] = onem_n * onem_n * n * p * q;
  ap[4] = onem_n * n * p * onem_q * q;
  ap[5] = n * p * onem_q * onem_q * q;
  ap[6] = onem_n * onem_n * onem_n * n * p * q;
  ap[7] = onem_n * onem_n * n * p * onem_q * q;
  ap[8] = onem_n * n * p * onem_q * onem_q * q;
  ap[9] = n * p * onem_q * onem_q * onem_q * q;

  /* 6. Sum or max the total path probability for the flanks generating
   *     0..3 residues
   */
  aL[0] = ap[0];
  aL[1] = do_viterbi ? esl_vec_DMax(ap+1, 2) : esl_vec_DSum(ap+1, 2);
  aL[2] = do_viterbi ? esl_vec_DMax(ap+3, 3) : esl_vec_DSum(ap+3, 3);
  aL[3] = do_viterbi ? esl_vec_DMax(ap+6, 4) : esl_vec_DSum(ap+6, 4);

  /* 7. And finally: viterbi or forward log-odds scores */
  if (do_viterbi) {
    sc[0] = -eslINFINITY;
    sc[1] = log ( jL[1] * aL[0] );
    sc[2] = log ( ESL_MAX( jL[2] * aL[0],
			   jL[1] * aL[1]) );
    sc[3] = log ( ESL_MAX( ESL_MAX( jL[3] * aL[0],
				    jL[2] * aL[1]),
			            jL[1] * aL[2]) );
    sc[4] = log ( ESL_MAX( ESL_MAX( jL[4] * aL[0],
				    jL[3] * aL[1]),
			   ESL_MAX( jL[2] * aL[2],
				    jL[1] * aL[3])));
  } else {
    sc[0] = -eslINFINITY;
    sc[1] = log(jL[1] * aL[0]);
    sc[2] = log(jL[2] * aL[0] + jL[1] * aL[1]);
    sc[3] = log(jL[3] * aL[0] + jL[2] * aL[1] + jL[1] * aL[2]);
    sc[4] = log(jL[4] * aL[0] + jL[3] * aL[1] + jL[2] * aL[2] + jL[1] * aL[3]);
  }
}

/* finally: the test itself. */
static void
utest_brute(ESL_RANDOMNESS *rng, int N)
{
  char          msg[] = "brute test failed";
  struct p7_brute_utest_s prm;	/* 20 free parameters of the brute test */
  ESL_ALPHABET *abc = esl_alphabet_Create(eslDNA);
  P7_BG        *bg  = p7_bg_Create(abc);
  P7_HMM       *hmm = NULL;
  P7_PROFILE   *gm  = NULL;
  P7_REFMX     *vit = p7_refmx_Create(3, 4); /* M=3, L=4. */
  P7_REFMX     *fwd = p7_refmx_Create(3, 4); /* M=3, L=4. */
  P7_REFMX     *bck = p7_refmx_Create(3, 4); /* M=3, L=4. */
  P7_TRACE     *vtr = p7_trace_Create();
  ESL_DSQ       dsq[6];			     /* digital sequence of up to 4 residues */
  double brute_fwd[5];
  double brute_vit[5];
  float  fsc[5];
  float  bsc[5];
  float  vsc[5];
  int    idx;
  int    L;			/* sequence lengths 1..4 */
  int    pos;
  float         vprecision = 1e-5;
  float         fprecision = ( p7_logsum_IsSlowExact() ? 0.0001 : 0.01 );

  for (idx = 0; idx < N; idx++)
    {
      if (idx == 0) set_bruteparams(&prm);
      else          sample_bruteparams(rng, &prm);

      create_brute_models(&prm, abc, bg, &hmm, &gm);
      score_brute(&prm, bg, FALSE, brute_fwd);
      score_brute(&prm, bg, TRUE,  brute_vit);

      for (L = 1; L <= 4; L++)
	{
	  /* create digital sequence of length L, all A's */
	  dsq[0] = dsq[L+1] = eslDSQ_SENTINEL;
	  for (pos = 1; pos <= L; pos++) dsq[pos] = 0; /* 0='A' */
	  
	  p7_ReferenceViterbi (dsq, L, gm, vit, vtr, &(vsc[L]));   
	  p7_ReferenceForward (dsq, L, gm, fwd,      &(fsc[L])); 
	  p7_ReferenceBackward(dsq, L, gm, bck,      &(bsc[L]));  

	  /* It's possible, though very unusual, for the scores to be -inf.
	   * An example I ran across:
	   *  suppose B->L=0  (so only glocal paths are possible)
	   *  and     G->D1=0 (so you have to start by emitting on match state 1)
	   *  and     DG2->DG3=0 (so G->MG1->DG2->DG3 path has zero prob)
	   * now, for L=1 target seq, no path is possible, all scores -inf.
           * in this case, convention for traces is that they have N=0 (empty).
	   */
	  //if (idx==0) p7_trace_DumpAnnotated(stdout, vtr, gm, dsq);

	  if (esl_FCompareAbs(vsc[L], brute_vit[L], vprecision) != eslOK) esl_fatal(msg);
	  if (esl_FCompareAbs(fsc[L], brute_fwd[L], fprecision) != eslOK) esl_fatal(msg);
	  if (esl_FCompareAbs(bsc[L], brute_fwd[L], fprecision) != eslOK) esl_fatal(msg);

	  p7_trace_Reuse(vtr);
	  p7_refmx_Reuse(vit);
	  p7_refmx_Reuse(fwd);
	  p7_refmx_Reuse(bck);
	}

      p7_profile_Destroy(gm);
      p7_hmm_Destroy(hmm);
    }

  p7_trace_Destroy(vtr);
  p7_refmx_Destroy(fwd);
  p7_bg_Destroy(bg);
  esl_alphabet_Destroy(abc);
}
#endif /*p7REFERENCE_FWDBACK_TESTDRIVE*/
/*----------------- end, unit tests -----------------------------*/




/*****************************************************************
 * 6. Test driver
 *****************************************************************/
#ifdef p7REFERENCE_FWDBACK_TESTDRIVE

#include "p7_config.h"

#include "easel.h"
#include "esl_getopts.h"
#include "esl_msa.h"

#include "hmmer.h"

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "show brief help on version and usage",           0 },
  { "-s",        eslARG_INT,      "0", NULL, NULL,  NULL,  NULL, NULL, "set random number seed to <n>",                  0 },
  { "-v",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "be verbose",                                     0 },
  { "--vv",      eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "be very verbose",                                0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options]";
static char banner[] = "unit test driver for the generic Forward/Backward dual-mode implementation";

int
main(int argc, char **argv)
{
  ESL_GETOPTS    *go   = p7_CreateDefaultApp(options, 0, argc, argv, banner, usage);
  ESL_RANDOMNESS *r    = esl_randomness_CreateFast(esl_opt_GetInteger(go, "-s"));

  p7_Init();

  fprintf(stderr, "## %s\n", argv[0]);
  fprintf(stderr, "#  rng seed = %" PRIu32 "\n", esl_randomness_GetSeed(r));

  utest_brute      (r, 100);

  utest_randomseq  (r, eslAMINO, 50, 100, 100);
  utest_generation (r, eslAMINO, 50, 100, 100);
  utest_duality    (r, eslAMINO, 50, 100, 100);
  utest_enumeration(r);	                   // this test hardcodes eslCOINS, a small M, and a fixed enumeration of target sequences.
  utest_singlepath (r, eslAMINO, 50);

  fprintf(stderr, "#  status = ok\n");
  esl_randomness_Destroy(r);
  esl_getopts_Destroy(go);
  return 0;
}
#endif /*p7REFERENCE_FWDBACK_TESTDRIVE*/
/*---------------- end, test driver -----------------------------*/




/*****************************************************************
 * 7. Example
 *****************************************************************/
#ifdef p7REFERENCE_FWDBACK_EXAMPLE
#include "p7_config.h"

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_getopts.h"
#include "esl_regexp.h"
#include "esl_sq.h"
#include "esl_sqio.h"

#include "hmmer.h"

#define STYLES     "--fs,--sw,--ls,--s"	               /* Exclusive choice for alignment mode     */

static int parse_coord_string(const char *cstring, int *ret_start, int *ret_end);

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range  toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "show brief help on version and usage",             0 },
  { "-i",        eslARG_STRING, FALSE, NULL, NULL,   NULL,  NULL, NULL, "when dumping, restrict dump to rows <i1>..<i2>",    0 },
  { "-k",        eslARG_STRING, FALSE, NULL, NULL,   NULL,  NULL, NULL, "when dumping, restrict dump to columns <k1>..<k2>", 0 },
  { "-s",        eslARG_INT,      "0", NULL, NULL,   NULL,  NULL, NULL, "set random number seed to <n>",                     0 },
  { "--fs",      eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "multihit local alignment",                         0 },
  { "--sw",      eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "unihit local alignment",                           0 },
  { "--ls",      eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "multihit glocal alignment",                        0 },
  { "--s",       eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "unihit glocal alignment",                          0 },
  { "-A",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "dump OA alignment DP matrix for examination",      0 },
  { "-B",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "dump Backward DP matrix for examination",          0 },
  { "-D",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "dump posterior Decoding matrix for examination",   0 },
  { "-F",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "dump Forward DP matrix for examination",           0 },
  { "-S",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "dump stochastic trace for examination",            0 },
  { "-T",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "dump OA traceback for examination",                0 },
  { "--csv",  eslARG_OUTFILE,   NULL,  NULL, NULL,   NULL,  NULL, NULL, "output CSV file of decoding matrix, for heat map", 0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options] <hmmfile> <seqfile>";
static char banner[] = "example of Forward/Backward, generic dual local/glocal implementation";

int 
main(int argc, char **argv)
{
  ESL_GETOPTS    *go      = p7_CreateDefaultApp(options, 2, argc, argv, banner, usage);
  ESL_RANDOMNESS *rng     = esl_randomness_Create(esl_opt_GetInteger(go, "-s"));
  char           *hmmfile = esl_opt_GetArg(go, 1);
  char           *seqfile = esl_opt_GetArg(go, 2);
  char           *csvfile = esl_opt_GetString(go, "--csv");
  ESL_ALPHABET   *abc     = NULL;
  P7_HMMFILE     *hfp     = NULL;
  P7_HMM         *hmm     = NULL;
  P7_BG          *bg      = NULL;
  P7_PROFILE     *gm      = NULL;
  P7_REFMX       *fwd     = p7_refmx_Create(100, 100);
  P7_REFMX       *bck     = p7_refmx_Create(100, 100);
  P7_REFMX       *pp      = p7_refmx_Create(100, 100);
  P7_REFMX       *mge     = p7_refmx_Create(100, 100);
  P7_TRACE       *tr      = p7_trace_Create();
  ESL_SQ         *sq      = NULL;
  ESL_SQFILE     *sqfp    = NULL;
  float          *wrk     = NULL;
  int             format  = eslSQFILE_UNKNOWN;
  float           fsc, bsc;
  float           gain;
  float           nullsc;
  char           *cstring;
  int             istart, iend, kstart, kend;
  char            errbuf[eslERRBUFSIZE];
  int             status;

  p7_Init();

 /* Determine coords of dump windows, if any */
  istart = iend = 0;
  kstart = kend = 0;
  if ( esl_opt_IsOn(go, "-i")) {
    cstring = esl_opt_GetString(go, "-i");
    parse_coord_string(cstring, &istart, &iend);
  }
  if ( esl_opt_IsOn(go, "-k")) {
    cstring = esl_opt_GetString(go, "-k");
    parse_coord_string(cstring, &kstart, &kend);
  }

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
 
  /* Configure a profile from the HMM */
  bg = p7_bg_Create(abc);
  gm = p7_profile_Create(hmm->M, abc);

  /* Now reconfig the models however we were asked to */
  if      (esl_opt_GetBoolean(go, "--fs"))  p7_profile_ConfigLocal    (gm, hmm, bg, sq->n);
  else if (esl_opt_GetBoolean(go, "--sw"))  p7_profile_ConfigUnilocal (gm, hmm, bg, sq->n);
  else if (esl_opt_GetBoolean(go, "--ls"))  p7_profile_ConfigGlocal   (gm, hmm, bg, sq->n);
  else if (esl_opt_GetBoolean(go, "--s"))   p7_profile_ConfigUniglocal(gm, hmm, bg, sq->n);
  else                                      p7_profile_Config         (gm, hmm, bg);

  printf("%-30s   %-10s %-10s   %-10s %-10s\n", "# seq name",      "fwd (raw)",   "bck (raw) ",  "fwd (bits)",  "bck (bits)");
  printf("%-30s   %10s %10s   %10s %10s\n",     "#--------------", "----------",  "----------",  "----------",  "----------");

  while ( (status = esl_sqio_Read(sqfp, sq)) != eslEOF)
    {
      if      (status == eslEFORMAT) p7_Fail("Parse failed (sequence file %s)\n%s\n", sqfp->filename, sqfp->get_error(sqfp));     
      else if (status != eslOK)      p7_Fail("Unexpected error %d reading sequence file %s", status, sqfp->filename);

      /* Set the profile and null model's target length models */
      p7_bg_SetLength     (bg, sq->n);
      p7_profile_SetLength(gm, sq->n);

      //p7_profile_Dump(stdout, gm);

      /* Run Forward, Backward, Decoding; 
       * after decoding, <bck> becomes the pp matrix;
       * after alignment, <fwd> becomes the OA alignment DP matrix
       */
      p7_ReferenceForward (sq->dsq, sq->n, gm, fwd, &fsc);            if (esl_opt_GetBoolean(go, "-F")) p7_refmx_DumpWindow(stdout, fwd, (istart ? istart: 0), (iend ? iend: sq->n), (kstart? kstart : 0), (kend? kend:gm->M));
      p7_ReferenceBackward(sq->dsq, sq->n, gm, bck, &bsc);            if (esl_opt_GetBoolean(go, "-B")) p7_refmx_DumpWindow(stdout, bck, (istart ? istart: 0), (iend ? iend: sq->n), (kstart? kstart : 0), (kend? kend:gm->M));
      p7_ReferenceDecoding(sq->dsq, sq->n, gm, fwd, bck, pp);         if (esl_opt_GetBoolean(go, "-D")) p7_refmx_DumpWindow(stdout, pp,  (istart ? istart: 0), (iend ? iend: sq->n), (kstart? kstart : 0), (kend? kend:gm->M));
      p7_ReferenceAlign   (gm, /*gamma=*/1.0, pp, mge, tr, &gain);    if (esl_opt_GetBoolean(go, "-A")) p7_refmx_DumpWindow(stdout, mge, (istart ? istart: 0), (iend ? iend: sq->n), (kstart? kstart : 0), (kend? kend:gm->M));
      /*                                    */                        if (esl_opt_GetBoolean(go, "-T")) p7_trace_DumpAnnotated(stdout, tr, gm, sq->dsq);

      if (esl_opt_GetBoolean(go, "-S")) 
	{
	  p7_trace_Reuse(tr);
	  p7_reference_trace_Stochastic(rng, &wrk, gm, fwd, tr);
	  if ( p7_trace_Validate(tr, abc, sq->dsq, errbuf) != eslOK) esl_fatal("stochastic trace validation failed: %s", errbuf);
	  p7_trace_DumpAnnotated(stdout, tr, gm, sq->dsq);
	}


      if (csvfile) {
	FILE *csvfp = fopen(csvfile, "w");
	p7_refmx_DumpCSV(csvfp, pp, 1, sq->n, 1, gm->M);
	fclose(csvfp);
      }

      /* Those scores are partial log-odds likelihoods in nats.
       * Subtract off the rest of the null model, convert to bits.
       */
      p7_bg_NullOne(bg, sq->dsq, sq->n, &nullsc);

      printf("%-30s   %10.4f %10.4f   %10.4f %10.4f %10.4f\n", 
	     sq->name, 
	     fsc, 
	     bsc, 
	     (fsc - nullsc) / eslCONST_LOG2,
	     (bsc - nullsc) / eslCONST_LOG2,
	     gain);

      p7_refmx_Reuse(fwd);
      p7_refmx_Reuse(bck);
      p7_refmx_Reuse(pp);
      p7_refmx_Reuse(mge);
      p7_trace_Reuse(tr);
      esl_sq_Reuse(sq);
    }

  /* Cleanup */
  free(wrk);
  esl_sqfile_Close(sqfp);
  esl_sq_Destroy(sq);
  p7_trace_Destroy(tr);
  p7_refmx_Destroy(mge);
  p7_refmx_Destroy(pp);
  p7_refmx_Destroy(fwd);
  p7_refmx_Destroy(bck);
  p7_profile_Destroy(gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  esl_getopts_Destroy(go);
  return 0;
}


static int
parse_coord_string(const char *cstring, int *ret_start, int *ret_end)
{
  ESL_REGEXP *re = esl_regexp_Create();
  char        tok1[32];
  char        tok2[32];

  if (esl_regexp_Match(re, "^(\\d+)\\D+(\\d*)$", cstring) != eslOK) esl_fatal("-c takes arg of subseq coords <from>..<to>; %s not recognized", cstring);
  if (esl_regexp_SubmatchCopy(re, 1, tok1, 32)            != eslOK) esl_fatal("Failed to find <from> coord in %s", cstring);
  if (esl_regexp_SubmatchCopy(re, 2, tok2, 32)            != eslOK) esl_fatal("Failed to find <to> coord in %s",   cstring);
  
  *ret_start = atol(tok1);
  *ret_end   = (tok2[0] == '\0') ? 0 : atol(tok2);
  
  esl_regexp_Destroy(re);
  return eslOK;
}


#endif /*p7REFERENCE_FWDBACK_EXAMPLE*/
/*-------------------- end, example -----------------------------*/


/*****************************************************************
 * @LICENSE@
 *
 * SVN $URL$
 * SVN $Id$
 *****************************************************************/
