/* Reference implementation of the DP algorithms.
 * 
 * This implementation is intended for clarity, not speed. It is
 * deliberately not optimized. It is provided as a reference
 * implementation of the computationally intensive DP algorithms of
 * HMMER. It serves both as documentation and as a regression test
 * target for developing optimized code.
 * 
 * Contents:
 *     1. HMM alignment algorithms
 *     2. Traceback algorithms.
 *     3. Unit tests.
 *     4. Test driver.
 * 
 * SRE, Tue Jan 30 10:49:43 2007 [at Einstein's in St. Louis]
 * SVN $Id$
 */

#include "p7_config.h"

#include <easel.h>
#include <esl_alphabet.h>

#include "hmmer.h"


/*****************************************************************
 * 1. HMM alignment algorithms
 *****************************************************************/

/* Function:  p7_GViterbi()
 * Synopsis:  The Viterbi algorithm.
 * Incept:    SRE, Tue Jan 30 10:50:53 2007 [Einstein's, St. Louis]
 * 
 * Purpose:   The standard Viterbi dynamic programming algorithm. 
 *
 *            Given a digital sequence <dsq> of length <L>, a profile
 *            <gm>, and DP matrix <gm> allocated for at least <gm->M>
 *            by <L> cells; calculate the maximum scoring path by
 *            Viterbi; return the Viterbi score in <ret_sc>, and the
 *            Viterbi matrix is in <mx>.
 *            
 *            The caller may then retrieve the Viterbi path by calling
 *            <p7_GTrace()>.
 *           
 *            The Viterbi score is in internal SILO form.  To convert
 *            to a bitscore, the caller needs to subtract a null model
 *            SILO score, then convert to bits with
 *            <p7_SILO2Bitscore()>.
 *           
 * Args:      dsq    - sequence in digitized form, 1..L
 *            L      - length of dsq
 *            gm     - profile. Does not need to contain any
 *                     reference pointers (alphabet, HMM, or null model)
 *            mx     - DP matrix with room for an MxL alignment
 *            ret_sc - RETURN: Viterbi score in bits.
 *           
 * Return:   <eslOK> on success.
 */
int
p7_GViterbi(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_GMX *mx, int *ret_sc)
{
  int **xmx;
  int **mmx;
  int **imx;
  int **dmx;
  int   i,k;
  int   sc;

  /* Some convenience */
  xmx = mx->xmx;
  mmx = mx->mmx;
  imx = mx->imx;
  dmx = mx->dmx;

  /* Initialization of the zero row.  */
  xmx[0][p7_XMN] = 0;		                     /* S->N, p=1            */
  xmx[0][p7_XMB] = gm->xsc[p7_XTN][p7_MOVE];         /* S->N->B, no N-tail   */
  xmx[0][p7_XME] = xmx[0][p7_XMC] = xmx[0][p7_XMJ] = p7_IMPOSSIBLE;  /* need seq to get here */
  for (k = 0; k <= gm->M; k++)
    mmx[0][k] = imx[0][k] = dmx[0][k] = p7_IMPOSSIBLE;      /* need seq to get here */

  /* Recursion. Done as a pull.
   * Note some slightly wasteful boundary conditions:  
   *    tsc[0] = impossible for all eight transitions (no node 0)
   *    I_M is wastefully calculated (doesn't exist)
   *    D_M is wastefully calculated (provably can't appear in a Viterbi path)
   */
  for (i = 1; i <= L; i++) {
    mmx[i][0] = imx[i][0] = dmx[i][0] = p7_IMPOSSIBLE;

    for (k = 1; k <= gm->M; k++) {
				/* match state */
      mmx[i][k]  = p7_IMPOSSIBLE;
      if ((sc = mmx[i-1][k-1] + gm->tsc[p7_TMM][k-1]) > mmx[i][k])
	mmx[i][k] = sc;
      if ((sc = imx[i-1][k-1] + gm->tsc[p7_TIM][k-1]) > mmx[i][k])
	mmx[i][k] = sc;
      if ((sc = xmx[i-1][p7_XMB] + gm->bsc[k]) > mmx[i][k])
	mmx[i][k] = sc;
      if ((sc = dmx[i-1][k-1] + gm->tsc[p7_TDM][k-1]) > mmx[i][k])
	mmx[i][k] = sc;
      if (gm->msc[dsq[i]][k] != p7_IMPOSSIBLE) mmx[i][k] += gm->msc[dsq[i]][k];
      else                                     mmx[i][k] =  p7_IMPOSSIBLE;

				/* delete state */
      dmx[i][k] = p7_IMPOSSIBLE;
      if ((sc = mmx[i][k-1] + gm->tsc[p7_TMD][k-1]) > dmx[i][k])
	dmx[i][k] = sc;
      if ((sc = dmx[i][k-1] + gm->tsc[p7_TDD][k-1]) > dmx[i][k])
	dmx[i][k] = sc;

				/* insert state */
      if (k < gm->M) {
	imx[i][k] = p7_IMPOSSIBLE;
	if ((sc = mmx[i-1][k] + gm->tsc[p7_TMI][k]) > imx[i][k])
	  imx[i][k] = sc;
	if ((sc = imx[i-1][k] + gm->tsc[p7_TII][k]) > imx[i][k])
	  imx[i][k] = sc;

	if (gm->isc[dsq[i]][k] != p7_IMPOSSIBLE) 
	  imx[i][k] += gm->isc[dsq[i]][k];
	else
	  imx[i][k] = p7_IMPOSSIBLE;   
      }
    }

    /* Now the special states. Order is important here.
     * remember, N, C and J emissions are zero score by definition.
     */
				/* N state */
    xmx[i][p7_XMN] = p7_IMPOSSIBLE;
    if ((sc = xmx[i-1][p7_XMN] + gm->xsc[p7_XTN][p7_LOOP]) > p7_IMPOSSIBLE)
      xmx[i][p7_XMN] = sc;

				/* E state */
    /* We don't need to check D_M->E transition; it provably cannot
     * be used in a Viterbi path in HMMER3 parameterization */
    xmx[i][p7_XME] = p7_IMPOSSIBLE;
    for (k = 1; k <= gm->M; k++)
      if ((sc =  mmx[i][k] + gm->esc[k]) > xmx[i][p7_XME])
	xmx[i][p7_XME] = sc;
				/* J state */
    xmx[i][p7_XMJ] = p7_IMPOSSIBLE;
    if ((sc = xmx[i-1][p7_XMJ] + gm->xsc[p7_XTJ][p7_LOOP]) > p7_IMPOSSIBLE)
      xmx[i][p7_XMJ] = sc;
    if ((sc = xmx[i][p7_XME]   + gm->xsc[p7_XTE][p7_LOOP]) > xmx[i][p7_XMJ])
      xmx[i][p7_XMJ] = sc;

				/* B state */
    xmx[i][p7_XMB] = p7_IMPOSSIBLE;
    if ((sc = xmx[i][p7_XMN] + gm->xsc[p7_XTN][p7_MOVE]) > p7_IMPOSSIBLE)
      xmx[i][p7_XMB] = sc;
    if ((sc = xmx[i][p7_XMJ] + gm->xsc[p7_XTJ][p7_MOVE]) > xmx[i][p7_XMB])
      xmx[i][p7_XMB] = sc;

				/* C state */
    xmx[i][p7_XMC] = p7_IMPOSSIBLE;
    if ((sc = xmx[i-1][p7_XMC] + gm->xsc[p7_XTC][p7_LOOP]) > p7_IMPOSSIBLE)
      xmx[i][p7_XMC] = sc;
    if ((sc = xmx[i][p7_XME] + gm->xsc[p7_XTE][p7_MOVE]) > xmx[i][p7_XMC])
      xmx[i][p7_XMC] = sc;
  }
				/* T state (not stored) */
  sc = xmx[L][p7_XMC] + gm->xsc[p7_XTC][p7_MOVE];

  *ret_sc = sc;
  return eslOK;
}




/* Function:  p7_GForward()
 * Synopsis:  The Forward algorithm.
 * Incept:    SRE, Mon Apr 16 13:57:35 2007 [Janelia]
 *
 * Purpose:   The Forward dynamic programming algorithm. 
 *
 *            Given a digital sequence <dsq> of length <L>, a profile
 *            <gm>, and DP matrix <gm> allocated for at least <gm->M>
 *            by <L> cells; calculate the probability of the sequence
 *            given the model using the Forward algorithm; return the
 *            Forward matrix in <mx>, and the Forward score in <ret_sc>.
 *           
 *            The Forward score is in internal SILO form.  To convert
 *            to a bitscore, the caller needs to subtract a null model
 *            SILO score, then convert to bits with
 *            <p7_SILO2Bitscore()>.
 *           
 * Args:      dsq    - sequence in digitized form, 1..L
 *            L      - length of dsq
 *            gm     - profile. Does not need to contain any
 *                     reference pointers (alphabet, HMM, or null model)
 *            mx     - DP matrix with room for an MxL alignment
 *            ret_sc - RETURN: Forward SILO score.
 *           
 * Return:    <eslOK> on success.
 */
int
p7_GForward(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_GMX *mx, int *ret_sc)
{
  int **xmx;
  int **mmx;
  int **imx;
  int **dmx;
  int   i,k;
  int   sc;

  /* Some convenience */
  xmx = mx->xmx;
  mmx = mx->mmx;
  imx = mx->imx;
  dmx = mx->dmx;

  /* Initialization of the zero row.
   * Note that xmx[i][stN] = 0 by definition for all i,
   *    and xmx[i][stT] = xmx[i][stC], so neither stN nor stT need
   *    to be calculated in DP matrices.
   */
  xmx[0][p7_XMN] = 0;		                     /* S->N, p=1            */
  xmx[0][p7_XMB] = gm->xsc[p7_XTN][p7_MOVE];                 /* S->N->B, no N-tail   */
  xmx[0][p7_XME] = xmx[0][p7_XMC] = xmx[0][p7_XMJ] = p7_IMPOSSIBLE;  /* need seq to get here */
  for (k = 0; k <= gm->M; k++)
    mmx[0][k] = imx[0][k] = dmx[0][k] = p7_IMPOSSIBLE;      /* need seq to get here */

  /* Recursion. Done as a pull.
   * Note some slightly wasteful boundary conditions:  
   *    tsc[0] = impossible for all eight transitions (no node 0)
   *    I_M is wastefully calculated (doesn't exist)
   */
  for (i = 1; i <= L; i++) {
    mmx[i][0] = imx[i][0] = dmx[i][0] = p7_IMPOSSIBLE;
    
   for (k = 1; k <= gm->M; k++)
     {
       /* match state */
       mmx[i][k]  = p7_ILogsum(p7_ILogsum(mmx[i-1][k-1] + gm->tsc[p7_TMM][k-1],
					  imx[i-1][k-1] + gm->tsc[p7_TIM][k-1]),
			       p7_ILogsum(xmx[i-1][p7_XMB] + gm->bsc[k],
					  dmx[i-1][k-1] + gm->tsc[p7_TDM][k-1]));
       mmx[i][k] += gm->msc[dsq[i]][k];
       if (mmx[i][k] < p7_IMPOSSIBLE) mmx[i][k] = p7_IMPOSSIBLE;

       dmx[i][k]  = p7_ILogsum(mmx[i][k-1] + gm->tsc[p7_TMD][k-1],
			       dmx[i][k-1] + gm->tsc[p7_TDD][k-1]);
       if (dmx[i][k] < p7_IMPOSSIBLE) dmx[i][k] = p7_IMPOSSIBLE;

       imx[i][k]  = p7_ILogsum(mmx[i-1][k] + gm->tsc[p7_TMI][k],
			       imx[i-1][k] + gm->tsc[p7_TII][k]);
       imx[i][k] += gm->isc[dsq[i]][k];
       if (imx[i][k] < p7_IMPOSSIBLE) imx[i][k] = p7_IMPOSSIBLE;
     }

   /* Now the special states.
    * remember, C and J emissions are zero score by definition
    */
   xmx[i][p7_XMN] = xmx[i-1][p7_XMN] + gm->xsc[p7_XTN][p7_LOOP];
   if (xmx[i][p7_XMN] < p7_IMPOSSIBLE) xmx[i][p7_XMN] = p7_IMPOSSIBLE;

   xmx[i][p7_XME] = p7_IMPOSSIBLE;
   for (k = 1; k <= gm->M; k++)
     xmx[i][p7_XME] = p7_ILogsum(xmx[i][p7_XME], mmx[i][k] + gm->esc[k]);
   if (xmx[i][p7_XME] < p7_IMPOSSIBLE) xmx[i][p7_XME] = p7_IMPOSSIBLE;

   xmx[i][p7_XMJ] = p7_ILogsum(xmx[i-1][p7_XMJ] + gm->xsc[p7_XTJ][p7_LOOP],
			       xmx[i][p7_XME]   + gm->xsc[p7_XTE][p7_LOOP]);
   if (xmx[i][p7_XMJ] < p7_IMPOSSIBLE) xmx[i][p7_XMJ] = p7_IMPOSSIBLE;

   xmx[i][p7_XMB] = p7_ILogsum(xmx[i][p7_XMN] + gm->xsc[p7_XTN][p7_MOVE],
			       xmx[i][p7_XMJ] + gm->xsc[p7_XTJ][p7_MOVE]);
   if (xmx[i][p7_XMB] < p7_IMPOSSIBLE) xmx[i][p7_XMB] = p7_IMPOSSIBLE;

   xmx[i][p7_XMC] = p7_ILogsum(xmx[i-1][p7_XMC] + gm->xsc[p7_XTC][p7_LOOP],
			       xmx[i][p7_XME] + gm->xsc[p7_XTE][p7_MOVE]);
   if (xmx[i][p7_XMC] < p7_IMPOSSIBLE) xmx[i][p7_XMC] = p7_IMPOSSIBLE;

  }
  
  sc = xmx[L][p7_XMC] + gm->xsc[p7_XTC][p7_MOVE];
  if (sc < p7_IMPOSSIBLE) sc = p7_IMPOSSIBLE;
  *ret_sc = sc;
  return eslOK;
}


/* Function:  p7_GHybrid()
 * Synopsis:  The "hybrid" algorithm.
 * Incept:    SRE, Sat May 19 10:01:46 2007 [Janelia]
 *
 * Purpose:   The profile HMM version of the Hwa "hybrid" alignment
 *            algorithm \citep{YuHwa02}. The "hybrid" score is the
 *            maximum score in the Forward matrix. 
 *            
 *            Given a digital sequence <dsq> of length <L>, a profile
 *            <gm>, and DP matrix <mx> allocated for at least <gm->M>
 *            by <L> cells; calculate the probability of the sequence
 *            given the model using the Forward algorithm; return
 *            the calculated Forward matrix in <mx>, and optionally
 *            return the Forward score in <opt_fwdscore> and/or the
 *            Hybrid score in <opt_hybscore>.
 *           
 *            This is implemented as a wrapper around <p7_GForward()>.
 *            The Forward matrix and the Forward score obtained from
 *            this routine are identical to what <p7_GForward()> would
 *            return.
 *           
 *            The scores are returned in internal SILO form.  To
 *            convert to a bitscore, the caller needs to subtract a
 *            null model SILO score, then convert to bits with
 *            <p7_SILO2Bitscore()>.
 *           
 * Args:      dsq          - sequence in digitized form, 1..L
 *            L            - length of dsq
 *            gm           - profile. Does not need to contain any
 *                           reference pointers (alphabet, HMM, or null model)
 *            mx           - DP matrix with room for an MxL alignment
 *            opt_fwdscore - optRETURN: Forward score, SILO.
 *            opt_hybscore - optRETURN: Hybrid score, SILO. 
 *
 * Returns:   <eslOK> on success, and results are in <mx>, <opt_fwdscore>,
 *            and <opt_hybscore>.
 */
int
p7_GHybrid(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, P7_GMX *mx, int *opt_fwdscore, int *opt_hybscore)
{
  int status;
  int F,H;
  int i,k;

  if ((status = p7_GForward(dsq, L, gm, mx, &F)) != eslOK)  goto ERROR;

  H = p7_IMPOSSIBLE;
  for (i = 1; i <= L; i++)
    for (k = 1 ; k <= gm->M; k++)
      if (mx->mmx[i][k] > H) H = mx->mmx[i][k];
  
  if (opt_fwdscore != NULL) *opt_fwdscore = F;
  if (opt_hybscore != NULL) *opt_hybscore = H;
  return eslOK;

 ERROR:
  if (opt_fwdscore != NULL) *opt_fwdscore = 0;
  if (opt_hybscore != NULL) *opt_hybscore = 0;
  return status;
}


/*****************************************************************
 * 2. Traceback algorithms
 *****************************************************************/

/* Function: p7_GTrace()
 * Incept:   SRE, Thu Feb  1 10:25:56 2007 [UA 8018 St. Louis to Dulles]
 * 
 * Purpose:  Traceback of a Viterbi matrix: retrieval 
 *           of optimum alignment.
 *           
 * Args:     dsq    - sequence aligned to (digital form) 1..L 
 *           L      - length of dsq gm    
 *           gm     - profile model; does not need any ref ptrs
 *           mx     - the matrix to trace, L x M
 *           tr     - storage for the recovered traceback.
 *           
 * Return:   <eslOK> on success.
 *           <eslFAIL> if even the optimal path has zero probability;
 *           in this case, the trace is set blank (<tr->N = 0>).
 */
int
p7_GTrace(const ESL_DSQ *dsq, int L, const P7_PROFILE *gm, const P7_GMX *mx, P7_TRACE *tr)
{
  int status;
  int i;			/* position in seq (1..L) */
  int k;			/* position in model (1..M) */
  int **xmx, **mmx, **imx, **dmx;
  int sc;			/* temp var for pre-emission score */

  if ((status = p7_trace_Reuse(tr)) != eslOK) goto ERROR;
  xmx = mx->xmx;
  mmx = mx->mmx;
  imx = mx->imx;
  dmx = mx->dmx;

  /* Initialization.
   * (back to front. ReverseTrace() called later.)
   */
  if ((status = p7_trace_Append(tr, p7_STT, 0, 0)) != eslOK) goto ERROR;
  if ((status = p7_trace_Append(tr, p7_STC, 0, 0)) != eslOK) goto ERROR;
  i    = L;			/* next position to explain in seq */

  /* Traceback
   */
  while (tr->st[tr->N-1] != p7_STS) {
    switch (tr->st[tr->N-1]) {
    case p7_STC:		/* C(i) comes from C(i-1) or E(i) */
      if   (xmx[i][p7_XMC] <= p7_IMPOSSIBLE)
	ESL_XEXCEPTION(eslFAIL, "impossible C reached at i=%d", i);

      if (xmx[i][p7_XMC] == xmx[i-1][p7_XMC] + gm->xsc[p7_XTC][p7_LOOP]) {
	tr->i[tr->N-1]    = i--;  /* first C doesn't emit: subsequent ones do */
	status = p7_trace_Append(tr, p7_STC, 0, 0);
      } else if (xmx[i][p7_XMC] == xmx[i][p7_XME] + gm->xsc[p7_XTE][p7_MOVE]) 
	status = p7_trace_Append(tr, p7_STE, 0, 0);
      else ESL_XEXCEPTION(eslFAIL, "C at i=%d couldn't be traced", i);
      break;

    case p7_STE:		/* E connects from any M state. k set here */
      if (xmx[i][p7_XME] <= p7_IMPOSSIBLE) 
	ESL_XEXCEPTION(eslFAIL, "impossible E reached at i=%d", i);

      for (k = gm->M; k >= 1; k--)
	if (xmx[i][p7_XME] == mmx[i][k] + gm->esc[k]) {
	  status = p7_trace_Append(tr, p7_STM, k, i);
	  break;
	}
      if (k < 0) ESL_XEXCEPTION(eslFAIL, "E at i=%d couldn't be traced", i);
      break;

    case p7_STM:			/* M connects from i-1,k-1, or B */
      sc = mmx[i][k] - gm->msc[dsq[i]][k];
      if (sc <= p7_IMPOSSIBLE) ESL_XEXCEPTION(eslFAIL, "impossible M reached at k=%d,i=%d", k,i);

      if      (sc == xmx[i-1][p7_XMB] + gm->bsc[k])        status = p7_trace_Append(tr, p7_STB, 0,   0);
      else if (sc == mmx[i-1][k-1] + gm->tsc[p7_TMM][k-1]) status = p7_trace_Append(tr, p7_STM, k-1, i-1);
      else if (sc == imx[i-1][k-1] + gm->tsc[p7_TIM][k-1]) status = p7_trace_Append(tr, p7_STI, k-1, i-1);
      else if (sc == dmx[i-1][k-1] + gm->tsc[p7_TDM][k-1]) status = p7_trace_Append(tr, p7_STD, k-1, 0);
      else ESL_XEXCEPTION(eslFAIL, "M at k=%d,i=%d couldn't be traced", k,i);
      if (status != eslOK) goto ERROR;
      k--; 
      i--;
      break;

    case p7_STD:			/* D connects from M,D at i,k-1 */
      if (dmx[i][k] <= p7_IMPOSSIBLE) ESL_XEXCEPTION(eslFAIL, "impossible D reached at k=%d,i=%d", k,i);

      if      (dmx[i][k] == mmx[i][k-1] + gm->tsc[p7_TMD][k-1]) status = p7_trace_Append(tr, p7_STM, k-1, i);
      else if (dmx[i][k] == dmx[i][k-1] + gm->tsc[p7_TDD][k-1]) status = p7_trace_Append(tr, p7_STD, k-1, 0);
      else ESL_XEXCEPTION(eslFAIL, "D at k=%d,i=%d couldn't be traced", k,i);
      if (status != eslOK) goto ERROR;
      k--;
      break;

    case p7_STI:			/* I connects from M,I at i-1,k*/
      sc = imx[i][k] - gm->isc[dsq[i]][k];
      if (sc <= p7_IMPOSSIBLE) ESL_XEXCEPTION(eslFAIL, "impossible I reached at k=%d,i=%d", k,i);

      if      (sc == mmx[i-1][k] + gm->tsc[p7_TMI][k]) status = p7_trace_Append(tr, p7_STM, k, i-1);
      else if (sc == imx[i-1][k] + gm->tsc[p7_TII][k]) status = p7_trace_Append(tr, p7_STI, k, i-1);
      else ESL_XEXCEPTION(eslFAIL, "I at k=%d,i=%d couldn't be traced", k,i);
      if (status != eslOK) goto ERROR;
      i--;
      break;

    case p7_STN:			/* N connects from S, N */
      if (xmx[i][p7_XMN] <= p7_IMPOSSIBLE) ESL_XEXCEPTION(eslFAIL, "impossible N reached at i=%d", i);

      if      (i == 0 && xmx[i][p7_XMN] == 0) 
	status = p7_trace_Append(tr, p7_STS, 0, 0);
      else if (i > 0  && xmx[i][p7_XMN] == xmx[i-1][p7_XMN] + gm->xsc[p7_XTN][p7_LOOP]) {
	tr->i[tr->N-1] = i--;
	status = p7_trace_Append(tr, p7_STN, 0, 0);
      } else ESL_XEXCEPTION(eslFAIL, "N at i=%d couldn't be traced", i);
      if (status != eslOK) goto ERROR;
      break;

    case p7_STB:			/* B connects from N, J */
      if (xmx[i][p7_XMB] <= p7_IMPOSSIBLE) ESL_XEXCEPTION(eslFAIL, "impossible B reached at i=%d", i);

      if (xmx[i][p7_XMB] == xmx[i][p7_XMN] + gm->xsc[p7_XTN][p7_MOVE]) 
	status = p7_trace_Append(tr, p7_STN, 0, 0);
      else if (xmx[i][p7_XMB] == xmx[i][p7_XMJ] + gm->xsc[p7_XTJ][p7_MOVE])
	status = p7_trace_Append(tr, p7_STJ, 0, 0);
      else  ESL_XEXCEPTION(eslFAIL, "B at i=%d couldn't be traced", i);
      break;

    case p7_STJ:			/* J connects from E(i) or J(i-1) */
      if (xmx[i][p7_XMJ] <= p7_IMPOSSIBLE) ESL_XEXCEPTION(eslFAIL, "impossible J reached at i=%d", i);

      if (xmx[i][p7_XMJ] == xmx[i-1][p7_XMJ] + gm->xsc[p7_XTJ][p7_LOOP]) {
	tr->i[tr->N-1] = i--;
	status = p7_trace_Append(tr, p7_STJ, 0, 0);
      } else if (xmx[i][p7_XMJ] == xmx[i][p7_XME] + gm->xsc[p7_XTE][p7_LOOP])
	status = p7_trace_Append(tr, p7_STE, 0, 0);
      else  ESL_XEXCEPTION(eslFAIL, "J at i=%d couldn't be traced", i);
      break;

    default: ESL_XEXCEPTION(eslFAIL, "bogus state in traceback");
    } /* end switch over statetype[tpos-1] */

    if (status != eslOK) goto ERROR;
  } /* end traceback, at S state */

  if ((status = p7_trace_Reverse(tr)) != eslOK) goto ERROR;
  return eslOK;

 ERROR:
  return status;
}



/*****************************************************************
 * 3. Unit tests.
 *****************************************************************/
#ifdef p7DP_SLOW_TESTDRIVE

/* Viterbi validation is done by comparing the returned score
 * to the score of the optimal trace. Not foolproof, but catches
 * many kinds of errors.
 * 
 * Another check is that the average score should be <= 0,
 * since the random sequences are drawn from the null model.
 */ 
static void
utest_viterbi(ESL_RANDOMNESS *r, ESL_ALPHABET *abc, P7_PROFILE *gm, int nseq, int L)
{
  int       status;
  char      errbuf[eslERRBUFSIZE];
  ESL_DSQ  *dsq = NULL;
  P7_GMX   *mx  = NULL;
  P7_TRACE *tr  = NULL;
  int       idx;
  int       sc1, sc2;
  double    avg_sc = 0.;
  
  if ((dsq    = malloc(sizeof(ESL_DSQ) *(L+2))) == NULL)  esl_fatal("malloc failed");
  if ((status = p7_trace_Create(L, &tr))        != eslOK) esl_fatal("trace creation failed");
  if ((mx     = p7_gmx_Create(gm->M, L))        == NULL)  esl_fatal("matrix creation failed");

  for (idx = 0; idx < nseq; idx++)
    {
      if (esl_rnd_xfIID(r, gm->bg->f, abc->K, L, dsq) != eslOK) esl_fatal("seq generation failed");
      if (p7_GViterbi(dsq, L, gm, mx, &sc1)           != eslOK) esl_fatal("viterbi failed");
      if (p7_GTrace  (dsq, L, gm, mx, tr)             != eslOK) esl_fatal("trace failed");
      if (p7_trace_Validate(tr, abc, dsq, errbuf)     != eslOK) esl_fatal("trace invalid:\n%s", errbuf);
      if (p7_trace_Score(tr, dsq, gm, &sc2)           != eslOK) esl_fatal("trace score failed");

      if (sc1 != sc2) esl_fatal("Trace score not equal to Viterbi score");

      /* avg_sc += p7_SILO2Bitscore(sc1); */
      avg_sc += (((double) sc1 / p7_INTSCALE) - L * log(gm->bg->p1) - log(1.-gm->bg->p1)) / eslCONST_LOG2;
    }

  avg_sc /= (double) nseq;
  if (avg_sc > 0.) esl_fatal("Viterbi scores have positive expectation (%f bits)", avg_sc);

  p7_gmx_Destroy(mx);
  p7_trace_Destroy(tr);
  free(dsq);
  return;
}

/* Forward is harder to validate. 
 * We do know that the Forward score is >= Viterbi.
 * We also know that the expected score on random seqs is <= 0.
 */
static void
utest_forward(ESL_RANDOMNESS *r, ESL_ALPHABET *abc, P7_PROFILE *gm, int nseq, int L)
{
  ESL_DSQ  *dsq = NULL;
  P7_GMX   *mx  = NULL;
  P7_TRACE *tr  = NULL;
  int       idx;
  int       vsc, fsc;
  double    avg_sc = 0.;

  if ((dsq    = malloc(sizeof(ESL_DSQ) *(L+2))) == NULL)  esl_fatal("malloc failed");
  if ((mx     = p7_gmx_Create(gm->M, L))        == NULL)  esl_fatal("matrix creation failed");

  for (idx = 0; idx < nseq; idx++)
    {
      if (esl_rnd_xfIID(r, gm->bg->f, abc->K, L, dsq) != eslOK) esl_fatal("seq generation failed");
      if (p7_GViterbi(dsq, L, gm, mx, &vsc)           != eslOK) esl_fatal("viterbi failed");
      if (p7_GForward(dsq, L, gm, mx, &fsc)           != eslOK) esl_fatal("forward failed");
      if (fsc < vsc) esl_fatal("Forward score can't be less than Viterbi score");

      /* avg_sc += p7_SILO2Bitscore(fsc);*/
      avg_sc +=  (((double) fsc / p7_INTSCALE) - L * log(gm->bg->p1) - log(1.-gm->bg->p1)) / eslCONST_LOG2;
    }

  avg_sc /= (double) nseq;
  if (avg_sc > 0.) esl_fatal("Forward scores have positive expectation (%f bits)", avg_sc);

  p7_gmx_Destroy(mx);
  p7_trace_Destroy(tr);
  free(dsq);
  return;
}

/* The "enumeration" test samples a random enumerable HMM (transitions to insert are 0,
 * so the generated seq space only includes seqs of L<=M). 
 *
 * The test scores all seqs of length <=M by both Viterbi and Forward, verifies that 
 * the two scores are identical, and verifies that the sum of all the probabilities is
 * 1.0. It also verifies that the score of a sequence of length M+1 is indeed <P7_IMPOSSIBLE>.
 * 
 * Because this function is going to work in unscaled probabilities, adding them up,
 * all P(seq) terms must be >> DBL_EPSILON.  That means M must be small; on the order 
 * of <= 10. 
 */
static void
utest_Enumeration(ESL_RANDOMNESS *r, ESL_ALPHABET *abc, int M)
{
  char            errbuf[eslERRBUFSIZE];
  P7_HMM         *hmm  = NULL;
  P7_BG          *bg   = NULL;
  ESL_DSQ        *dsq  = NULL;
  P7_GMX         *mx   = NULL;
  int    vsc, fsc;
  double bg_ll;   		/* log P(seq | bg) */
  double vp, fp;		/* P(seq,\pi | model) and P(seq | model) */
  int L;
  int i;
  double total_p;
  char   *seq;
    
  /* Sample an enumerable HMM & profile of length M.
   */
  if (p7_hmm_SampleEnumerable(r, M, abc, &hmm)        != eslOK) esl_fatal("failed to sample an enumerable HMM");
  if ((bg = p7_bg_Create(abc))                        == NULL)  esl_fatal("failed to create null model");
  if ((hmm->gm = p7_profile_Create(hmm->M, abc))      == NULL)  esl_fatal("failed to create profile");
  if (p7_ProfileConfig(hmm, bg, hmm->gm, p7_UNILOCAL) != eslOK) esl_fatal("failed to config profile");
  if (p7_ReconfigLength(hmm->gm, 0)                   != eslOK) esl_fatal("failed to config profile length");
  if (p7_hmm_Validate    (hmm,     0.0001, errbuf)    != eslOK) esl_fatal("whoops, HMM is bad!");
  if (p7_profile_Validate(hmm->gm, 0.0001)            != eslOK) esl_fatal("whoops, profile is bad!");

  if (  (dsq = malloc(sizeof(ESL_DSQ) * (M+3)))    == NULL)  esl_fatal("allocation failed");
  if (  (seq = malloc(sizeof(char)    * (M+2)))    == NULL)  esl_fatal("allocation failed");
  if ((mx     = p7_gmx_Create(hmm->M, M+3))         == NULL)  esl_fatal("matrix creation failed");

  /* Enumerate all sequences of length L <= M
   */
  total_p = 0;
  for (L = 0; L <= M; L++)
    {
      /* Initialize dsq of length L at 0000... */
      dsq[0] = dsq[L+1] = eslDSQ_SENTINEL;
      for (i = 1; i <= L; i++) dsq[i] = 0;

      while (1) 		/* enumeration of seqs of length L*/
	{
	  if (p7_GViterbi(dsq, L, hmm->gm, mx, &vsc)  != eslOK) esl_fatal("viterbi failed");
	  if (p7_GForward(dsq, L, hmm->gm, mx, &fsc)  != eslOK) esl_fatal("forward failed");
 
	  /* calculate bg log likelihood component of the scores */
	  for (bg_ll = 0., i = 1; i <= L; i++)  bg_ll += log(hmm->bg->f[dsq[i]]);
	  
	  /* convert the scores back to probabilities, adding the bg LL back to the LLR */
	  vp =  exp ((double)(vsc / p7_INTSCALE) + bg_ll);
	  fp =  exp ((double)(fsc / p7_INTSCALE) + bg_ll);

	  /*
	  esl_abc_Textize(abc, dsq, L, seq);
	  printf("probability of sequence: %10s   %16g  (SILO v=%6d f=%6d)\n", seq, fp, vsc, fsc);
	  */
	  total_p += fp;

	  /* Increment dsq like a reversed odometer */
	  for (i = 1; i <= L; i++) 
	    if (dsq[i] < abc->K-1) { dsq[i]++; break; } else { dsq[i] = 0; }
	  if (i > L) break;	/* we're done enumerating sequences */
	}
    }

  /* That sum is subject to a large amount of numerical error because of integer roundoff, etc;
   * don't expect it to be too close.
   */
  if (total_p < 0.8 || total_p > 1.2) esl_fatal("Enumeration unit test failed: total Forward p isn't near 1.0 (%g)", total_p);
  printf("total p is %g\n", total_p);
  
  p7_gmx_Destroy(mx);
  p7_bg_Destroy(bg);
  p7_profile_Destroy(hmm->gm);
  p7_hmm_Destroy(hmm);
  free(dsq);
}

#endif /*p7DP_SLOW_TESTDRIVE*/




/*****************************************************************
 * 3. Test driver.
 *****************************************************************/
/* gcc -g -Wall -Dp7DP_SLOW_TESTDRIVE -I. -I../easel -L. -L../easel -o dp_slow_utest dp_slow.c -lhmmer -leasel -lm
 */
#ifdef p7DP_SLOW_TESTDRIVE
#include "easel.h"

#include "p7_config.h"
#include "hmmer.h"

int
main(int argc, char **argv)
{
  char            errbuf[eslERRBUFSIZE];
  ESL_RANDOMNESS *r    = NULL;
  ESL_ALPHABET   *abc  = NULL;
  P7_HMM         *hmm  = NULL;
  P7_BG          *bg   = NULL;
  int             M    = 100;
  int             L    = 200;
  int             nseq = 20;

  if ((r   = esl_randomness_CreateTimeseeded())    == NULL)  esl_fatal("failed to create rng");
  if ((abc = esl_alphabet_Create(eslAMINO))        == NULL)  esl_fatal("failed to create alphabet");

  if (p7_hmm_Sample(r, M, abc, &hmm)                  != eslOK) esl_fatal("failed to sample an HMM");
  if ((bg = p7_bg_Create(abc))                        == NULL)  esl_fatal("failed to create null model");
  if ((hmm->gm = p7_profile_Create(hmm->M, abc))      == NULL)  esl_fatal("failed to create profile");
  if (p7_ProfileConfig(hmm, bg, hmm->gm, p7_UNILOCAL) != eslOK) esl_fatal("failed to config profile");
  if (p7_ReconfigLength(hmm->gm, L)                   != eslOK) esl_fatal("failed to config profile length");
  if (p7_hmm_Validate    (hmm,     0.0001, errbuf)    != eslOK) esl_fatal("whoops, HMM is bad!");
  if (p7_profile_Validate(hmm->gm, 0.0001)            != eslOK) esl_fatal("whoops, profile is bad!");

  utest_Enumeration(r, abc, 4);	/* can't go much higher than 5; enumeration test is cpu-intensive. */
  utest_viterbi(r, abc, hmm->gm, nseq, L);
  utest_forward(r, abc, hmm->gm, nseq, L);
  
  p7_profile_Destroy(hmm->gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  esl_randomness_Destroy(r);
  return 0;
}

#endif /*p7DP_SLOW_TESTDRIVE*/

/*****************************************************************
 * @LICENSE@
 *****************************************************************/
