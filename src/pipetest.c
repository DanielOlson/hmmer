#include "p7_config.h"

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_exponential.h"
#include "esl_getopts.h"
#include "esl_gumbel.h"
#include "esl_sq.h"
#include "esl_sqio.h"

#include "hmmer.h"
#include "p7_sparsemx.h"
#include "sparse_fwdback.h"
#include "sparse_masstrace.h"
#include "sparse_envscore.h"

static ESL_OPTIONS options[] = {
  /* name           type      default  env  range toggles reqs incomp  help                                       docgroup*/
  { "-h",        eslARG_NONE,   FALSE, NULL, NULL,  NULL,  NULL, NULL, "show brief help on version and usage",           0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options] <hmmfile> <seqfile>";
static char banner[] = "testbed for new pipeline";

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
  P7_OPROFILE    *om      = NULL;
  P7_FILTERMX    *ox      = NULL;
  P7_SPARSEMASK  *sm      = NULL;
  P7_SPARSEMX    *sx      = NULL;
  P7_SPARSEMX    *sxf     = NULL;
  P7_SPARSEMX    *sxb     = NULL;
  P7_SPARSEMX    *sxd     = NULL;
  P7_TRACE       *tr      = p7_trace_CreateWithPP();
  ESL_SQ         *sq      = NULL;
  ESL_SQFILE     *sqfp    = NULL;
  int             format  = eslSQFILE_UNKNOWN;
  float           msvsc, vfsc, fsc, vsc, nullsc;
  float           P;
  int             d;
  int             iae,ibe, kae,kbe;
  float           ndom_exp;
  float           Bprob, Eprob;
  float           envsc_approx, envsc_exact;
  int             status;
  

  /* initialize HMMER (simplify this!) */
  p7_FLogsumInit();
  impl_Init();

  /* Read in one HMM */
  if (p7_hmmfile_OpenE(hmmfile, NULL, &hfp, NULL) != eslOK) p7_Fail("Failed to open HMM file %s", hmmfile);
  if (p7_hmmfile_Read(hfp, &abc, &hmm)            != eslOK) p7_Fail("Failed to read HMM");

  /* create default null model, then create and optimize profile */
  bg = p7_bg_Create(abc);               
  gm = p7_profile_Create(hmm->M, abc); 
  p7_profile_ConfigCustom(gm, hmm, bg, 500, 1.0, 0.5);
  om = p7_oprofile_Create(gm->M, abc);
  p7_oprofile_Convert(gm, om);

  /* Open sequence file for reading */
  sq     = esl_sq_CreateDigital(abc);
  if (esl_sqfile_Open(seqfile, format, NULL, &sqfp) != eslOK) p7_Fail("Failed to open seq file %s\n", seqfile);

  /* Allocate matrices and sparse mask for a default seq size, 500 */
  ox  = p7_filtermx_Create  (gm->M, 500, ESL_MBYTES(32));  
  sm  = p7_sparsemask_Create(gm->M, 500);
  sx  = p7_sparsemx_Create  (sm);
  sxf = p7_sparsemx_Create  (sm);
  sxb = p7_sparsemx_Create  (sm);
  sxd = p7_sparsemx_Create  (sm);
  
  /* Loop over sequences in db... */
  while ((status = esl_sqio_Read(sqfp, sq)) == eslOK)
    {
      /* Length models */
      p7_oprofile_ReconfigLength(om, sq->n);
      p7_profile_SetLength      (gm, sq->n);
      p7_bg_SetLength(bg,            sq->n);

      /* Null */
      p7_bg_NullOne  (bg, sq->dsq, sq->n, &nullsc);

      /* MSV */
      p7_filtermx_GrowTo(ox, om->M, sq->n);  // we should specialize a mx to MSV,Vit only
      p7_MSVFilter(sq->dsq, sq->n, om, ox, &msvsc);
      msvsc = (msvsc - nullsc) / eslCONST_LOG2;
      P     =  esl_gumbel_surv(msvsc,  om->evparam[p7_MMU],  om->evparam[p7_MLAMBDA]);
      if (P > 0.02) goto NEXT_SEQ;

      /* Viterbi filter */
      p7_ViterbiFilter(sq->dsq, sq->n, om, ox, &vfsc);  
      vfsc = (vfsc - nullsc) / eslCONST_LOG2;
      P    = esl_gumbel_surv(vfsc,  om->evparam[p7_VMU],  om->evparam[p7_VLAMBDA]);
      if (P > 0.001) goto NEXT_SEQ;

      /* Forward filter */
      p7_ForwardFilter(sq->dsq, sq->n, om, ox, &fsc);
      fsc = (fsc - nullsc) / eslCONST_LOG2;
      P   = esl_exp_surv(fsc, om->evparam[p7_FTAU],  om->evparam[p7_FLAMBDA]);
      if (P > 1e-5) goto NEXT_SEQ;

      /* Backward, decoding, and sparse mask creation */
      p7_sparsemask_Reinit(sm, om->M, sq->n);
      p7_BackwardFilter(sq->dsq, sq->n, om, ox, sm);

      /* Sparse postprocessors */
      p7_sparsemx_Reinit(sx,  sm);
      p7_sparsemx_Reinit(sxf, sm);
      p7_sparsemx_Reinit(sxb, sm);
      p7_sparsemx_Reinit(sxd, sm);

      p7_SparseViterbi (sq->dsq, sq->n, gm, sx,  tr, &vsc);
      p7_SparseForward (sq->dsq, sq->n, gm, sxf,     &fsc);
      p7_SparseBackward(sq->dsq, sq->n, gm, sxb,     NULL);
      p7_SparseDecoding(gm, sxf, sxb, sxd);
      p7_sparsemx_TracePostprobs(sxd, tr);
      p7_trace_Index(tr);
      p7_sparsemx_Reuse(sx);
      
      for (d = 0; d < tr->ndom; d++)
	{
	  p7_sparsemx_Reinit(sx, sm);
	  p7_sparse_masstrace_Up  (sq->dsq, sq->n, gm, sxf, sx, tr, tr->anch[d], 0.1, &iae, &kae);
	  p7_sparse_masstrace_Down(sq->dsq, sq->n, gm, sxb, sx, tr, tr->anch[d], 0.1, &ibe, &kbe);
	  p7_sparsemx_Reuse(sx);
	  
	  p7_sparsemx_ExpectedDomains(sxd, iae, ibe, &ndom_exp);
	  Bprob = p7_sparsemx_GetSpecial(sxd, iae-1, p7S_B);
	  Eprob = p7_sparsemx_GetSpecial(sxd, ibe,   p7S_E);
	  
	  p7_sparsemx_ApproxEnvScore(gm, sxf, iae, ibe, &envsc_approx);

	  p7_sparsemx_Reinit(sx, sm);
	  p7_SparseEnvScore(sq->dsq, sq->n, gm, iae, ibe, kae, kbe, sx, &envsc_exact);
	  p7_sparsemx_Reuse(sx);

	  printf("%-30s %-20s %3d %5d %5d %5d %5d %5d %5d %5d %5d %5.2f %6.4f %6.4f %9.3f %9.3f %9.3f\n",
		 sq->name, gm->name, d+1,
		 tr->sqfrom[d], tr->sqto[d], tr->hmmfrom[d], tr->hmmto[d],
		 iae, ibe, kae, kbe,  
		 ndom_exp, Bprob, Eprob,
		 envsc_approx, envsc_exact, 
		 envsc_approx-envsc_exact);
	}

    NEXT_SEQ:
      esl_sq_Reuse(sq);
      p7_trace_Reuse(tr);
      p7_filtermx_Reuse(ox);    
      p7_sparsemask_Reuse(sm);  // most seqs don't use sparsemask: don't need to reuse because we never reinit'ed
      p7_sparsemx_Reuse(sx);
      p7_sparsemx_Reuse(sxf);
      p7_sparsemx_Reuse(sxb);
      p7_sparsemx_Reuse(sxd);
    }

  esl_sqfile_Close(sqfp);
  p7_hmmfile_Close(hfp);
  esl_sq_Destroy(sq);
  p7_trace_Destroy(tr);
  p7_sparsemx_Destroy(sx);
  p7_sparsemx_Destroy(sxf);
  p7_sparsemx_Destroy(sxb);
  p7_sparsemx_Destroy(sxd);
  p7_sparsemask_Destroy(sm);
  p7_filtermx_Destroy(ox);
  p7_oprofile_Destroy(om);
  p7_profile_Destroy(gm);
  p7_bg_Destroy(bg);
  p7_hmm_Destroy(hmm);
  esl_alphabet_Destroy(abc);
  esl_getopts_Destroy(go);
  return 0;
}
