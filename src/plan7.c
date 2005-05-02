/* plan7.c
 * Support for Plan 7 HMM data structure, plan7_s.
 * 
 * SVN $Id$
 * SRE, Sat Nov 16 14:19:56 1996
 */

#include "config.h"		/* must be included first */
#include "squidconf.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "squid.h"
#include "funcs.h"
#include "structs.h"
#include "plan7.h"		/* the model structure */



/* Functions: AllocPlan7(), AllocPlan7Shell(), AllocPlan7Body(), FreePlan7()
 * 
 * Purpose:   Allocate or free a Plan7 HMM structure.
 *            Can either allocate all at one (AllocPlan7()) or
 *            in two steps (AllocPlan7Shell(), AllocPlan7Body()).
 *            The two step method is used in hmmio.c where we start
 *            parsing the header of an HMM file but don't 
 *            see the size of the model 'til partway thru the header.
 */
struct plan7_s *
AllocPlan7(int M) 
{
  struct plan7_s *hmm;

  hmm = AllocPlan7Shell();
  AllocPlan7Body(hmm, M);
  return hmm;
}  
struct plan7_s *
AllocPlan7Shell(void) 
{
  struct plan7_s *hmm;

  hmm    = (struct plan7_s *) MallocOrDie (sizeof(struct plan7_s));

  hmm->M    = 0;
  hmm->t    = NULL;
  hmm->mat  = NULL;
  hmm->ins  = NULL;

  hmm->t2     = NULL;
  hmm->begin  = NULL;
  hmm->end    = NULL;

  hmm->tsc     = hmm->msc     = hmm->isc     = NULL;
  hmm->tsc_mem = hmm->msc_mem = hmm->msc_mem = NULL;
  hmm->bsc = hmm->bsc_mem = NULL;
  hmm->esc = hmm->esc_mem = NULL;

  hmm->name     = NULL;
  hmm->acc      = NULL;
  hmm->desc     = NULL;
  hmm->rf       = NULL;
  hmm->cs       = NULL;
  hmm->ca       = NULL;
  hmm->comlog   = NULL; 
  hmm->nseq     = 0;
  hmm->ctime    = NULL;
  hmm->map      = NULL;
  hmm->checksum = 0;

  hmm->tpri = NULL;
  hmm->mpri = NULL;
  hmm->ipri = NULL;

  hmm->ga1 = hmm->ga2 = 0.0;
  hmm->tc1 = hmm->tc2 = 0.0;
  hmm->nc1 = hmm->nc2 = 0.0;
				/* DNA translation is not enabled by default */
  hmm->dnam   = NULL;
  hmm->dnai   = NULL;
  hmm->dna2   = -INFTY;
  hmm->dna4   = -INFTY;
			/* statistical parameters set to innocuous empty values */
  hmm->mu     = 0.; 
  hmm->lambda = 0.;
  
  hmm->flags = 0;
  return hmm;
}  
#ifndef ALTIVEC  /* in Altivec port, this func is replaced in fast_algorithms.c */
void
AllocPlan7Body(struct plan7_s *hmm, int M) 
{
  int k, x;

  hmm->M = M;
  hmm->t      = MallocOrDie (M     *           sizeof(float *));
  hmm->mat    = MallocOrDie ((M+1) *           sizeof(float *));
  hmm->ins    = MallocOrDie (M     *           sizeof(float *));
  hmm->t[0]   = MallocOrDie ((7*M)     *       sizeof(float));
  hmm->mat[0] = MallocOrDie ((MAXABET*(M+1)) * sizeof(float));
  hmm->ins[0] = MallocOrDie ((MAXABET*M) *     sizeof(float));
  /* note allocation strategy for important 2D arrays -- trying
   * to keep locality as much as possible, cache efficiency etc.
   */
  for (k = 1; k <= M; k++) {
    hmm->mat[k] = hmm->mat[0] + k * MAXABET;
    if (k < M) {
      hmm->ins[k] = hmm->ins[0] + k * MAXABET;
      hmm->t[k]   = hmm->t[0]   + k * 7;
    }
  }

  hmm->t2     = MallocOrDie  (M     * sizeof(float *));
  hmm->t2[0]  = MallocOrDie  ((7*M) * sizeof(float));
  for (k = 1; k < M; k++) hmm->t2[k] = hmm->t2[0] + k * 7;
  hmm->begin  = MallocOrDie  ((M+1) * sizeof(float));
  hmm->end    = MallocOrDie  ((M+1) * sizeof(float));

  hmm->tsc     = MallocOrDie (7     *           sizeof(int *));
  hmm->msc     = MallocOrDie (MAXCODE   *       sizeof(int *));
  hmm->isc     = MallocOrDie (MAXCODE   *       sizeof(int *)); 
  hmm->tsc_mem = MallocOrDie ((7*M)     *       sizeof(int));
  hmm->msc_mem = MallocOrDie ((MAXCODE*(M+1)) * sizeof(int));
  hmm->isc_mem = MallocOrDie ((MAXCODE*M) *     sizeof(int));
  hmm->tsc[0]  = hmm->tsc_mem;
  hmm->msc[0]  = hmm->msc_mem;
  hmm->isc[0]  = hmm->isc_mem;

  for (x = 1; x < MAXCODE; x++) {
    hmm->msc[x] = hmm->msc[0] + x * (M+1);
    hmm->isc[x] = hmm->isc[0] + x * M;
  }
  for (x = 0; x < 7; x++)
    hmm->tsc[x] = hmm->tsc[0] + x * M;

  /* tsc[x][0] is used as a boundary condition sometimes [Viterbi()],
   * so init (and keep) at -inf always.
   */
  for (x = 0; x < 7; x++)
    hmm->tsc[x][0] = -INFTY;

  hmm->bsc_mem  = MallocOrDie  ((M+1) * sizeof(int));
  hmm->esc_mem  = MallocOrDie  ((M+1) * sizeof(int));
  hmm->bsc = hmm->bsc_mem;
  hmm->esc = hmm->esc_mem;

  hmm->rf     = MallocOrDie ((M+2) * sizeof(char));
  hmm->cs     = MallocOrDie ((M+2) * sizeof(char));
  hmm->ca     = MallocOrDie ((M+2) * sizeof(char));
  hmm->map    = MallocOrDie ((M+1) * sizeof(int));

  return;
}  
#endif /* not the ALTIVEC version */

void
FreePlan7(struct plan7_s *hmm)
{
  if (hmm->mat     != NULL) free(hmm->mat[0]);
  if (hmm->ins     != NULL) free(hmm->ins[0]);
  if (hmm->t       != NULL) free(hmm->t[0]);
  if (hmm->mat     != NULL) free(hmm->mat);
  if (hmm->ins     != NULL) free(hmm->ins);
  if (hmm->t       != NULL) free(hmm->t);
  if (hmm->t2      != NULL) free(hmm->t2[0]);
  if (hmm->t2      != NULL) free(hmm->t2);
  if (hmm->begin   != NULL) free(hmm->begin);
  if (hmm->end     != NULL) free(hmm->end);
  if (hmm->bsc_mem != NULL) free(hmm->bsc_mem);
  if (hmm->esc_mem != NULL) free(hmm->esc_mem);
  if (hmm->msc_mem != NULL) free(hmm->msc_mem);
  if (hmm->isc_mem != NULL) free(hmm->isc_mem);
  if (hmm->tsc_mem != NULL) free(hmm->tsc_mem);
  if (hmm->msc     != NULL) free(hmm->msc);
  if (hmm->isc     != NULL) free(hmm->isc);
  if (hmm->tsc     != NULL) free(hmm->tsc);
  if (hmm->name    != NULL) free(hmm->name);
  if (hmm->acc     != NULL) free(hmm->acc);
  if (hmm->desc    != NULL) free(hmm->desc);
  if (hmm->rf      != NULL) free(hmm->rf);
  if (hmm->cs      != NULL) free(hmm->cs);
  if (hmm->ca      != NULL) free(hmm->ca);
  if (hmm->comlog  != NULL) free(hmm->comlog);
  if (hmm->ctime   != NULL) free(hmm->ctime);
  if (hmm->map     != NULL) free(hmm->map);
  if (hmm->tpri    != NULL) free(hmm->tpri);
  if (hmm->mpri    != NULL) free(hmm->mpri);
  if (hmm->ipri    != NULL) free(hmm->ipri);
  if (hmm->dnam    != NULL) free(hmm->dnam);
  if (hmm->dnai    != NULL) free(hmm->dnai);
  free(hmm);
}

/* Function: ZeroPlan7()
 * 
 * Purpose:  Zeros the counts/probabilities fields in a model (both
 *           core and configured form).  
 *           Leaves null model untouched. 
 */
void
ZeroPlan7(struct plan7_s *hmm)
{
  int k;
  for (k = 1; k < hmm->M; k++)
    {
      FSet(hmm->t[k], 7, 0.);
      FSet(hmm->mat[k], Alphabet_size, 0.);
      FSet(hmm->ins[k], Alphabet_size, 0.);
    }
  FSet(hmm->mat[hmm->M], Alphabet_size, 0.);
  hmm->tbd1 = 0.;
  FSet(hmm->begin+1, hmm->M, 0.);
  FSet(hmm->end+1, hmm->M, 0.);
  for (k = 0; k < 4; k++)
    FSet(hmm->xt[k], 2, 0.);
  hmm->flags &= ~PLAN7_HASBITS;	/* invalidates scores */
  hmm->flags &= ~PLAN7_HASALG;	/* invalidates configuration */
  hmm->flags &= ~PLAN7_HASPROB;	/* invalidates probabilities */
}


/* Function: Plan7SetName()
 * 
 * Purpose:  Change the name of a Plan7 HMM. Convenience function.
 *      
 * Note:     Trailing whitespace and \n's are chopped.     
 */
void
Plan7SetName(struct plan7_s *hmm, char *name)
{
  if (hmm->name != NULL) free(hmm->name);
  hmm->name = Strdup(name);
  StringChop(hmm->name);
}
/* Function: Plan7SetAccession()
 * 
 * Purpose:  Change the accession number of a Plan7 HMM. Convenience function.
 *      
 * Note:     Trailing whitespace and \n's are chopped.     
 */
void
Plan7SetAccession(struct plan7_s *hmm, char *acc)
{
  if (hmm->acc != NULL) free(hmm->acc);
  hmm->acc = Strdup(acc);
  StringChop(hmm->acc);
  hmm->flags |= PLAN7_ACC;
}

/* Function: Plan7SetDescription()
 * 
 * Purpose:  Change the description line of a Plan7 HMM. Convenience function.
 * 
 * Note:     Trailing whitespace and \n's are chopped.
 */
void
Plan7SetDescription(struct plan7_s *hmm, char *desc)
{
  if (hmm->desc != NULL) free(hmm->desc);
  hmm->desc = Strdup(desc);
  StringChop(hmm->desc); 
  hmm->flags |= PLAN7_DESC;
}

/* Function: Plan7ComlogAppend()
 * Date:     SRE, Wed Oct 29 09:57:30 1997 [TWA 721 over Greenland] 
 * 
 * Purpose:  Concatenate command line options and append to the
 *           command line log.
 */
void
Plan7ComlogAppend(struct plan7_s *hmm, int argc, char **argv)
{
  int len;
  int i;

  /* figure out length of command line, w/ spaces and \n */
  len = argc;
  for (i = 0; i < argc; i++)
    len += strlen(argv[i]);

  /* allocate */
  if (hmm->comlog != NULL)
    {
      len += strlen(hmm->comlog);
      hmm->comlog = ReallocOrDie(hmm->comlog, sizeof(char)* (len+1));
    }
  else
    {
      hmm->comlog = MallocOrDie(sizeof(char)* (len+1));
      *(hmm->comlog) = '\0'; /* need this to make strcat work */
    }

  /* append */
  strcat(hmm->comlog, "\n");
  for (i = 0; i < argc; i++)
    {
      strcat(hmm->comlog, argv[i]);
      if (i < argc-1) strcat(hmm->comlog, " ");
    }
}

/* Function: Plan7SetCtime()
 * Date:     SRE, Wed Oct 29 11:53:19 1997 [TWA 721 over the Atlantic]
 * 
 * Purpose:  Set the ctime field in a new HMM to the current time.
 */
void
Plan7SetCtime(struct plan7_s *hmm)
{
  time_t date = time(NULL);
  if (hmm->ctime != NULL) free(hmm->ctime);
  hmm->ctime = Strdup(ctime(&date));
  StringChop(hmm->ctime);
}


/* Function: Plan7SetNullModel()
 * 
 * Purpose:  Set the null model section of an HMM.
 *           Convenience function.
 */
void
Plan7SetNullModel(struct plan7_s *hmm, float null[MAXABET], float p1)
{
  int x;
  for (x = 0; x < Alphabet_size; x++)
    hmm->null[x] = null[x];
  hmm->p1 = p1;
}

/* left_wing_retraction_imposed()
 * 
 * Wing retraction, when the B->M_k entry distribution is imposed by
 * the algorithm (sw, fs modes). No calculation is needed in this
 * case. The D_1 state is simply removed from the model.
 * 
 * bsc[1..M] scores are set. 
 * 
 * xref STL8/91.
 */
static void
left_wing_retraction_imposed(struct plan7_s *hmm)
{
  int k;

  for (k = 1; k <= hmm->M; k++)
    hmm->bsc[k] = Prob2Score(hmm->begin[k], hmm->p1);

  /* Virtual removal of D_1 state. */
  hmm->tsc[TDM][1] = -INFTY;
  hmm->tsc[TDD][1] = -INFTY;
  return;
}

/* left_wing_retraction_added()
 * 
 * Wing retraction, where B->M_k entry distribution comes entirely
 * from retracted paths for k>1 (begin[1] = 1.0 from algorithm; ls
 * mode, for example).
 * 
 * Sets bsc[1..M] (the entry/begin scores), using the core
 * model and the algorithmic begin[] distribution.
 * 
 * xref STL9/81.
 */
static void
left_wing_retraction_added(struct plan7_s *hmm)
{
  int    k;
  float  cumul;
  float *bmk;		/* log B->D...D->M_k path probabilities, BMk */
  float  x;

  bmk = MallocOrDie(sizeof(float) * (hmm->M+1));

  /* Calculate the log B->M_k path probabilities; xref STL9/81
   */
  bmk[1] = log((1. - hmm->tbd1), 1.0);
  cumul = log(hmm->tbd1);
  for (k = 2; k <= hmm->M; k++)
    {
      bmk[k] = cumul + log(hmm->t[k-1][TDM]);
      cumul  += log(hmm->t[k-1][TDD]);
    }

  /* Renormalization (has little if any effect)
   * 
   * <cumul> now contains the log P of the B->D_1...D_M->E mute path
   * that we're removing. If (1-BE) is significantly different than
   * 1.0, renormalize the B distribution by dividing by (1-BE).  
   * Because log(1-x) = -x for x << 1, we know that subtracting
   * log(1-BE) from a log prob is only significant if logBE > log epsilon.
   */
  if (cumul > log(FLT_EPSILON))
    { 
      x = log(1. - exp(cumul));
      for (k = 1; k <= M; k++)
	bmk -= x;
    }

  /* Conversion to scores. 
   * At this step, we're assuming that hmm->begin[k] = 0.0 for
   * k>1: the algorithm has no internal entries of its own, and
   * internal entry comes exclusively from paths through D states.
   */
  for (k = 1; k <= hmm->M; k++)
     hmm->bsc[k] = LL2Score(bmk[k], hmm->p1);
  
  /* Virtual removal of D_1 state.
   */
  hmm->tsc[TDM][1] = -INFTY;
  hmm->tsc[TDD][1] = -INFTY;

  free(bmk);
  return;
}


/* right_wing_retraction_imposed()
 * 
 * Wing retraction for exits, for algorithms where M_k->E exit
 * probabilities are imposed by the algorithm (sw, fs modes). 
 * 
 * Sets esc[1..M] (the exit scores); also sets tsc[TM*][1..M-1], which
 * are affected by the presence of the new M_k->E probabilities.
 * 
 * xref STL9/81.
 */
static void
right_wing_retraction_imposed(struct plan7_s *hmm)
{
  int    k;
  float *mke;
  float  cumul;
  float  x;			/* temporary log prob */

  mke = MallocOrDie(sizeof(float) * (hmm->M+1));  

  /* The log prob of the wing-retracted M_k -> D...D -> E paths,
   * for k < M. (undefined for k == M).
   */
  cumul = 0.;
  for (k = hmm->M-1; k >= 1; k--)
    {
      mke[k] = cumul + log(hmm->t[k][TMD]);
      cumul += log(hmm->t[k][TDD]);
    }

  /* Set the esc[] and tsc[][TM*] scores.
   * 
   * The MkE path probability is subtracted from t[k][TMD] transition;
   * the match transitions are renormalized to account for the new
   * end[k] probability; and the match transitions are also renormalized
   * to account for the newly missing MkE path probability from TMD.
   * (xref STL9/91 for details).
   */
  for (k = 1; k < hmm->M; k++)
    {
      hmm->esc[k] = Prob2Score(hmm->end[k], 1.0); /* end[k] is imposed. */

      x = log(hmm->t[k][TMM]);
      if (hmm->end[k] > FLOAT_EPSILON) x += log(1. - hmm->end[k]);
      if (mke[k] > log(FLOAT_EPSILON)) x -= log(1. - exp(mke[k]));
      hmm->tsc[TMM][k] = LL2Score(x, hmm->p1);

      x = log(hmm->t[k][TMI]);
      if (hmm->end[k] > FLOAT_EPSILON) x += log(1. - hmm->end[k]);
      if (mke[k] > log(FLOAT_EPSILON)) x -= log(1. - exp(mke[k]));
      hmm->tsc[TMI][k] = LL2Score(x, hmm->p1);

      x = log(hmm->t[k][TMD]);
      if (mke[k] - x > log(FLOAT_EPSILON)) x += log(1. - exp(mke[k] - x));
      if (hmm->end[k] > FLOAT_EPSILON)     x += log(1. - hmm->end[k]);         
      if (mke[k] > log(FLOAT_EPSILON))     x -= log(1. - exp(mke[k]));
      hmm->tsc[TMD][k] = LL2Score(x, 1.0);
    }
  hmm->esc[hmm->M] = 0.0;	/* by definition */

  /* Note that node M isn't even explicitly represented in the
   * configured HMM scores -- tsc[][] only contains values for
   * 1..M-1. So there's no need to set any M_M or D_M transition
   * scores to 0 and -INFTY as we virtually remove D_M state;
   * the only other affected score is tsc[TDM][hmm->M-1], which
   * is going to be set in Logoddsify(), as it's probability 1.0
   * by definition. So, we're done.
   */
  free(mke);
  return;
}


/* right_wing_retraction_added()
 * 
 * Retract the right wing (remove the D_M state, and all paths through
 * it), for algorithms which have no M_k->E end[k] internal exit
 * probability. The Mk->Dk+1...DM->E path probabilities are therefore
 * subtracted from t[k][TMD] and added to end[k].
 *
 * Sets esc[1..M] (the exit scores); also sets tsc[TM*][1..M-1], which
 * are affected by the presence of the new M_k->E probabilities.
 * 
 * xref STL9/81.
 */
static void
right_wing_retraction_added(struct plan7_s *hmm)
{
  int    k;
  float *mke;
  float  cumul;
  float  x;			/* temporary log prob */

  mke = MallocOrDie(sizeof(float) * (hmm->M+1));  

  /* The log prob of the wing-retracted M_k -> D...D -> E paths,
   * for k < M. (undefined for k == M).
   */
  cumul = 0.;
  for (k = hmm->M-1; k >= 1; k--)
    {
      mke[k] = cumul + log(hmm->t[k][TMD]);
      cumul += log(hmm->t[k][TDD]);
    }

  /* Set the esc[] and tsc[TM*][] scores.
   * 
   * The end probability is assumed to be exclusively the MkE
   * path probability; algorithm has no internal exit prob of its own.
   * 
   * The MkE path probability is moved from the t[k][TMD] transition
   * to the end[k] probability. No renormalization is needed, because
   * prob is conserved: we assume that the algorithm added no
   * internal exit probability end[k] of its own.
   * (xref STL9/91 for details).
   */
  for (k = 1; k < hmm->M; k++)
    {
      hmm->esc[k] = LL2Score(mke[k], 1.0); /* M->E comes only thru terminal deletes */

      hmm->tsc[TMM][k] = Prob2Score(hmm->t[k][TMM], hmm->p1);
      hmm->tsc[TMI][k] = Prob2Score(hmm->t[k][TMI], hmm->p1);
      
      x = log(hmm->t[k][TMD]);
      if ((mke[k] - x) > log(FLOAT_EPSILON)) x += log(1. - exp(mke[k] - x));
      hmm->tsc[TMD][k] = LL2Score(x, 1.0);
    }
  hmm->esc[hmm->M] = 0.0;	/* by definition */

  /* Note that node M isn't even explicitly represented in the
   * configured HMM scores -- tsc[][] only contains values for
   * 1..M-1. So there's no need to set any M_M or D_M transition
   * scores to 0 and -INFTY as we virtually remove D_M state;
   * the only other affected score is tsc[TDM][hmm->M-1], which
   * is going to be set in Logoddsify(), as it's probability 1.0
   * by definition. So, we're done.
   */
  free(mke);
  return;
}






/* Function: P7Logoddsify()
 * 
 * Purpose:  Take an HMM with valid probabilities, and
 *           fill in the integer log-odds score section of the model.
 *           
 *    Notes on log-odds scores:
 *         type of parameter       probability        score
 *         -----------------       -----------        ------
 *         any emission             p_x           log_2 p_x/null_x
 *             N,J,C /assume/ p_x = null_x so /always/ score zero.  
 *         transition to emitters   t_x           log_2 t_x/p1
 *            (M,I; N,C; J)
 *             NN and CC loops are often equal to p1, so usu. score zero.
 *         C->T transition          t_x            log_2 t_x/p2 
 *            often zero, usu. C->T = p2. 
 *         all other transitions    t_x           log_2 t_x
 *             (no null model counterpart, so null prob is 1)    
 *             
 *    Notes on entry/exit scores, B->M and M->E:
 *         The probability form model includes delete states 1 and M. 
 *         these states are removed from a search form model to
 *         prevent B->D...D->E->J->B mute cycles, which would complicate
 *         dynamic programming algorithms. The data-independent
 *         S/W B->M and M->E transitions are folded together with
 *         data-dependent B->D...D->M and M->D...D->E paths.
 *         
 *         This process is referred to in the code as "wing folding"
 *         or "wing retraction"... the analogy is to a swept-wing
 *         fighter in landing vs. high speed flight configuration.
 *         
 *    Note on Viterbi vs. forward flag:     
 *         Wing retraction must take forward vs. Viterbi
 *         into account. If forward, sum two paths; if Viterbi, take
 *         max. I tried to slide this by as a sum, without
 *         the flag, but Alex detected it as a bug, because you can
 *         then find cases where the Viterbi score doesn't match
 *         the P7TraceScore().
 *             
 * Args:      hmm          - the hmm to calculate scores in.
 *            viterbi_mode - TRUE to fold wings in Viterbi configuration.
 *                  
 * Return:    (void)
 *            hmm scores are filled in.
 */  
void
P7Logoddsify(struct plan7_s *hmm, int viterbi_mode)
{
  int k;			/* counter for model position */
  int x;			/* counter for symbols        */
  float accum;
  float tbm, tme;

  if (hmm->flags & PLAN7_HASBITS) return;

  /* Symbol emission scores
   */
  for (k = 1; k <= hmm->M; k++) 
    {
				/* match/insert emissions in main model */
      for (x = 0; x < Alphabet_size; x++) 
	{
	  hmm->msc[x][k] = Prob2Score(hmm->mat[k][x], hmm->null[x]);
	  if (k < hmm->M) 
	    hmm->isc[x][k] =  Prob2Score(hmm->ins[k][x], hmm->null[x]); 
	}
				/* degenerate match/insert emissions */
      for (x = Alphabet_size; x < Alphabet_iupac; x++) 
	{
	  hmm->msc[x][k] = DegenerateSymbolScore(hmm->mat[k], hmm->null, x);
	  if (k < hmm->M)
	    hmm->isc[x][k] = DegenerateSymbolScore(hmm->ins[k], hmm->null, x);
	}
    }

  /* State transitions.
   * 
   * A note on "folding" of D_1 and D_M.
   * These two delete states are folded out of search form models
   * in order to prevent null cycles in the dynamic programming
   * algorithms (see code below). However, we use their log transitions
   * when we save the model! So the following log transition probs
   * are used *only* in save files, *never* in search algorithms:
   *    log (tbd1), D1 -> M2, D1 -> D2
   *    Mm-1 -> Dm, Dm-1 -> Dm
   *    
   * In a search algorithm, these have to be interpreted as -INFTY    
   * because their contributions are folded into bsc[] and esc[]
   * entry/exit scores. They can't be set to -INFTY here because
   * we need them in save files.
   */
  for (k = 1; k < hmm->M; k++)
    {
      hmm->tsc[TMM][k] = Prob2Score(hmm->t[k][TMM], hmm->p1);
      hmm->tsc[TMI][k] = Prob2Score(hmm->t[k][TMI], hmm->p1);
      hmm->tsc[TMD][k] = Prob2Score(hmm->t[k][TMD], 1.0);
      hmm->tsc[TIM][k] = Prob2Score(hmm->t[k][TIM], hmm->p1);
      hmm->tsc[TII][k] = Prob2Score(hmm->t[k][TII], hmm->p1);
      hmm->tsc[TDM][k] = Prob2Score(hmm->t[k][TDM], hmm->p1);
      hmm->tsc[TDD][k] = Prob2Score(hmm->t[k][TDD], 1.0);
    }

  /* B->M entry transitions. Note how D_1 is folded out.
   * M1 is just B->M1
   * M2 is sum (or max) of B->M2 and B->D1->M2
   * M_k is sum (or max) of B->M_k and B->D1...D_k-1->M_k
   * These have to be done in log space, else you'll get
   * underflow errors; and we also have to watch for log(0).
   * A little sloppier than it probably has to be; historically,
   * doing in this in log space was in response to a bug report.
   */
  accum = hmm->tbd1 > 0.0 ? log(hmm->tbd1) : -9999.;
  for (k = 1; k <= hmm->M; k++)
    {
      tbm = hmm->begin[k] > 0. ? log(hmm->begin[k]) : -9999.;	/* B->M_k part */

      /* B->D1...D_k-1->M_k part we get from accum*/
      if (k > 1 && accum > -9999.) 
	{	
	  if (hmm->t[k-1][TDM] > 0.0)
	    {
	      if (viterbi_mode) tbm =  MAX(tbm, accum + log(hmm->t[k-1][TDM]));
	      else              tbm =  LogSum(tbm, accum + log(hmm->t[k-1][TDM]));
	    }

	  accum = (hmm->t[k-1][TDD] > 0.0) ? accum + log(hmm->t[k-1][TDD]) : -9999.;
	}
				/* Convert from log_e to scaled integer log_2 odds. */
      if (tbm > -9999.) 
	hmm->bsc[k] = (int) floor(0.5 + INTSCALE * 1.44269504 * (tbm - log(hmm->p1)));
      else
	hmm->bsc[k] = -INFTY;
    }

  /* M->E exit transitions. Note how D_M is folded out.
   * M_M is 1 by definition
   * M_M-1 is sum of M_M-1->E and M_M-1->D_M->E, where D_M->E is 1 by definition
   * M_k is sum of M_k->E and M_k->D_k+1...D_M->E
   * Must be done in log space to avoid underflow errors.
   * A little sloppier than it probably has to be; historically,
   * doing in this in log space was in response to a bug report.
   */
  hmm->esc[hmm->M] = 0;
  accum = 0.;
  for (k = hmm->M-1; k >= 1; k--)
    {
      tme = hmm->end[k] > 0. ? log(hmm->end[k]) : -9999.;
      if (accum > -9999.)
	{
	  if (hmm->t[k][TMD] > 0.0)
	    {	
	      if (viterbi_mode) tme = MAX(tme, accum + log(hmm->t[k][TMD]));
	      else              tme = LogSum(tme, accum + log(hmm->t[k][TMD]));
	    }
	  accum = (hmm->t[k][TDD] > 0.0) ? accum + log(hmm->t[k][TDD]) : -9999.;
	}
				/* convert from log_e to scaled integer log odds. */
      hmm->esc[k] = (tme > -9999.) ? (int) floor(0.5 + INTSCALE * 1.44269504 * tme) : -INFTY;
    }

				/* special transitions */
  hmm->xsc[XTN][LOOP] = Prob2Score(hmm->xt[XTN][LOOP], hmm->p1);
  hmm->xsc[XTN][MOVE] = Prob2Score(hmm->xt[XTN][MOVE], 1.0);
  hmm->xsc[XTE][LOOP] = Prob2Score(hmm->xt[XTE][LOOP], 1.0);
  hmm->xsc[XTE][MOVE] = Prob2Score(hmm->xt[XTE][MOVE], 1.0);
  hmm->xsc[XTC][LOOP] = Prob2Score(hmm->xt[XTC][LOOP], hmm->p1);
  hmm->xsc[XTC][MOVE] = Prob2Score(hmm->xt[XTC][MOVE], 1.-hmm->p1);
  hmm->xsc[XTJ][LOOP] = Prob2Score(hmm->xt[XTJ][LOOP], hmm->p1);
  hmm->xsc[XTJ][MOVE] = Prob2Score(hmm->xt[XTJ][MOVE], 1.0);

  hmm->flags |= PLAN7_HASBITS;	/* raise the log-odds ready flag */
}



/* Function:  Plan7Rescale()
 * Incept:    Steve Johnson, 3 May 2004
 *            eweights code incorp: SRE, Thu May 20 10:34:03 2004 [St. Louis]
 *
 * Purpose:   Scale a counts-based HMM by some factor, for
 *            adjusting counts to a new effective sequence number.
 *
 * Args:      hmm        - counts based HMM.
 *            scale      - scaling factor (e.g. eff_nseq/nseq); 1.0= no scaling.
 *
 * Returns:   (void)
 */
void 
Plan7Rescale(struct plan7_s *hmm, float scale)
{
  int k;
  int st;

  /* emissions and transitions in the main model.
   * Note that match states are 1..M, insert states are 1..M-1,
   * and only nodes 1..M-1 have a valid array of transitions.
   */
  for(k = 1; k <= hmm->M; k++) 
    FScale(hmm->mat[k], Alphabet_size, scale);
  for(k = 1; k <  hmm->M; k++) 
    FScale(hmm->ins[k], Alphabet_size, scale);
  for(k = 1; k <  hmm->M; k++) 
    FScale(hmm->t[k],   7,             scale);

  /* begin, end transitions; only valid [1..M] */
  FScale(hmm->begin+1, hmm->M, scale);
  FScale(hmm->end+1,   hmm->M, scale);
  
  /* B->D1 transition */
  hmm->tbd1 *= scale;

  /* special transitions */
  for (st = 0; st < 4; st++)
    FScale(hmm->xt[st], 2, scale);

  return;
}




/* Function: Plan7Renormalize()
 * 
 * Purpose:  Take an HMM in counts form, and renormalize
 *           all of its probability vectors. Also enforces
 *           Plan7 restrictions on nonexistent transitions.
 *           
 * Args:     hmm - the model to renormalize.
 *                 
 * Return:   (void)
 *           hmm is changed.
 */                          
void
Plan7Renormalize(struct plan7_s *hmm)
{
  int   k;			/* counter for model position */
  int   st;			/* counter for special states */
  float d;			/* denominator */

				/* match emissions */
  for (k = 1; k <= hmm->M; k++) 
    FNorm(hmm->mat[k], Alphabet_size);
				/* insert emissions */
  for (k = 1; k < hmm->M; k++)
    FNorm(hmm->ins[k], Alphabet_size);
				/* begin transitions */
  d = FSum(hmm->begin+1, hmm->M) + hmm->tbd1;
  FScale(hmm->begin+1, hmm->M, 1./d);
  hmm->tbd1 /= d;
				/* main model transitions */
  for (k = 1; k < hmm->M; k++)
    {
      d = FSum(hmm->t[k], 3) + hmm->end[k]; 
      FScale(hmm->t[k], 3, 1./d);
      hmm->end[k] /= d;

      FNorm(hmm->t[k]+3, 2);	/* insert */
      FNorm(hmm->t[k]+5, 2);	/* delete */
    }
				/* null model emissions */
  FNorm(hmm->null, Alphabet_size);
				/* special transitions  */
  for (st = 0; st < 4; st++)
    FNorm(hmm->xt[st], 2);
				/* enforce nonexistent transitions */
				/* (is this necessary?) */
  hmm->t[0][TDM] = hmm->t[0][TDD] = 0.0;

  hmm->flags &= ~PLAN7_HASBITS;	/* clear the log-odds ready flag */
  hmm->flags |= PLAN7_HASPROB;	/* set the probabilities OK flag */
}
  

/* Function: Plan7RenormalizeExits()
 * Date:     SRE, Fri Aug 14 11:22:19 1998 [St. Louis]
 *
 * Purpose:  Renormalize just the match state transitions;
 *           for instance, after a Config() function has
 *           modified the exit distribution.
 *
 * Args:     hmm - hmm to renormalize
 *
 * Returns:  void
 */
void
Plan7RenormalizeExits(struct plan7_s *hmm)
{
  int   k;
  float d;

  for (k = 1; k < hmm->M; k++)
    {
      d = FSum(hmm->t[k], 3);
      /* FScale(hmm->t[k], 3, 1./(d + d*hmm->end[k])); */
      FScale(hmm->t[k], 3, (1.-hmm->end[k])/d);
    }
}



#ifdef SRE_REMOVED
/* Function: Plan7ESTConfig()
 * 
 * Purpose:  Configure a Plan7 model for EST Smith/Waterman
 *           analysis.
 *           
 *           OUTDATED; DO NOT USE WITHOUT RECHECKING
 *           
 * Args:     hmm        - hmm to configure.
 *           aacode     - 0..63 vector mapping genetic code to amino acids
 *           estmodel   - 20x64 translation matrix, w/ codon bias and substitution error
 *           dna2       - probability of a -1 frameshift in a triplet
 *           dna4       - probability of a +1 frameshift in a triplet     
 */ 
void
Plan7ESTConfig(struct plan7_s *hmm, int *aacode, float **estmodel, 
	       float dna2, float dna4)
{
  int k;
  int x;
  float p;
  float *tripnull;		/* UNFINISHED!!! */

				/* configure specials */
  hmm->xt[XTN][MOVE] = 1./351.;
  hmm->xt[XTN][LOOP] = 350./351.;
  hmm->xt[XTE][MOVE] = 1.;
  hmm->xt[XTE][LOOP] = 0.;
  hmm->xt[XTC][MOVE] = 1./351.;
  hmm->xt[XTC][LOOP] = 350./351.;
  hmm->xt[XTJ][MOVE] = 1.;
  hmm->xt[XTJ][LOOP] = 0.;
				/* configure entry/exit */
  hmm->begin[1] = 0.5;
  FSet(hmm->begin+2, hmm->M-1, 0.5 / ((float)hmm->M - 1.));
  hmm->end[hmm->M] = 1.;
  FSet(hmm->end, hmm->M-1, 0.5 / ((float)hmm->M - 1.));

				/* configure dna triplet/frameshift emissions */
  for (k = 1; k <= hmm->M; k++)
    {
				/* translate aa to triplet probabilities */
      for (x = 0; x < 64; x++) {
	p =  hmm->mat[k][aacode[x]] * estmodel[aacode[x]][x] * (1.-dna2-dna4);
	hmm->dnam[x][k] = Prob2Score(p, tripnull[x]);

	p = hmm->ins[k][aacode[x]] * estmodel[aacode[x]][x] * (1.-dna2-dna4);
	hmm->dnai[x][k] = Prob2Score(p, tripnull[x]);
      }
      hmm->dnam[64][k] = 0;	/* ambiguous codons score 0 (danger?) */
      hmm->dna2 = Prob2Score(dna2, 1.);
      hmm->dna4 = Prob2Score(dna4, 1.);
    }
}
#endif /*SRE_REMOVED*/
	  
/* Function: PrintPlan7Stats()
 * 
 * Purpose:  Given a newly constructed HMM and the tracebacks
 *           of the sequences it was trained on, print out all
 *           the interesting information at the end of hmmbuild
 *           runs that convinces the user we actually
 *           did something.
 *           
 * Args:     fp   - where to send the output (stdout, usually)
 *           hmm  - the new HMM, probability form
 *           dsq  - digitized training seqs
 *           nseq - number of dsq's
 *           tr   - array of tracebacks for dsq
 *                  
 * Return:   (void)
 */
void
PrintPlan7Stats(FILE *fp, struct plan7_s *hmm, unsigned char **dsq, int nseq,
		struct p7trace_s **tr)
{
  int   idx;			/* counter for sequences                */
  float score;			/* an individual trace score            */
  float total, best, worst;	/* for the avg. and range of the scores */
  float sqsum, stddev;		/* for the std. deviation of the scores */

  P7Logoddsify(hmm, TRUE);	/* make sure model scores are ready */

				/* find individual trace scores */
  score = P7TraceScore(hmm, dsq[0], tr[0]);
  total = best = worst = score;
  sqsum = score * score;
  for (idx = 1; idx < nseq; idx++) {
    /* P7PrintTrace(stdout, tr[idx], hmm, dsq[idx]); */
    score  = P7TraceScore(hmm, dsq[idx], tr[idx]);
    total += score;
    sqsum += score * score;
    if (score > best)  best = score;
    if (score < worst) worst = score;
  }
  if (nseq > 1) {
    stddev = (sqsum - (total * total / (float) nseq)) / ((float) nseq - 1.);
    stddev = (stddev > 0) ? sqrt(stddev) : 0.0;
  } else stddev = 0.0;
				/* print out stuff. */
  fprintf(fp, "Average score:  %10.2f bits\n", total / (float) nseq);
  fprintf(fp, "Minimum score:  %10.2f bits\n", worst);
  fprintf(fp, "Maximum score:  %10.2f bits\n", best);
  fprintf(fp, "Std. deviation: %10.2f bits\n", stddev);
}

/* Function: DegenerateSymbolScore()
 * 
 * Purpose:  Given a sequence character x and an hmm emission probability
 *           vector, calculate the log-odds (base 2) score of
 *           the symbol.
 *          
 *           Easy if x is in the emission alphabet, but not so easy
 *           is x is a degenerate symbol. The "correct" Bayesian
 *           philosophy is to calculate score(X) by summing over
 *           p(x) for all x in the degenerate symbol X to get P(X),
 *           doing the same sum over the prior to get F(X), and
 *           doing log_2 (P(X)/F(X)). This gives an X a zero score,
 *           for instance.
 *           
 *           Though this is correct in a formal Bayesian sense --
 *           we have no information on the sequence, so we can't
 *           say if it's random or model, so it scores zero --
 *           it sucks, big time, for scoring biological sequences.
 *           Sequences with lots of X's score near zero, while
 *           real sequences have average scores that are negative --
 *           so the X-laden sequences appear to be lifted out
 *           of the noise of a full histogram of a database search.
 *           Correct or not, this is highly undesirable.
 *           
 *           So therefore we calculated the expected score of
 *           the degenerate symbol by summing over all x in X:
 *                 e_x log_2 (p(x)/f(x))
 *           where the expectation of x, e_x, is calculated from
 *           the random model.
 *
 *           Empirically, this works; it also has a wooly hand-waving
 *           probabilistic justification that I'm happy enough about.
 *           
 * Args:     p      - probabilities of normal symbols
 *           null   - null emission model
 *           ambig  - index of the degenerate character in Alphabet[]
 *                    
 * Return:   the integer log odds score of x given the emission
 *           vector and the null model, scaled up by INTSCALE.              
 */
int 
DegenerateSymbolScore(float *p, float *null, int ambig)
{
  int x;
  float numer = 0.;
  float denom = 0.;

  for (x = 0; x < Alphabet_size; x++) {
    if (Degenerate[ambig][x]) {
      numer += null[x] * sreLOG2(p[x] / null[x]);
      denom += null[x];
    }
  }
  return (int) (INTSCALE * numer / denom);
}

/*****************************************************************
 * 
 * Plan9/Plan7 interface
 * 
 * Very important code during the evolutionary takeover by Plan7 --
 * convert between Krogh/Haussler and Plan7 models.
 *****************************************************************/

/* Function: Plan9toPlan7()
 * 
 * Purpose:  Convert an old HMM into Plan7. Configures it in
 *           ls mode.
 *           
 * Args:     hmm       - old ugly plan9 style HMM
 *           ret_plan7 - new wonderful Plan7 HMM
 *           
 * Return:   (void)    
 *           Plan7 HMM is allocated here. Free w/ FreePlan7().
 */               
void
Plan9toPlan7(struct plan9_s *hmm, struct plan7_s **ret_plan7)
{
  struct plan7_s *plan7;
  int k, x;

  plan7 = AllocPlan7(hmm->M);
  
  for (k = 1; k < hmm->M; k++)
    {
      plan7->t[k][TMM] = hmm->mat[k].t[MATCH];
      plan7->t[k][TMD] = hmm->mat[k].t[DELETE];
      plan7->t[k][TMI] = hmm->mat[k].t[INSERT];
      plan7->t[k][TDM] = hmm->del[k].t[MATCH];
      plan7->t[k][TDD] = hmm->del[k].t[DELETE];
      plan7->t[k][TIM] = hmm->ins[k].t[MATCH];
      plan7->t[k][TII] = hmm->ins[k].t[INSERT];
    }

  for (k = 1; k <= hmm->M; k++)
    for (x = 0; x < Alphabet_size; x++)
      plan7->mat[k][x] = hmm->mat[k].p[x];

  for (k = 1; k < hmm->M; k++)
    for (x = 0; x < Alphabet_size; x++)
      plan7->ins[k][x] = hmm->ins[k].p[x];

  plan7->tbd1 = hmm->mat[0].t[DELETE] / (hmm->mat[0].t[DELETE] + hmm->mat[0].t[MATCH]);
  
		/* We have to make up the null transition p1; use default */
  P7DefaultNullModel(plan7->null, &(plan7->p1));
  for (x = 0; x < Alphabet_size; x++)
    plan7->null[x] = hmm->null[x];
      
  if (hmm->name != NULL) 
    Plan7SetName(plan7, hmm->name);
  if (hmm->flags & HMM_REF) {
    strcpy(plan7->rf, hmm->ref);
    plan7->flags |= PLAN7_RF;
  }
  if (hmm->flags & HMM_CS) {
    strcpy(plan7->cs, hmm->cs);
    plan7->flags |= PLAN7_CS;
  }

  Plan7LSConfig(plan7);		/* configure specials for ls-style alignment */
  Plan7Renormalize(plan7);	/* mainly to correct for missing ID and DI */
  plan7->flags |= PLAN7_HASPROB;	/* probabilities are valid */
  plan7->flags &= ~PLAN7_HASBITS;	/* scores are not valid    */
  *ret_plan7 = plan7;
}



/************************************************************
 * @LICENSE@
 ************************************************************/


