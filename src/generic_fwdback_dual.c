/* Forward/Backward implementation variant:
 *   dual-mode alignment (local/glocal);
 *   quadratic memory (simplest variant; not banded, not checkpointed);
 *   "generic" (standard C code; not striped/vectorized);
 *   using P7_GMXD DP matrix structure.
 *   
 * Contents:  
 *   1. Forward implementation.
 *   2. Backward implementation.
 *   3. Benchmark driver.
 *   4. Unit tests.
 *   5. Test driver.
 *   6. Example.
 *   7. Copyright and license information.
 */


#include "p7_config.h"

#include "easel.h"

#include "hmmer.h"
#include "p7_gmxd.h"


/*****************************************************************
 * 1. Forward implementation
 *****************************************************************/

/* Function:  p7_GForwardDual()
 * Synopsis:  Forward, dual-mode, quadratic memory, generic profile
 *
 * Purpose:   The Forward algorithm, comparing profile <gm> to target
 *            sequence <dsq> of length <L>. Caller provides an
 *            allocated <P7_GMXD> DP matrix <gxd>, sized for an
 *            <gm->M> by <L> problem. 
 *            
 *            Caller also has initialized with a <p7_FLogsumInit()>
 *            call; this function will use <p7_FLogsum()>.
 *            
 *            Upon successful return, the raw Forward score (in nats)
 *            is optionally returned in <*opt_sc>, and the DP matrix
 *            <gxd> is filled in.
 *
 * Args:      dsq    : digital target sequence of length <L>
 *            L      : length of the target sequence
 *            gm     : query profile 
 *            gxd    : allocated DP matrix
 *            opt_sc : optRETURN: raw Forward score in nats
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    (no abnormal error conditions)
 *
 * Notes:     This function makes assumptions about the order
 *            of the state indices in p7_gmxd.h:
 *              main states: ML MG IL IG DL DG
 *              specials:    E N J B L G C
 *              
 *            If L=0, the score is -infinity, by construction; HMMER
 *            profiles generate sequences of L>=1.
 */
int
p7_GForwardDual(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_GMXD *gxd, float *opt_sc)
{
  float *dpc, *dpp;
  const float *tsc;		/* ptr for stepping thru profile's transition parameters */
  const float *rsc;		/* ptr for stepping thru profile's emission parameters   */
  int    M          = gm->M;
  int    i, k, s;
  float  mlv, mgv;	      /* ML,MG cell values on current row   */
  float  dlv, dgv; 	      /* pushed-ahead DL,DG cell k+1 values */
  float  xE, xN, xJ, xB, xL, xG;
  
  /* Initialization of the zero row. */
  dpc = gxd->dp[0];
  for (s = 0; s < (M+1) * p7GD_NSCELLS; s++)
    *dpc++ = -eslINFINITY; 	                               // all M,I,D; k=0..M
  *dpc++ = -eslINFINITY;	                               // E
  *dpc++ = 0.0;			                               // N
  *dpc++ = -eslINFINITY;                                       // J
  *dpc++ = gm->xsc[p7P_N][p7P_MOVE];                           // B
  *dpc++ = xL = gm->xsc[p7P_N][p7P_MOVE] + gm->xsc[p7P_B][0];  // L
  *dpc++ = xG = gm->xsc[p7P_N][p7P_MOVE] + gm->xsc[p7P_B][1];  // G
  *dpc        = -eslINFINITY;                                  // C
  /* *dpc must end and stay on C state, to handle L=0 case where the recursion body below doesn't run */

  /* Main DP recursion */
  for (i = 1; i <= L; i++)
    {
      /* Initialization for a new row */
      rsc = gm->rsc[dsq[i]] + p7P_NR;	/* this ptr steps through the row's emission scores 1..M. skip k=0 */
      tsc = gm->tsc;			/* this ptr steps through profile's transition scores 0..M         */

      dpp = gxd->dp[i-1];               /* previous row dpp is already set, and at k=0 */
      dpc = gxd->dp[i];                 /* current DP row, skip k=0, start at k=1.  */
      for (s = 0; s < p7GD_NSCELLS; s++) *dpc++ = -eslINFINITY;

      dlv = dgv = -eslINFINITY;
      xE  =       -eslINFINITY;

      /* Main inner loop of the recursion */
      for (k = 1; k < M; k++)
	{
	  /* match states MLk, MGk */
	  mlv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7GD_ML) + *(tsc + p7P_MM),
						       *(dpp+p7GD_IL) + *(tsc + p7P_IM)),
					    p7_FLogsum(*(dpp+p7GD_DL) + *(tsc + p7P_DM),
						       xL             + *(tsc + p7P_LM)));

	  mgv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7GD_MG) + *(tsc + p7P_MM),
						       *(dpp+p7GD_IG) + *(tsc + p7P_IM)),
 					    p7_FLogsum(*(dpp+p7GD_DG) + *(tsc + p7P_DM),
						       xG             + *(tsc + p7P_GM)));

	  rsc++;                /* rsc advances to insert score for position k */
	  tsc += p7P_NTRANS;    /* tsc advances to transitions in states k     */
	  dpp += p7GD_NSCELLS;	/* dpp advances to cells for states k          */

	  /* Insert state calculations ILk, IGk. */
	  *dpc++ = *rsc + p7_FLogsum( *(dpp + p7GD_ML) + *(tsc + p7P_MI), *(dpp + p7GD_IL) + *(tsc + p7P_II));
	  *dpc++ = *rsc + p7_FLogsum( *(dpp + p7GD_MG) + *(tsc + p7P_MI), *(dpp + p7GD_IG) + *(tsc + p7P_II));
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
      mlv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7GD_ML) + *(tsc + p7P_MM),
						   *(dpp+p7GD_IL) + *(tsc + p7P_IM)),
					p7_FLogsum(*(dpp+p7GD_DL) + *(tsc + p7P_DM),
						   xL             + *(tsc + p7P_LM)));

      mgv = *dpc++ = *rsc + p7_FLogsum( p7_FLogsum(*(dpp+p7GD_MG) + *(tsc + p7P_MM),
						   *(dpp+p7GD_IG) + *(tsc + p7P_IM)),
                    				   *(dpp+p7GD_DG) + *(tsc + p7P_DM));
      dpp  += p7GD_NSCELLS; 

      /* I_M state doesn't exist      */
      *dpc++ = -eslINFINITY;	/* IL */
      *dpc++ = -eslINFINITY;	/* IG */

      /* E state update now includes glocal exits: transition prob 1.0 from MG_m, DG_m */
      xE  = p7_FLogsum( xE, p7_FLogsum( p7_FLogsum(mlv, dlv), p7_FLogsum(mgv, dgv)));
      
      /* D_M state: deferred storage only */
      *dpc++ = dlv;
      *dpc++ = dgv;
    
      /* row i is now finished, and dpc[] is positioned exactly on first special state, E */
      dpp += p7GD_NSCELLS;    /* now dpp[] is also positioned exactly on first special, E */
      
      *dpc++ = xE;		/* E */
      *dpc++ = xN = *(dpp + p7GD_N) + gm->xsc[p7P_N][p7P_LOOP]; /* N */
      *dpc++ = xJ = p7_FLogsum( *(dpp + p7GD_J) + gm->xsc[p7P_J][p7P_LOOP],  xE + gm->xsc[p7P_E][p7P_LOOP]); /* J */
      *dpc++ = xB = p7_FLogsum(             xN  + gm->xsc[p7P_N][p7P_MOVE],  xJ + gm->xsc[p7P_J][p7P_MOVE]); /* B */
      *dpc++ = xL = xB  + gm->xsc[p7P_B][0]; /* L */
      *dpc++ = xG = xB  + gm->xsc[p7P_B][1]; /* G */
      *dpc        = p7_FLogsum( *(dpp + p7GD_C) + gm->xsc[p7P_C][p7P_LOOP],  xE + gm->xsc[p7P_E][p7P_MOVE]); /* C */
    }
  /* Done with all rows i. As we leave, dpc is still sitting on the xC value for i=L ... including even the L=0 case */
  
  if (opt_sc) *opt_sc = *dpc + gm->xsc[p7P_C][p7P_MOVE];
  gxd->M = M;
  gxd->L = L;
  return eslOK;
}
/*-----------  end, Forwards implementation ---------------------*/



/*****************************************************************
 * 2. Backwards implementation
 *****************************************************************/

/* Function:  p7_GBackwardDual()
 * Synopsis:  Backward, dual-mode, quadratic memory, generic profile
 *
 * Purpose:   The Backward algorithm, comparing profile <gm> to target
 *            sequence <dsq> of length <L>. Caller provides an
 *            allocated <P7_GMXD> DP matrix <gxd>, sized for an
 *            <gm->M> by <L> problem. 
 *            
 *            Caller also has initialized with a <p7_FLogsumInit()>
 *            call; this function will use <p7_FLogsum()>.
 *            
 *            Upon successful return, the raw Backward score (in nats)
 *            is optionally returned in <*opt_sc>, and the DP matrix
 *            <gxd> is filled in.
 *
 * Args:      dsq    : digital target sequence of length <L>
 *            L      : length of the target sequence
 *            gm     : query profile 
 *            gxd    : allocated DP matrix
 *            opt_sc : optRETURN: raw Backward score in nats
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    (no abnormal error conditions)
 *
 * Notes:     In <gm->rsc>, assumes p7P_NR = 2 and order [M I]
 *            In <gm->tsc>, does not make assumptions about p7P_NTRANS or order of values
 *            in <gxd->dp[i]>, assumes p7GD_NSCELLS=6 in order [ ML MG IL IG DL DG]
 *                             assumes p7GD_NXCELLS=7 in order [ E N J B L G C ]
 *                             
 *            Order of evaluation in the code is pretty carefully
 *            arranged to guarantee that dpc,dpn could be pointing
 *            into the same row of memory in a memory-efficient
 *            one-row DP implementation... even though this particular
 *            function, working with a <gxd>, knows it has rows
 *            <0,1..L>. This is so this code could be cribbed for a
 *            one-row implementation.
 */
int
p7_GBackwardDual(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_GMXD *gxd, float *opt_sc)
{
  float *dpc;			/* ptr into current DP row, gxd->dp[i]                    */
  float *dpn;	            	/* ptr into next DP row, gxd->dp[i+1]                     */   // dpc, dpn could point to same row, in a single-row implementation.
  const float *rsc;		/* ptr to current row's residue score x_i vector in <gm>  */
  const float *rsn;		/* ptr to next row's residue score x_{i+1} vector in <gm> */
  const float *tsc;		/* ptr to model transition score vector gm->tsc[]         */
  float dgc, dlc;	        /* DG,DL tmp values on current row, [i,?,DG], [i,?,DL]    */
  float mgc, mlc;
  float mgn, mln;
  float ign, iln;
  float xN, xJ, xC, xE, xG, xL, xB;	/* temp vars for special state values                     */
  int   i;			/* counter over sequence positions 1..L */
  int   k;			/* counter over model positions 1..M    */
  const int M  = gm->M;

  /* Initialize row L. */
  /* Specials are in order ENJBLGC: step backwards thru them */
  dpc  = gxd->dp[L] + (M+1)*p7GD_NSCELLS + p7GD_C;
  rsc  = gm->rsc[dsq[L]] + M*p7P_NR;

  *dpc-- = xC = gm->xsc[p7P_C][p7P_MOVE];      /* C : C<-T */
  *dpc--      = -eslINFINITY;                  /* G : impossible w/o residues after it */
  *dpc--      = -eslINFINITY;	               /* L : ditto, impossible */
  *dpc--      = -eslINFINITY;	               /* B : ditto, impossible */
  *dpc--      = -eslINFINITY;	               /* J : ditto, impossible */
  *dpc--      = -eslINFINITY;	               /* N : ditto, impossible */
  *dpc-- = xE = xC + gm->xsc[p7P_E][p7P_MOVE]; /* E: E<-C<-T, no tail */
  /* dpc is now sitting on [M][DG] */
  
  /* dpc main cells for k=M*/
  tsc = gm->tsc + (M-1)*p7P_NTRANS;
  
  xG   = xE + *rsc + *(tsc + p7P_GM);
  xL   = xE + *rsc + *(tsc + p7P_LM);
  rsc -= p7P_NR;

  *dpc-- = dgc = xE;		/* DG: D_M->E (transition prob 1.0)  */
  *dpc-- = dlc = xE;		/* DL: ditto */
  *dpc-- = -eslINFINITY;	/* IG_M: no such state: always init'ed to -inf */
  *dpc-- = -eslINFINITY;	/* IL_M: no such state: always init'ed to -inf */
  *dpc-- = xE;			/* MG: M_M->E (transition prob 1.0)  */
  *dpc-- = xE;			/* ML: ditto */
  /* dpc is now sitting on [M-1][DG] */

  /* initialize main cells [k=1..M-1] on row i=L*/
  for (k = M-1; k >= 1; k--)
    {
      mgc =                 dgc + *(tsc + p7P_MD);
      mlc =  p7_FLogsum(xE, dlc + *(tsc + p7P_MD));

      xG   = p7_FLogsum(xG, mgc + *rsc + *(tsc + p7P_GM - p7P_NTRANS)); /* off-by-one: tGMk stored as [k-1,GM] */
      xL   = p7_FLogsum(xL, mlc + *rsc + *(tsc + p7P_LM - p7P_NTRANS));
      rsc -= p7P_NR;

      *dpc-- = dgc =                dgc + *(tsc + p7P_DD);  /* DG: only D->D path is possible */
      *dpc-- = dlc = p7_FLogsum(xE, dlc + *(tsc + p7P_DD)); /* DL: Dk->Dk+1 or Dk->E */
      *dpc--       = -eslINFINITY;  	                    /* IG impossible w/o residues following it */
      *dpc--       = -eslINFINITY;	                    /* IL, ditto */
      *dpc--       = mgc;
      *dpc--       = mlc;
      tsc -= p7P_NTRANS;
    }

  /* k=0 cells are -inf */


  /* The main recursion over rows i=L-1 down to 1. (residues x_{L-1} down to x_1) */
  for (i = L-1; i >= 1; i--)
    {
                                                        /* ...xG,xL inherited from previous loop...               */
      rsn = gm->rsc[dsq[i+1]] + M * p7P_NR;        	/* residue x_{i+1} scores in *next* row:  start at end, M */
      rsc = gm->rsc[dsq[i]]   + M * p7P_NR;	        /* residue x_{i} scores in *current* row: start at end, M */
      dpc = gxd->dp[i]   + (M+1)*p7GD_NSCELLS + p7GD_C;	/* dpc is on [i,(M),C]   : end of current row's specials  */
      dpn = gxd->dp[i+1] + (M+1)*p7GD_NSCELLS;	        /* dpn is on [i+1,(M),0] : start of next row's specials   */
      tsc = gm->tsc + M * p7P_NTRANS;		        /* tsc is on t[M,0]: vector [MM IM DM LM GM MD DD MI II]  */

      /* Calculation of the special states. */
      /* dpc is on dp[i][C] special, will now step backwards thru [E N J B L G C ] */
      *dpc-- = xC = *(dpn + p7GD_C) + gm->xsc[p7P_C][p7P_LOOP]; /* C = C<-C */

      *dpc-- = xG;     /* G was calculated during prev row (G->Mk wing unfolded) */
      *dpc-- = xL;     /* L was calculated during prev row */

      *dpc-- = xB = p7_FLogsum( xG + gm->xsc[p7P_B][1],    /* B<-G */
				xL + gm->xsc[p7P_B][0]);   /* B<-L */
      
      *dpc-- = xJ = p7_FLogsum( *(dpn + p7GD_J) + gm->xsc[p7P_J][p7P_LOOP],   /* J<-J */
				xB              + gm->xsc[p7P_J][p7P_MOVE]);  /* J<-B */
      
      *dpc--      = p7_FLogsum( *(dpn + p7GD_N) + gm->xsc[p7P_N][p7P_LOOP],  /* N<-N */
				xB              + gm->xsc[p7P_N][p7P_MOVE]); /* N<-B */

      *dpc-- = xE = p7_FLogsum( xC + gm->xsc[p7P_E][p7P_MOVE],
				xJ + gm->xsc[p7P_E][p7P_LOOP]);
      dpn -= 5;		      /* backs dpn up to [i+1,M,MG], skipping [IL IG DL DG] at k=M */
      

      /* Initialization of the k=M states */
      /* dpc on [i,k=M,DG], init at k=M, step back thru [ ML MG IL IG DL DG] */
      /* dpn on [i+1,k=M,MG] */
      mgn = *rsn + *dpn--;	/* pick up MG(i+1,k=M) + s(x_i+1,k=M, M) */
      mln = *rsn + *dpn--;	/* pick up ML(i+1,k=M) + s(x_i+1,k=M, M) */
      rsn--;			/* rsn now on s(x_i+1, k=M-1, I)         */

      xG     = xE + *rsc + *(tsc + p7P_GM - p7P_NTRANS); /* t[k-1][GM] is G->Mk wing-folded entry, recall off-by-one storage   */
      xL     = xE + *rsc + *(tsc + p7P_LM - p7P_NTRANS); /* t[k-1][LM] is L->Mk uniform local entry */
      rsc -= p7P_NR;		/* rsc now on s[x_{i},M-1,M] */
      tsc -= p7P_NTRANS;	/* tsc now on t[M-1,0]       */

      *dpc-- = dgc = xE;		/* DGm->E */
      *dpc-- = dlc = xE;		/* DLm->E */
      *dpc--       = -eslINFINITY;	/* IGm nonexistent */
      *dpc--       = -eslINFINITY;	/* ILm nonexistent */
      *dpc--       = xE;		/* MGm->E */
      *dpc--       = xE;		/* MLm->E */
      /* dpc on [i,M-1,DG]; dpn on [i+1,M-1,DG] */


      /* The main recursion over model positions k=M-1 down to 1. */
      for (k = M-1; k >= 1; k--)
	{
                             	    /* rsn is on residue score [x_{i+1},k,I]    */
	                            /* dpn is on [i+1,k,DG]                     */
	  dpn -= 2;	   	    /* skip DG/DL values on next row            */
	  ign = *dpn--;		    /* pick up IG value from dp[i+1]            */ // if inserts had nonzero score: + *rsn 
	  iln = *dpn--;		    /* pick up IL value                         */ // if inserts had nonzero score: + *rsn
	  rsn--;		    /* skip residue score for I (zero)          */ 
	                            /* dpn is now sitting on dp[i+1,k,MG]       */

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

	  /* dpc is on [i,k,DG] and will now step backwards thru: [ ML MG IL IG DL DG ] */
	  *dpc-- = dgc = p7_FLogsum( mgn + *(tsc + p7P_DM),   /* dgc picked up for next loop of k */
				     dgc + *(tsc + p7P_DD));
	  *dpc-- = dlc = p7_FLogsum( p7_FLogsum( mln + *(tsc + p7P_DM),   /* dlc picked up for next loop of k */
						 dlc + *(tsc + p7P_DD)),
				     xE);

	  *dpc-- = p7_FLogsum( mgn + *(tsc + p7P_IM),
			       ign + *(tsc + p7P_II));
	  *dpc-- = p7_FLogsum( mln + *(tsc + p7P_IM),
			       iln + *(tsc + p7P_II));

                                /* recall that dpn is on dp[i+1][k,MG]    */
	  mgn = *rsn + *dpn--;	/* pick up M[i+1,k]; add score[x_i+1,k,M] */
	  mln = *rsn + *dpn--;
	  rsn--;		/* rsn is now on score[i+1,k-1,I] */

	  *dpc-- = mgc;		/* delayed store of [i,k,MG] value enables dpc,dpn to point into same single row */
	  *dpc-- = mlc;

	  tsc -= p7P_NTRANS;

	  /* as we loop around now and decrement k:
           *   dpn is on [i+1,k-1,DG] which becomes [i+1,k,DG] 
           *   dpc is on [i,k-1,DG]   which becomes [i,k,DG] 
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
  /* now on row i=0. Only N,B,G,L states are reachable on this initial row. G,L values are already done. */
  
  dpc = gxd->dp[0] + (M+1)*p7GD_NSCELLS + p7GD_C;	/* dpc is on [0,(M),C] : end of row 0 specials  */
  dpn = gxd->dp[1] + (M+1)*p7GD_NSCELLS;	        /* dpn is on [1,(M),0] : start of row 1 specials   */

  *dpc--      = -eslINFINITY;                                           /* C */
  *dpc--      = xG;                                                     /* G */
  *dpc--      = xL;                                                     /* L */
  *dpc-- = xB = p7_FLogsum( xG + gm->xsc[p7P_B][1],                     /* B */
			    xL + gm->xsc[p7P_B][0]);   
  *dpc--      = -eslINFINITY;                                           /* J */
  *dpc-- = xN = p7_FLogsum( *(dpn + p7GD_N) + gm->xsc[p7P_N][p7P_LOOP],	/* N */
			    xB              + gm->xsc[p7P_N][p7P_MOVE]); 
  *dpc--      = -eslINFINITY;                                           /* E */

  gxd->M = M;
  gxd->L = L;
  if (opt_sc) *opt_sc = xN;
  return eslOK;
}
/*-------------- end, backwards implementation ------------------*/



/*****************************************************************
 * 3. Benchmark driver.
 *****************************************************************/
#ifdef p7GENERIC_FWDBACK_DUAL_BENCHMARK
#include "p7_config.h"

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_getopts.h"
#include "esl_random.h"
#include "esl_randomseq.h"
#include "esl_stopwatch.h"

#include "hmmer.h"
#include "p7_gmxd.h"

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "show brief help on version and usage",           0 },
  { "-s",        eslARG_INT,     "42", NULL, NULL,  NULL,  NULL, NULL, "set random number seed to <n>",                  0 },
  { "-L",        eslARG_INT,    "400", NULL, "n>0", NULL,  NULL, NULL, "length of random target seqs",                   0 },
  { "-N",        eslARG_INT,   "2000", NULL, "n>0", NULL,  NULL, NULL, "number of random target seqs",                   0 },
  { "-B",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "only benchmark Backward",                        0 },
  { "-F",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "only benchmark Forward",                         0 },
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
  P7_GMXD        *fwd     = NULL;
  P7_GMXD        *bck     = NULL;
  int             L       = esl_opt_GetInteger(go, "-L");
  int             N       = esl_opt_GetInteger(go, "-N");
  ESL_DSQ        *dsq     = malloc(sizeof(ESL_DSQ) * (L+2));
  int             i;
  float           sc;
  double          base_time, bench_time, Mcs;

  if (p7_hmmfile_OpenE(hmmfile, NULL, &hfp, NULL) != eslOK) p7_Fail("Failed to open HMM file %s", hmmfile);
  if (p7_hmmfile_Read(hfp, &abc, &hmm)            != eslOK) p7_Fail("Failed to read HMM");

  bg = p7_bg_Create(abc);
  p7_bg_SetLength(bg, L);

  gm = p7_profile_Create(hmm->M, abc);
  p7_profile_Config(gm, hmm, bg);
  p7_profile_SetLength(gm, L);

  fwd = p7_gmxd_Create(gm->M, L);
  bck = p7_gmxd_Create(gm->M, L);

  /* Baseline time. */
  esl_stopwatch_Start(w);
  for (i = 0; i < N; i++) esl_rsq_xfIID(r, bg->f, abc->K, L, dsq);
  esl_stopwatch_Stop(w);
  base_time = w->user;

  /* Benchmark time. */
  esl_stopwatch_Start(w);
  for (i = 0; i < N; i++)
    {
      esl_rsq_xfIID(r, bg->f, abc->K, L, dsq);
      if (! esl_opt_GetBoolean(go, "-B"))  p7_GForwardDual (dsq, L, gm, fwd, &sc);
      if (! esl_opt_GetBoolean(go, "-F"))  p7_GBackwardDual(dsq, L, gm, bck, NULL);

      p7_gmxd_Reuse(fwd);
      p7_gmxd_Reuse(bck);
    }
  esl_stopwatch_Stop(w);
  bench_time = w->user - base_time;
  Mcs        = (double) N * (double) L * (double) gm->M * 1e-6 / (double) bench_time;
  esl_stopwatch_Display(stdout, w, "# CPU time: ");
  printf("# M    = %d\n",   gm->M);
  printf("# %.1f Mc/s\n", Mcs);

  free(dsq);
  p7_gmxd_Destroy(bck);
  p7_gmxd_Destroy(fwd);
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
#endif /*p7GENERIC_FWDBACK_DUAL_BENCHMARK*/
/*----------------- end, benchmark ------------------------------*/



/*****************************************************************
 * 4. Unit tests
 *****************************************************************/
#ifdef p7GENERIC_FWDBACK_DUAL_TESTDRIVE
#include "esl_getopts.h"
#include "esl_random.h"
#include "esl_randomseq.h"

/* The p7_GForward() function only evaluates local alignment,
 * regardless of configuration of glocal/local in <gm>.  p7_GForward()
 * with a model configured in dual-mode should give the same score as
 * p7_GForwardDual() with a model configured in local-only mode.
 *
 * Make two profiles from a sampled <hmm>, one local-only and one dual-mode (both multihit, L=L).
 * Generate <nseq> iid random sequences of length <L>, using <bg> frequencies and the seeded <rng>.
 * Score each sequence, using p7_GForward(dual) and p7_GForwardDual(local). 
 * Check that raw nat scores match (within an absolute floating point tolerance).
 * Also, check that average bit score (e.g. expected score on random seqs) is nonpositive.
 */
static void
utest_compare_local(ESL_GETOPTS *go, ESL_RANDOMNESS *rng)
{
  char          msg[]  = "generic_fwdback_dual : compare-local unit test failed";
  ESL_DSQ      *dsq    = NULL;
  ESL_ALPHABET *abc    = NULL;
  P7_HMM       *hmm    = NULL;
  P7_PROFILE   *gmd    = NULL;
  P7_PROFILE   *gml    = NULL;
  P7_GMX       *gx     = NULL;
  P7_GMXD      *gxd    = NULL;
  P7_BG        *bg     = NULL;
  //int           M      = 100;
  //int           L      = 200;
  int           M      = 10;
  int           L      = 10;
  int           nseq   = 20;
  float         avg_sc = 0.0;
  float         sc1, sc2, nullsc;
  int           idx;
  char          errbuf[eslERRBUFSIZE];

  if ((abc = esl_alphabet_Create(eslAMINO))      == NULL)  esl_fatal(msg);
  if ( p7_hmm_Sample(rng, M, abc, &hmm)          != eslOK) esl_fatal(msg);
  if (p7_hmm_Validate (hmm, errbuf, 0.0001)      != eslOK) esl_fatal("bad hmm: %s", errbuf);

  if ((bg = p7_bg_Create(abc))                   == NULL)  esl_fatal(msg);
  if ( p7_bg_SetLength(bg, L)                    != eslOK) esl_fatal(msg);                 

  if (( gmd = p7_profile_Create(hmm->M, abc) )   == NULL)  esl_fatal(msg);
  if (( gml = p7_profile_Create(hmm->M, abc) )   == NULL)  esl_fatal(msg);

  if ( p7_profile_Config(gmd, hmm, bg)           != eslOK)  esl_fatal(msg); /* gmd is dual-mode, multihit, L=L */
  if ( p7_profile_SetLength(gmd, L)              != eslOK)  esl_fatal(msg);

  if ( p7_profile_ConfigLocal(gml, hmm, bg, L)   != eslOK)  esl_fatal(msg); /* gml is local-mode, multihit, L=L */

  if ( p7_profile_Validate(gmd,  errbuf, 0.0001) != eslOK) esl_fatal("bad profile: %s", errbuf);
  if ( p7_profile_Validate(gml,  errbuf, 0.0001) != eslOK) esl_fatal("bad profile: %s", errbuf);

  if (( dsq = malloc(sizeof(ESL_DSQ) * (L+2)))   == NULL)  esl_fatal(msg);
  if (( gx  = p7_gmx_Create(hmm->M, L))          == NULL)  esl_fatal(msg);
  if (( gxd = p7_gmxd_Create(hmm->M, L))         == NULL)  esl_fatal(msg);

  for (idx = 0; idx < nseq; idx++)
    {
      if ( esl_rsq_xfIID(rng, bg->f, abc->K, L, dsq) != eslOK) esl_fatal(msg);
      if ( p7_GForward    (dsq, L, gmd, gx,  &sc1)   != eslOK) esl_fatal(msg);
      if ( p7_GForwardDual(dsq, L, gml, gxd, &sc2)   != eslOK) esl_fatal(msg);
      if ( p7_bg_NullOne  (bg, dsq, L, &nullsc)      != eslOK) esl_fatal(msg);

      if (fabs(sc1-sc2) > 0.0001) esl_fatal(msg);

      avg_sc += (sc2 - nullsc) / eslCONST_LOG2; /* bit conversion is for consistency; unnecessary here because we're only going to check for nonpositive value */
    }
  
  avg_sc /= (float) nseq;
  if (avg_sc > 0.0) esl_fatal(msg);
  
  p7_gmxd_Destroy(gxd);
  p7_gmx_Destroy(gx);
  p7_profile_Destroy(gmd);
  p7_profile_Destroy(gml);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  free(dsq);
} 


/* The "duality" test uses the fact that for unihit models, the
 * dual-mode score should be equal to FLogsum(local_score +
 * glocal_score) - 1 bit.
 */
static void
utest_duality(ESL_GETOPTS *go, ESL_RANDOMNESS *rng)
{
  char          msg[]  = "generic_fwdback_dual : duality unit test failed";
  ESL_DSQ      *dsq    = NULL;
  ESL_ALPHABET *abc    = NULL;
  P7_HMM       *hmm    = NULL;
  P7_BG        *bg     = NULL;
  P7_PROFILE   *gmd    = NULL;
  P7_PROFILE   *gml    = NULL;
  P7_PROFILE   *gmg    = NULL;
  P7_GMXD      *gxd    = NULL;
  int           M      = 100;
  int           L      = 200;
  int           nseq   = 20;
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
  if (( gxd = p7_gmxd_Create(hmm->M, L))       == NULL)  esl_fatal(msg);

  for (idx = 0; idx < nseq; idx++)
    {
      if ( esl_rsq_xfIID(rng, bg->f, abc->K, L, dsq) != eslOK) esl_fatal(msg);

      if ( p7_GForwardDual(dsq, L, gmd, gxd, &dual_sc)   != eslOK) esl_fatal(msg);
      if ( p7_gmxd_Reuse(gxd)                            != eslOK) esl_fatal(msg);

      if ( p7_GForwardDual(dsq, L, gml, gxd, &local_sc)  != eslOK) esl_fatal(msg);
      if ( p7_gmxd_Reuse(gxd)                            != eslOK) esl_fatal(msg);

      if ( p7_GForwardDual(dsq, L, gmg, gxd, &glocal_sc) != eslOK) esl_fatal(msg);
      if ( p7_gmxd_Reuse(gxd)                            != eslOK) esl_fatal(msg);

      combined_sc = p7_FLogsum(local_sc, glocal_sc) - eslCONST_LOG2;

      if (fabs(dual_sc-combined_sc) > 0.001)  esl_fatal(msg);
    }
  
  p7_gmxd_Destroy(gxd);
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
 * p7_GForwardDual(), obtains P(seq | profile) from the score, and
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
utest_enumeration(ESL_GETOPTS *go, ESL_RANDOMNESS *rng)
{
  char          msg[] = "enumeration unit test failed";
  ESL_ALPHABET *abc   = esl_alphabet_Create(eslCOINS);
  P7_HMM       *hmm   = NULL;
  P7_BG        *bg    = NULL;
  ESL_DSQ      *dsq   = NULL;
  P7_PROFILE   *gm    = NULL;
  P7_GMXD      *gxd   = NULL;
  int           M     = 8;
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
  if (( gxd = p7_gmxd_Create(hmm->M, maxL+1))     == NULL)  esl_fatal(msg); /* +1 because of the maxL+1 final test */

  /* L=0 included just to test that an L=0 sequence does indeed get a score of -inf, as it should */
  for (L = 0; L <= maxL; L++)
    {
      /* initialize dsq[1..L] at "0000..." */
      dsq[0] = dsq[L+1] = eslDSQ_SENTINEL;
      for (i = 1; i <= L; i++) dsq[i] = 0;

      /* enumerate and score all sequences of length L */
      while (1)	
	{
	  if ( p7_GForwardDual(dsq, L, gm, gxd, &fsc) != eslOK) esl_fatal(msg);
	  
	  /* calculate bg log likelihood component of the scores */
	  for (bg_ll = 0., i = 1; i <= L; i++)  bg_ll += log(bg->f[dsq[i]]);
	  
	  /* convert to probability P(seq|model), adding the bg LL back to the LLR */
	  fp =  exp(fsc + bg_ll);
	  total_p += fp;

	  /* Increment dsq to next seq, like a reversed odometer; works for any alphabet */
	  for (i = 1; i <= L; i++) 
	    if (dsq[i] < abc->K-1) { dsq[i]++; break; } else { dsq[i] = 0; }
	  if (i > L) break;	/* we're done enumerating sequences */

	  p7_gmxd_Reuse(gxd);
	}
    }

  /* That sum is subject to significant numerical error because of
   * discretization error in FLogsum(); don't expect it to be too close.
   */
  if (total_p < 0.999 || total_p > 1.001) esl_fatal(msg);

  /* And any sequence of length L > maxL should get score -infinity. */
  if ( esl_rsq_xfIID(rng, bg->f, abc->K, maxL+1, dsq) != eslOK) esl_fatal(msg);
  if ( p7_GForwardDual(dsq, maxL+1, gm, gxd, &fsc)    != eslOK) esl_fatal(msg);
  if ( fsc != -eslINFINITY) esl_fatal(msg);                                    

  p7_gmxd_Destroy(gxd);
  p7_profile_Destroy(gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  free(dsq);
}
#endif /*p7GENERIC_FWDBACK_DUAL_TESTDRIVE*/

/*----------------- end, unit tests -----------------------------*/




/*****************************************************************
 * 5. Test driver
 *****************************************************************/
#ifdef p7GENERIC_FWDBACK_DUAL_TESTDRIVE

#include "p7_config.h"

#include "easel.h"
#include "esl_getopts.h"
#include "esl_msa.h"

#include "hmmer.h"
#include "p7_gmxd.h"

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "show brief help on version and usage",           0 },
  { "-s",        eslARG_INT,     "42", NULL, NULL,  NULL,  NULL, NULL, "set random number seed to <n>",                  0 },
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

  p7_FLogsumInit();

  utest_compare_local(go, r);
  utest_duality      (go, r);
  utest_enumeration  (go, r);

  esl_randomness_Destroy(r);
  esl_getopts_Destroy(go);
  return 0;
}


#endif /*p7GENERIC_FWDBACK_DUAL_TESTDRIVE*/
/*---------------- end, test driver -----------------------------*/




/*****************************************************************
 * 6. Example
 *****************************************************************/
#ifdef p7GENERIC_FWDBACK_DUAL_EXAMPLE
#include "p7_config.h"

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_getopts.h"
#include "esl_sq.h"
#include "esl_sqio.h"

#include "hmmer.h"
#include "p7_gmxd.h"

#define STYLES     "--fs,--sw,--ls,--s"	               /* Exclusive choice for alignment mode     */

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range  toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "show brief help on version and usage",             0 },
  { "--fs",      eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "multihit local alignment",                         0 },
  { "--sw",      eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "unihit local alignment",                           0 },
  { "--ls",      eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "multihit glocal alignment",                        0 },
  { "--s",       eslARG_NONE,   FALSE, NULL, NULL, STYLES,  NULL, NULL, "unihit glocal alignment",                          0 },
  { "--vv",      eslARG_NONE,   FALSE, NULL, NULL,   NULL,  NULL, NULL, "very verbose debugging output: inc. DP matrix",    0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options] <hmmfile> <seqfile>";
static char banner[] = "example of Forward/Backward, generic dual local/glocal implementation";

int 
main(int argc, char **argv)
{
  ESL_GETOPTS    *go      = p7_CreateDefaultApp(options, 2, argc, argv, banner, usage);
  char           *hmmfile = esl_opt_GetArg(go, 1);
  char           *seqfile = esl_opt_GetArg(go, 2);
  ESL_ALPHABET   *abc     = NULL;
  P7_HMMFILE     *hfp     = NULL;
  P7_HMM         *hmm     = NULL;
  P7_BG          *bg      = NULL;
  P7_PROFILE     *gm      = NULL;
  P7_GMXD        *fwd     = NULL;
  P7_GMXD        *bck     = NULL;
  ESL_SQ         *sq      = NULL;
  ESL_SQFILE     *sqfp    = NULL;
  int             format  = eslSQFILE_UNKNOWN;
  float           fsc, bsc;
  float           nullsc;
  int             status;

  /* Initialize log-sum calculator */
  p7_FLogsumInit();

  /* Read in one HMM */
  if (p7_hmmfile_OpenE(hmmfile, NULL, &hfp, NULL) != eslOK) p7_Fail("Failed to open HMM file %s", hmmfile);
  if (p7_hmmfile_Read(hfp, &abc, &hmm)            != eslOK) p7_Fail("Failed to read HMM");
  p7_hmmfile_Close(hfp);
 
  /* Read in one sequence */
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

  /* Allocate matrices */
  fwd = p7_gmxd_Create(gm->M, sq->n);
  bck = p7_gmxd_Create(gm->M, sq->n);

  printf("%-30s   %-10s %-10s   %-10s %-10s\n", "# seq name",      "fwd (raw)",   "bck (raw) ",  "fwd (bits)",  "bck (bits)");
  printf("%-30s   %10s %10s   %10s %10s\n",     "#--------------", "----------",  "----------",  "----------",  "----------");

  while ( (status = esl_sqio_Read(sqfp, sq)) != eslEOF)
    {
      if      (status == eslEFORMAT) p7_Fail("Parse failed (sequence file %s)\n%s\n", sqfp->filename, sqfp->get_error(sqfp));     
      else if (status != eslOK)      p7_Fail("Unexpected error %d reading sequence file %s", status, sqfp->filename);

      /* Resize the DP matrices if necessary */
      p7_gmxd_GrowTo(fwd, gm->M, sq->n);
      p7_gmxd_GrowTo(bck, gm->M, sq->n);

      /* Set the profile and null model's target length models */
      p7_bg_SetLength     (bg, sq->n);
      p7_profile_SetLength(gm, sq->n);

      //p7_profile_Dump(stdout, gm);

      /* Run Forward, Backward */
      p7_GForwardDual (sq->dsq, sq->n, gm, fwd, &fsc);
      p7_GBackwardDual(sq->dsq, sq->n, gm, bck, &bsc);

      if (esl_opt_GetBoolean(go, "--vv")) p7_gmxd_Dump(stdout, bck);

      /* Those scores are partial log-odds likelihoods in nats.
       * Subtract off the rest of the null model, convert to bits.
       */
      p7_bg_NullOne(bg, sq->dsq, sq->n, &nullsc);

      printf("%-30s   %10.4f %10.4f   %10.4f %10.4f\n", 
	     sq->name, 
	     fsc, bsc, 
	     (fsc - nullsc) / eslCONST_LOG2, (bsc - nullsc) / eslCONST_LOG2);

      p7_gmxd_Reuse(fwd);
      p7_gmxd_Reuse(bck);
      esl_sq_Reuse(sq);
    }

  /* Cleanup */
  esl_sqfile_Close(sqfp);
  esl_sq_Destroy(sq);
  p7_gmxd_Destroy(fwd);
  p7_gmxd_Destroy(bck);
  p7_profile_Destroy(gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  esl_getopts_Destroy(go);
  return 0;
}
#endif /*p7GENERIC_FWDBACK_DUAL_EXAMPLE*/
/*-------------------- end, example -----------------------------*/


/*****************************************************************
 * @LICENSE@
 *
 * SVN $URL$
 * SVN $Id$
 *****************************************************************/
