/* Routines for the P7_OPROFILE structure:  
 * a search profile in an optimized implementation.
 * 
 * Contents:
 *   1. The P7_OPROFILE object: allocation, initialization, destruction.
 *   2. Conversion from generic P7_PROFILE to optimized P7_OPROFILE
 *   3. Conversion from optimized P7_OPROFILE to compact score arrays
 *   4. Debugging and development utilities.
 *   5. Benchmark driver.
 *   6. Example.
 *   7. Copyright and license information.
 */
#include "p7_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>		/* roundf() */

#if p7_CPU_ARCH == intel 
#include <xmmintrin.h>		/* SSE  */
#include <emmintrin.h>		/* SSE2 */
#endif /* intel arch */

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_random.h"
#include "esl_sse.h"
#include "esl_vectorops.h"

#include "base/p7_bg.h"
#include "base/p7_hmm.h"
#include "base/p7_profile.h"

#include "build/modelsample.h"
#include "search/modelconfig.h"

#include "dp_vector/p7_oprofile.h"

/*****************************************************************
 * 1. The P7_OPROFILE structure: a score profile.
 *****************************************************************/

/* Function:  p7_oprofile_Create()
 * Synopsis:  Allocate an optimized profile structure.
 * Incept:    SRE, Sun Nov 25 12:03:19 2007 [Casa de Gatos]
 *
 * Purpose:   Allocate for profiles of up to <allocM> nodes for digital alphabet <abc>.
 *
 * Throws:    <NULL> on allocation error.
 */
P7_OPROFILE *
p7_oprofile_Create_avx512(int allocM, const ESL_ALPHABET *abc)
{
#ifdef HAVE_AVX512
  int          status;
  P7_OPROFILE *om  = NULL;

  int          nqb_AVX_512 = P7_NVB_AVX_512(allocM); /* # of uchar vectors needed for query */
  int          nqw_AVX_512 = P7_NVW_AVX_512(allocM); /* # of sword vectors needed for query */
  int          nqf_AVX_512 = P7_NVF_AVX_512(allocM); /* # of float vectors needed for query */
  int          nqs_AVX_512 = nqb_AVX_512 + p7O_EXTRA_SB;

  int          x;

  /* level 0 */
  ESL_ALLOC(om, sizeof(P7_OPROFILE));
  om->simd = AVX512;
  om->rbv_mem_AVX_512   = NULL;
  om->sbv_mem_AVX_512   = NULL;
  om->rbv_AVX_512       = NULL;
  om->sbv_AVX_512       = NULL;
  om->rwv_mem_AVX_512   = NULL;
  om->twv_mem_AVX_512   = NULL;
  om->rwv_AVX_512       = NULL;
  om->twv_AVX_512       = NULL;
  om->rfv_mem_AVX_512   = NULL;
  om->tfv_mem_AVX_512   = NULL;
  om->rfv_AVX_512       = NULL;
  om->tfv_AVX_512       = NULL;


  om->is_shadow = FALSE;

  om->name    = NULL;
  om->acc     = NULL;
  om->desc    = NULL;

  om->rf        = NULL;
  om->mm        = NULL;
  om->cs        = NULL;
  om->consensus = NULL;

  /* level 1 */
 
  ESL_ALLOC(om->rbv_mem_AVX_512, sizeof(__m512i) * nqb_AVX_512  * abc->Kp          +63); /* +63 is for manual 64-byte alignment */
  ESL_ALLOC(om->sbv_mem_AVX_512, sizeof(__m512i) * nqs_AVX_512  * abc->Kp          +63); 
  ESL_ALLOC(om->rwv_mem_AVX_512, sizeof(__m512i) * nqw_AVX_512  * abc->Kp          +63);                     
  ESL_ALLOC(om->twv_mem_AVX_512, sizeof(__m512i) * nqw_AVX_512  * p7O_NTRANS       +63);
  ESL_ALLOC(om->rfv_mem_AVX_512, sizeof(__m512)  * nqf_AVX_512  * abc->Kp          +63);                     
  ESL_ALLOC(om->tfv_mem_AVX_512, sizeof(__m512)  * nqf_AVX_512  * p7O_NTRANS       +63);       

  ESL_ALLOC(om->rbv_AVX_512, sizeof(__m512i *) * abc->Kp); 
  ESL_ALLOC(om->sbv_AVX_512, sizeof(__m512i *) * abc->Kp); 
  ESL_ALLOC(om->rwv_AVX_512, sizeof(__m512i *) * abc->Kp);  
  ESL_ALLOC(om->rfv_AVX_512, sizeof(__m512  *) * abc->Kp); 

  om->rbv_AVX_512[0] = (__m512i *) (((unsigned long int) om->rbv_mem_AVX_512 + 63) & (~0x3f));
  om->sbv_AVX_512[0] = (__m512i *) (((unsigned long int) om->sbv_mem_AVX_512 + 63) & (~0x3f));
  om->rwv_AVX_512[0] = (__m512i *) (((unsigned long int) om->rwv_mem_AVX_512 + 63) & (~0x3f));
  om->twv_AVX_512    = (__m512i *) (((unsigned long int) om->twv_mem_AVX_512 + 63) & (~0x3f));
  om->rfv_AVX_512[0] = (__m512  *) (((unsigned long int) om->rfv_mem_AVX_512 + 63) & (~0x3f));
  om->tfv_AVX_512    = (__m512  *) (((unsigned long int) om->tfv_mem_AVX_512 + 63) & (~0x3f));

  /* set the rest of the row pointers for match emissions */
  for (x = 1; x < abc->Kp; x++) {

    om->rbv_AVX_512[x] = om->rbv_AVX_512[0] + (x * nqb_AVX_512);
    om->sbv_AVX_512[x] = om->sbv_AVX_512[0] + (x * nqs_AVX_512);
    om->rwv_AVX_512[x] = om->rwv_AVX_512[0] + (x * nqw_AVX_512);
    om->rfv_AVX_512[x] = om->rfv_AVX_512[0] + (x * nqf_AVX_512);

  }


  om->allocQ16_AVX_512  = nqb_AVX_512;
  om->allocQ8_AVX_512   = nqw_AVX_512;
  om->allocQ4_AVX_512   = nqf_AVX_512;

  /* Remaining initializations */
  om->tbm_b     = 0;
  om->tec_b     = 0;
  om->tjb_b     = 0;
  om->scale_b   = 0.0f;
  om->base_b    = 0;
  om->bias_b    = 0;

  om->scale_w      = 0.0f;
  om->base_w       = 0;
  om->ddbound_w    = 0;
  om->ncj_roundoff = 0.0f;	

  for (x = 0; x < p7_NOFFSETS; x++) om->offs[x]    = -1;
  for (x = 0; x < p7_NEVPARAM; x++) om->evparam[x] = p7_EVPARAM_UNSET;
  for (x = 0; x < p7_NCUTOFFS; x++) om->cutoff[x]  = p7_CUTOFF_UNSET;
  for (x = 0; x < p7_MAXABET;  x++) om->compo[x]   = p7_COMPO_UNSET;

  /* in a P7_OPROFILE, we always allocate for the optional RF, CS annotation.  
   * we only rely on the leading \0 to signal that it's unused, but 
   * we initialize all this memory to zeros to shut valgrind up about 
   * fwrite'ing uninitialized memory in the io functions.
   */
  ESL_ALLOC(om->rf,          sizeof(char) * (allocM+2));
  ESL_ALLOC(om->mm,          sizeof(char) * (allocM+2));
  ESL_ALLOC(om->cs,          sizeof(char) * (allocM+2));
  ESL_ALLOC(om->consensus,   sizeof(char) * (allocM+2));
  memset(om->rf,       '\0', sizeof(char) * (allocM+2));
  memset(om->mm,       '\0', sizeof(char) * (allocM+2));
  memset(om->cs,       '\0', sizeof(char) * (allocM+2));
  memset(om->consensus,'\0', sizeof(char) * (allocM+2));

  om->abc        = abc;
  om->L          = 0;
  om->M          = 0;
  om->max_length = -1;
  om->allocM     = allocM;
  om->mode       = p7_NO_MODE;
  om->nj         = 0.0f;
  return om;

 ERROR:
  p7_oprofile_Destroy(om);
  return NULL;
#endif //HAVE_AVX512
#ifndef HAVE_AVX512
return NULL;
#endif

}


/* Function:  p7_oprofile_Destroy()
 * Synopsis:  Frees an optimized profile structure.
 * Incept:    SRE, Sun Nov 25 12:22:21 2007 [Casa de Gatos]
 */
void
p7_oprofile_Destroy_avx512(P7_OPROFILE *om)
{
#ifdef HAVE_AVX512  
  if (om == NULL) return;

  if (! om->is_shadow)
    {
      if (om->rbv_mem_AVX_512)   free(om->rbv_mem_AVX_512);
      if (om->sbv_mem_AVX_512)   free(om->sbv_mem_AVX_512);
      if (om->rwv_mem_AVX_512)   free(om->rwv_mem_AVX_512);
      if (om->twv_mem_AVX_512)   free(om->twv_mem_AVX_512);
      if (om->rfv_mem_AVX_512)   free(om->rfv_mem_AVX_512);
      if (om->tfv_mem_AVX_512)   free(om->tfv_mem_AVX_512);

      if (om->rbv_AVX_512)       free(om->rbv_AVX_512);
      if (om->sbv_AVX_512)       free(om->sbv_AVX_512);
      if (om->rwv_AVX_512)       free(om->rwv_AVX_512); 
      if (om->rfv_AVX_512)       free(om->rfv_AVX_512);


  
 
      if (om->name)      free(om->name);
      if (om->acc)       free(om->acc);
      if (om->desc)      free(om->desc);
      if (om->rf)        free(om->rf);
      if (om->mm)        free(om->mm);
      if (om->cs)        free(om->cs);
      if (om->consensus) free(om->consensus);
    }
  free(om);
#endif //HAVE_AVX512
#ifndef HAVE_AVX512
return;
#endif  
}

/* Function:  p7_oprofile_Sizeof()
 * Synopsis:  Return the allocated size of a <P7_OPROFILE>.
 * Incept:    SRE, Wed Mar  2 10:09:21 2011 [Janelia]
 *
 * Purpose:   Returns the allocated size of a <P7_OPROFILE>,
 *            in bytes.
 *            
 *            Very roughly, M*284 bytes, for a model of length M; 60KB
 *            for a typical model; 30MB for a design limit M=100K
 *            model.
 */
size_t
p7_oprofile_Sizeof_avx512(const P7_OPROFILE *om)
{
  size_t n   = 0;
#ifdef HAVE_AVX512  
  int    nqb_AVX_512 = om->allocQ16_AVX_512;  /* # of uchar vectors needed for query */
  int    nqw_AVX_512 = om->allocQ8_AVX_512;     /* # of sword vectors needed for query */
  int    nqf_AVX_512 = om->allocQ4_AVX_512;     /* # of float vectors needed for query */
  int    nqs_AVX_512 = nqb_AVX_512 + p7O_EXTRA_SB;

  /* Stuff below exactly mirrors the malloc()'s in
   * p7_oprofile_Create(); so even though we could
   * write this more compactly, leave it like this
   * w/ one:one correspondence to _Create(), for
   * maintainability and clarity.
   */
  n  += sizeof(P7_OPROFILE);
 
  n  += sizeof(__m512i) * nqb_AVX_512  * om->abc->Kp +63; /* om->rbv_mem_AVX_512   */
  n  += sizeof(__m512i) * nqs_AVX_512  * om->abc->Kp +63; /* om->sbv_mem_AVX_512   */
  n  += sizeof(__m512i) * nqw_AVX_512  * om->abc->Kp +63; /* om->rwv_mem_AVX_512   */
  n  += sizeof(__m512i) * nqw_AVX_512  * p7O_NTRANS  +63; /* om->twv_mem_AVX_512   */
  n  += sizeof(__m512)  * nqf_AVX_512  * om->abc->Kp +15; /* om->rfv_mem   */
  n  += sizeof(__m512)  * nqf_AVX_512  * p7O_NTRANS  +15; /* om->tfv_mem   */

  n  += sizeof(__m512i *) * om->abc->Kp;          /* om->rbv_AVX_512      */
  n  += sizeof(__m512i *) * om->abc->Kp;          /* om->sbv_AVX_512       */
  n  += sizeof(__m512i *) * om->abc->Kp;          /* om->rwv_AVX_512       */
  n  += sizeof(__m512 *) * om->abc->Kp;          /* om->rfv_AVX_512       */

  n  += sizeof(char) * (om->allocM+2);            /* om->rf        */
  n  += sizeof(char) * (om->allocM+2);            /* om->mm        */
  n  += sizeof(char) * (om->allocM+2);            /* om->cs        */
  n  += sizeof(char) * (om->allocM+2);            /* om->consensus */
#endif //HAVE_AVX512
  return n;
}


/* Function:  p7_oprofile_Clone()
 * Synopsis:  Create a new copy of an optimized profile structure.
 * Incept:    SRE, Sun Nov 25 12:03:19 2007 [Casa de Gatos]
 *
 * Purpose:   Create a newly allocated copy of <om1> and return a ptr
 *            to it.
 *            
 * Throws:    <NULL> on allocation error.
 */
P7_OPROFILE *
p7_oprofile_Clone_avx512(const P7_OPROFILE *om1)
{
#ifdef HAVE_AVX512  
  const ESL_ALPHABET *abc = om1->abc;
  P7_OPROFILE  *om2  = NULL;

  int           nqb_AVX_512  = P7_NVB_AVX_512(om1->allocM); /* # of uchar vectors needed for query */
  int           nqw_AVX_512  = P7_NVW_AVX_512(om1->allocM); /* # of sword vectors needed for query */
  int           nqf_AVX_512  = P7_NVF_AVX_512(om1->allocM); /* # of float vectors needed for query */
  int           nqs_AVX_512  = nqb_AVX_512 + p7O_EXTRA_SB;

  size_t        size = sizeof(char) * (om1->allocM+2);
  int           x, y;
  int           status;

  /* level 0 */
  ESL_ALLOC(om2, sizeof(P7_OPROFILE));

  om2->rbv_mem_AVX_512   = NULL;
  om2->sbv_mem_AVX_512   = NULL;
  om2->rwv_mem_AVX_512   = NULL;
  om2->twv_mem_AVX_512   = NULL; 
   om2->rfv_mem_AVX_512   = NULL;
  om2->tfv_mem_AVX_512   = NULL;
 
  om2->rbv_AVX_512       = NULL;
  om2->sbv_AVX_512       = NULL;
  om2->rwv_AVX_512       = NULL;
  om2->twv_AVX_512       = NULL;
  om2->rfv_AVX_512       = NULL;
  om2->tfv_AVX_512       = NULL;

  om2->is_shadow = FALSE;  // om1 can be a shadow, but the resulting copy is a full-fledged profile
  
  om2->name      = NULL;
  om2->acc       = NULL;
  om2->desc      = NULL;
  om2->rf        = NULL;
  om2->mm        = NULL;
  om2->cs        = NULL;
  om2->consensus = NULL;

  /* level 1 */

  ESL_ALLOC(om2->rbv_mem_AVX_512, sizeof(__m512i) * nqb_AVX_512  * abc->Kp    +63); /* +63 is for manual 32-byte alignment */
  ESL_ALLOC(om2->sbv_mem_AVX_512, sizeof(__m512i) * nqs_AVX_512  * abc->Kp    +63);
  ESL_ALLOC(om2->rwv_mem_AVX_512, sizeof(__m512i) * nqw_AVX_512  * abc->Kp    +63);                     
  ESL_ALLOC(om2->twv_mem_AVX_512, sizeof(__m512i) * nqw_AVX_512  * p7O_NTRANS +63);   
  ESL_ALLOC(om2->rfv_mem_AVX_512, sizeof(__m512)  * nqf_AVX_512  * abc->Kp    +63);                     
  ESL_ALLOC(om2->tfv_mem_AVX_512, sizeof(__m512)  * nqf_AVX_512  * p7O_NTRANS +63);    

  ESL_ALLOC(om2->rbv_AVX_512, sizeof(__m512i *) * abc->Kp); 
  ESL_ALLOC(om2->sbv_AVX_512, sizeof(__m512i *) * abc->Kp); 
  ESL_ALLOC(om2->rwv_AVX_512, sizeof(__m512i *) * abc->Kp); 
  ESL_ALLOC(om2->rfv_AVX_512, sizeof(__m512  *) * abc->Kp); 

  /* align vector memory on vector size boundaries */

  om2->rbv_AVX_512[0] = (__m512i *) (((unsigned long int) om2->rbv_mem_AVX_512 + 63) & (~0x3f));
  om2->sbv_AVX_512[0] = (__m512i *) (((unsigned long int) om2->sbv_mem_AVX_512 + 63) & (~0x3f));
  memcpy(om2->rbv_AVX_512[0], om1->rbv_AVX_512[0], sizeof(__m512i) * nqb_AVX_512  * abc->Kp);
  memcpy(om2->sbv_AVX_512[0], om1->sbv_AVX_512[0], sizeof(__m512i) * nqs_AVX_512  * abc->Kp);
  om2->rwv_AVX_512[0] = (__m512i *) (((unsigned long int) om2->rwv_mem_AVX_512 + 63) & (~0x3f));
  om2->twv_AVX_512    = (__m512i *) (((unsigned long int) om2->twv_mem_AVX_512 + 63) & (~0x3f));
  memcpy(om2->rwv_AVX_512[0], om1->rwv_AVX_512[0], sizeof(__m512i) * nqw_AVX_512  * abc->Kp);
  om2->rfv_AVX_512[0] = (__m512  *) (((unsigned long int) om2->rfv_mem_AVX_512 + 63) & (~0x3f));
  om2->tfv_AVX_512    = (__m512 *) (((unsigned long int) om2->tfv_mem_AVX_512 + 63) & (~0x3f));

  /* copy the vector data */

 
  memcpy(om2->rfv_AVX_512[0], om1->rfv_AVX_512[0], sizeof(__m512i) * nqf_AVX_512  * abc->Kp);

  /* set the rest of the row pointers for match emissions */
  for (x = 1; x < abc->Kp; x++) {
    om2->rbv_AVX_512[x] = om2->rbv_AVX_512[0] + (x * nqb_AVX_512);
    om2->sbv_AVX_512[x] = om2->sbv_AVX_512[0] + (x * nqs_AVX_512);
    om2->rwv_AVX_512[x] = om2->rwv_AVX_512[0] + (x * nqw_AVX_512);
    om2->rfv_AVX_512[x] = om2->rfv_AVX_512[0] + (x * nqf_AVX_512);
  }
 
  om2->allocQ16_AVX_512  = nqb_AVX_512;
  om2->allocQ8_AVX_512   = nqw_AVX_512;
  om2->allocQ4_AVX_512   = nqf_AVX_512;

  /* Remaining initializations */
  om2->tbm_b     = om1->tbm_b;
  om2->tec_b     = om1->tec_b;
  om2->tjb_b     = om1->tjb_b;
  om2->scale_b   = om1->scale_b;
  om2->base_b    = om1->base_b;
  om2->bias_b    = om1->bias_b;

  om2->scale_w      = om1->scale_w;
  om2->base_w       = om1->base_w;
  om2->ddbound_w    = om1->ddbound_w;
  om2->ncj_roundoff = om1->ncj_roundoff;	

  for (x = 0; x < p7_NOFFSETS; x++) om2->offs[x]    = om1->offs[x];
  for (x = 0; x < p7_NEVPARAM; x++) om2->evparam[x] = om1->evparam[x];
  for (x = 0; x < p7_NCUTOFFS; x++) om2->cutoff[x]  = om1->cutoff[x];
  for (x = 0; x < p7_MAXABET;  x++) om2->compo[x]   = om1->compo[x];

  for (x = 0; x < nqw_AVX_512  * p7O_NTRANS; ++x) om2->twv_AVX_512[x] = om1->twv_AVX_512[x];
  for (x = 0; x < nqf_AVX_512  * p7O_NTRANS; ++x) om2->tfv_AVX_512[x] = om1->tfv_AVX_512[x];


  for (x = 0; x < p7O_NXSTATES; x++)
    for (y = 0; y < p7O_NXTRANS; y++)
      {
	om2->xw[x][y] = om1->xw[x][y];
	om2->xf[x][y] = om1->xf[x][y];
      }

  if ((status = esl_strdup(om1->name, -1, &om2->name)) != eslOK) goto ERROR;
  if ((status = esl_strdup(om1->acc,  -1, &om2->acc))  != eslOK) goto ERROR;
  if ((status = esl_strdup(om1->desc, -1, &om2->desc)) != eslOK) goto ERROR;

  /* in a P7_OPROFILE, we always allocate for the optional RF, CS annotation.  
   * we only rely on the leading \0 to signal that it's unused, but 
   * we initialize all this memory to zeros to shut valgrind up about 
   * fwrite'ing uninitialized memory in the io functions.
   */
  ESL_ALLOC(om2->rf,          size);
  ESL_ALLOC(om2->mm,          size);
  ESL_ALLOC(om2->cs,          size);
  ESL_ALLOC(om2->consensus,   size);

  memcpy(om2->rf,        om1->rf,        size);
  memcpy(om2->mm,        om1->mm,        size);
  memcpy(om2->cs,        om1->cs,        size);
  memcpy(om2->consensus, om1->consensus, size);

  om2->abc        = om1->abc;
  om2->L          = om1->L;
  om2->M          = om1->M;
  om2->allocM     = om1->allocM;
  om2->mode       = om1->mode;
  om2->nj         = om1->nj;
  om2->max_length = om1->max_length;
  return om2;

 ERROR:
  p7_oprofile_Destroy(om2);
  return NULL;
#endif //HAVE_AVX512
#ifndef HAVE_AVX512
return NULL;
#endif  
}

/*----------------- end, P7_OPROFILE structure ------------------*/



/*****************************************************************
 * 2. Conversion from generic P7_PROFILE to optimized P7_OPROFILE
 *****************************************************************/

/* sf_conversion():
 * Author: Bjarne Knudsen
 * 
 * Generates the SSVFilter() parts of the profile <om> scores
 * from the completed MSV score.  This includes calculating 
 * special versions of the match scores for using the the
 * ssv filter.
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    (no abnormal error conditions)
 */
int
sf_conversion_avx512(P7_OPROFILE *om)
{
#ifdef HAVE_AVX512
   int     x;     /* counter over residues                                        */
  int     q;      /* q counts over total # of striped vectors, 0..nq-1           */
  int     M   = om->M;		/* length of the query                                          */

  __m512i tmp_AVX_512;
  __m512i tmp2_AVX_512;
  int     nq_AVX_512  = P7_NVB_AVX_512(M);     /* segment length; total # of striped vectors needed            */


  /* We now want to fill out om->sbv with om->rbv - bias for use in the
   * SSV filter. The only challenge is that the om->rbv values are
   * unsigned and generally use the whole scale while the om->sbv
   * values are signed. To solve that problem we perform the following
   * calculation:
   *
   *   ((127 + bias) - rbv) ^ 127
   *
   * where the subtraction is unsigned saturated and the addition is
   * unsigned (it will not overflow, since bias is a small positive
   * number). The f(x) = x ^ 127 combined with a change from unsigned
   * to signed numbers have the same effect as f(x) = -x + 127. So if
   * we regard the above as signed instead of unsigned it is equal to:
   *
   *   -((127 + bias) - rbv) + 127 = rbv - bias
   *
   * which is what we want. The reason for this slightly complex idea
   * is that we wish the transformation to be fast, especially for
   * hmmscan where many models are loaded.
   */


  tmp_AVX_512 = _mm512_set1_epi8((int8_t) (om->bias_b + 127));
  tmp2_AVX_512  = _mm512_set1_epi8(127);

  for (x = 0; x < om->abc->Kp; x++)
    {
      for (q = 0;  q < nq_AVX_512;            q++) om->sbv_AVX_512[x][q] = _mm512_xor_si512(_mm512_subs_epu8(tmp_AVX_512, om->rbv_AVX_512[x][q]), tmp2_AVX_512);
      for (q = nq_AVX_512; q < nq_AVX_512 + p7O_EXTRA_SB; q++) om->sbv_AVX_512[x][q] = om->sbv_AVX_512[x][q % nq_AVX_512];
    }
  
  return eslOK;
  #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}

/* mf_conversion(): 
 * 
 * This builds the MSVFilter() parts of the profile <om>, scores
 * in lspace uchars (16-way parallel), by rescaling, rounding, and
 * casting the scores in <gm>.
 * 
 * Returns <eslOK> on success;
 * throws <eslEINVAL> if <om> hasn't been allocated properly.
 */
int
mf_conversion_avx512(const P7_PROFILE *gm, P7_OPROFILE *om)
{
#ifdef HAVE_AVX512
  int     M   = gm->M;		/* length of the query                                          */

  int     nq_AVX_512  = P7_NVB_AVX_512(M);     /* segment length; total # of striped vectors needed            */
  union { __m512i v; uint8_t i[64]; } tmp_AVX_512; /* used to align and load simd minivectors           */

  float   max = 0.0;		/* maximum residue score: used for unsigned emission score bias */
  int     x;			/* counter over residues                                        */
  int     q;			/* q counts over total # of striped vectors, 0..nq-1            */
  int     k;			/* the usual counter over model nodes 1..M                      */
  int     z;			/* counter within elements of one SIMD minivector               */

  if (nq_AVX_512 > om->allocQ16_AVX_512) ESL_EXCEPTION(eslEINVAL, "optimized profile is too small to hold conversion");

  /* First we determine the basis for the limited-precision MSVFilter scoring system. 
   * Default: 1/3 bit units, base offset 190:  range 0..255 => -190..65 => -63.3..21.7 bits
   * See J2/66, J4/138 for analysis.
   */
  for (x = 0; x < gm->abc->K; x++)  max = ESL_MAX(max, esl_vec_FMax(gm->rsc[x], (M+1)*2));
  om->scale_b = 3.0 / eslCONST_LOG2;                    /* scores in units of third-bits */
  om->base_b  = 190;
  om->bias_b  = unbiased_byteify(om, -1.0 * max);

  /* striped match costs: start at k=1.  */
  for (x = 0; x < gm->abc->Kp; x++)
    for (q = 0, k = 1; q < nq_AVX_512; q++, k++)
      {
  for (z = 0; z < 64; z++) tmp_AVX_512.i[z] = ((k+ z*nq_AVX_512 <= M) ? biased_byteify(om, P7P_MSC(gm, k+z*nq_AVX_512, x)) : 255);
  om->rbv_AVX_512[x][q]   = tmp_AVX_512.v;  
      }

  /* transition costs */
  om->tbm_b = unbiased_byteify(om, logf(2.0f / ((float) gm->M * (float) (gm->M+1)))); /* constant B->Mk penalty        */
  om->tec_b = unbiased_byteify(om, logf(0.5f));                                       /* constant multihit E->C = E->J */
  om->tjb_b = unbiased_byteify(om, logf(3.0f / (float) (gm->L+3))); /* this adopts the L setting of the parent profile */

  sf_conversion_avx512(om);

  return eslOK;
  #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}


/* vf_conversion(): 
 * 
 * This builds the ViterbiFilter() parts of the profile <om>, scores
 * in lspace swords (8-way parallel), by rescaling, rounding, and
 * casting the scores in <gm>.
 * 
 * Returns <eslOK> on success;
 * throws <eslEINVAL> if <om> hasn't been allocated properly.
 */
int
vf_conversion_avx512(const P7_PROFILE *gm, P7_OPROFILE *om)
{
#ifdef HAVE_AVX512  
  int     M   = gm->M;		/* length of the query                                          */

  int     nq_AVX_512  = P7_NVW_AVX_512(M);     /* segment length; total # of striped vectors needed            */
  union { __m512i v; int16_t i[32]; } tmp_AVX_512; /* used to align and load simd minivectors            */
  if (nq_AVX_512 > om->allocQ8_AVX_512) ESL_EXCEPTION(eslEINVAL, "optimized profile is too small to hold conversion");

  int     x;			/* counter over residues                                        */
  int     q;			/* q counts over total # of striped vectors, 0..nq-1            */
  int     k;			/* the usual counter over model nodes 1..M                      */
  int     kb;			/* possibly offset base k for loading om's TSC vectors          */
  int     z;			/* counter within elements of one SIMD minivector               */
  int     t;			/* counter over transitions 0..7 = p7O_{BM,MM,IM,DM,MD,MI,II,DD}*/
  int     tg;			/* transition index in gm                                       */
  int     j;			/* counter in interleaved vector arrays in the profile          */
  int     ddtmp;		/* used in finding worst DD transition bound                    */
  int16_t  maxval;		/* used to prevent zero cost II                                 */
  int16_t  val;


  /* First set the basis for the limited-precision scoring system. 
   * Default: 1/500 bit units, base offset 12000:  range -32768..32767 => -44768..20767 => -89.54..41.53 bits
   * See J4/138 for analysis.
   */
  om->scale_w = 500.0 / eslCONST_LOG2;
  om->base_w  = 12000;

  /* striped match scores */
  for (x = 0; x < gm->abc->Kp; x++) {
 
    for (k = 1, q = 0; q < nq_AVX_512; q++, k++)
      {
         for (z = 0; z < 32; z++) tmp_AVX_512.i[z] = ((k+ z*nq_AVX_512 <= M) ? wordify(om, P7P_MSC(gm, k+z*nq_AVX_512, x)) : -32768);
         om->rwv_AVX_512[x][q]   = tmp_AVX_512.v;
      }
  }

  /* Transition costs, all but the DD's. */ 
  for (j = 0, k = 1, q = 0; q < nq_AVX_512; q++, k++)
    {
      for (t = p7O_BM; t <= p7O_II; t++) /* this loop of 7 transitions depends on the order in p7o_tsc_e */
  {
    switch (t) {
    case p7O_BM: tg = p7P_LM;  kb = k-1; maxval =  0; break; /* gm has tLMk stored off by one! start from k=0 not 1   */
    case p7O_MM: tg = p7P_MM;  kb = k-1; maxval =  0; break; /* MM, DM, IM vectors are rotated by -1, start from k=0  */
    case p7O_IM: tg = p7P_IM;  kb = k-1; maxval =  0; break;
    case p7O_DM: tg = p7P_DM;  kb = k-1; maxval =  0; break;
    case p7O_MD: tg = p7P_MD;  kb = k;   maxval =  0; break; /* the remaining ones are straight up  */
    case p7O_MI: tg = p7P_MI;  kb = k;   maxval =  0; break; 
    case p7O_II: tg = p7P_II;  kb = k;   maxval = -1; break; 
    }

    for (z = 0; z < 32; z++) {
      val      = ((kb+ z*nq_AVX_512 < M) ? wordify(om, P7P_TSC(gm, kb+ z*nq_AVX_512, tg)) : -32768);
      tmp_AVX_512.i[z] = (val <= maxval) ? val : maxval; /* do not allow an II transition cost of 0, or hell may occur. */
    }
    om->twv_AVX_512[j++] = tmp_AVX_512.v;
  }
    }

  /* Finally the DD's, which are at the end of the optimized tsc vector; (j is already sitting there) */
  for (k = 1, q = 0; q < nq_AVX_512; q++, k++)
    {
      for (z = 0; z < 32; z++) tmp_AVX_512.i[z] = ((k+ z*nq_AVX_512 < M) ? wordify(om, P7P_TSC(gm, k+ z*nq_AVX_512, p7P_DD)) : -32768);
      om->twv_AVX_512[j++] = tmp_AVX_512.v;
    }

  /* Specials. (Actually in same order in om and gm, but we copy in general form anyway.)  */
  /* VF CC,NN,JJ transitions hardcoded zero; -3.0 nat approximation used instead; this papers
   * over a length independence problem, where the approximation weirdly outperforms the
   * exact solution, probably indicating that the model's Pascal distribution is problematic,
   * and the "approximation" is in fact closer to the One True Model, the mythic H4 supermodel.
   * [xref J5/36] 
   */
  om->xw[p7O_E][p7O_LOOP] = wordify(om, gm->xsc[p7P_E][p7P_LOOP]);  
  om->xw[p7O_E][p7O_MOVE] = wordify(om, gm->xsc[p7P_E][p7P_MOVE]);
  om->xw[p7O_N][p7O_MOVE] = wordify(om, gm->xsc[p7P_N][p7P_MOVE]);
  om->xw[p7O_N][p7O_LOOP] = 0;                                        /* was wordify(om, gm->xsc[p7P_N][p7P_LOOP]); */
  om->xw[p7O_C][p7O_MOVE] = wordify(om, gm->xsc[p7P_C][p7P_MOVE]);
  om->xw[p7O_C][p7O_LOOP] = 0;                                        /* was wordify(om, gm->xsc[p7P_C][p7P_LOOP]); */
  om->xw[p7O_J][p7O_MOVE] = wordify(om, gm->xsc[p7P_J][p7P_MOVE]);
  om->xw[p7O_J][p7O_LOOP] = 0;                                        /* was wordify(om, gm->xsc[p7P_J][p7P_LOOP]); */

  om->ncj_roundoff = 0.0; /* goes along with NN=CC=JJ=0, -3.0 nat approximation */
                          /* otherwise, would be = om->scale_w * gm->xsc[p7P_N][p7P_LOOP] -  om->xw[p7O_N][p7O_LOOP];   */
			  /* see J4/150 for discussion of VF error suppression, superceded by the -3.0 nat approximation */

  /* Transition score bound for "lazy F" DD path evaluation (xref J2/52) */
  om->ddbound_w = -32768;	
  for (k = 2; k < M-1; k++) 
    {
      ddtmp         = (int) wordify(om, P7P_TSC(gm, k,   p7P_DD));
      ddtmp        += (int) wordify(om, P7P_TSC(gm, k+1, p7P_DM));
      ddtmp        -= (int) wordify(om, P7P_TSC(gm, k+1, p7P_LM));
      om->ddbound_w = ESL_MAX(om->ddbound_w, ddtmp);
    }

  return eslOK;
  #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}


/* fb_conversion()
 * This builds the Forward/Backward part of the optimized profile <om>,
 * where we use odds ratios (not log-odds scores).
 */
int
fb_conversion_avx512(const P7_PROFILE *gm, P7_OPROFILE *om)
{
  #ifdef HAVE_AVX512
  int     M   = gm->M;		/* length of the query                                          */
  int     x;			/* counter over residues                                        */
  int     q;			/* q counts over total # of striped vectors, 0..nq-1            */
  int     k;			/* the usual counter over model nodes 1..M                      */
  int     kb;			/* possibly offset base k for loading om's TSC vectors          */
  int     z;			/* counter within elements of one SIMD minivector               */
  int     t;			/* counter over transitions 0..7 = p7O_{BM,MM,IM,DM,MD,MI,II,DD}*/
  int     tg;			/* transition index in gm                                       */
  int     j;			/* counter in interleaved vector arrays in the profile          */
  
  union { __m512 avx512; __m128 sse[4]; float x[16]; } tmp_AVX_512;

  int     nq_AVX_512  = P7_NVF_AVX_512(M);     /* segment length; total # of striped vectors needed            */
  if (nq_AVX_512 > om->allocQ4_AVX_512) ESL_EXCEPTION(eslEINVAL, "optimized profile is too small to hold conversion");

  /* striped match scores: start at k=1 */
  for (x = 0; x < gm->abc->Kp; x++)
    for (k = 1, q = 0; q < nq_AVX_512; q++, k++)
      {
        for (z = 0; z < 16; z++) tmp_AVX_512.x[z] = (k+ z*nq_AVX_512 <= M) ? P7P_MSC(gm, k+z*nq_AVX_512, x) : -eslINFINITY;
        tmp_AVX_512.sse[0] = esl_sse_expf(tmp_AVX_512.sse[0]);  // Hack because we don't currently have AVX version of expf
        tmp_AVX_512.sse[1] = esl_sse_expf(tmp_AVX_512.sse[1]);
        tmp_AVX_512.sse[2] = esl_sse_expf(tmp_AVX_512.sse[2]);  
        tmp_AVX_512.sse[3] = esl_sse_expf(tmp_AVX_512.sse[3]);
        om->rfv_AVX_512[x][q] = tmp_AVX_512.avx512;
      }

  /* Transition scores, all but the DD's. */
  for (j = 0, k = 1, q = 0; q < nq_AVX_512; q++, k++)
    {
      for (t = p7O_BM; t <= p7O_II; t++) /* this loop of 7 transitions depends on the order in the definition of p7o_tsc_e */
      {
        switch (t) {
          case p7O_BM: tg = p7P_LM;  kb = k-1; break; /* gm has tBMk stored off by one! start from k=0 not 1 */
          case p7O_MM: tg = p7P_MM;  kb = k-1; break; /* MM, DM, IM quads are rotated by -1, start from k=0  */
          case p7O_IM: tg = p7P_IM;  kb = k-1; break;
          case p7O_DM: tg = p7P_DM;  kb = k-1; break;
          case p7O_MD: tg = p7P_MD;  kb = k;   break; /* the remaining ones are straight up  */
          case p7O_MI: tg = p7P_MI;  kb = k;   break; 
          case p7O_II: tg = p7P_II;  kb = k;   break; 
           }

        for (z = 0; z < 16; z++) tmp_AVX_512.x[z] = (kb+z*nq_AVX_512 < M) ? P7P_TSC(gm, kb+z*nq_AVX_512, tg) : -eslINFINITY;
        tmp_AVX_512.sse[0] = esl_sse_expf(tmp_AVX_512.sse[0]);  // Hack because we don't currently have AVX version of expf
        tmp_AVX_512.sse[1] = esl_sse_expf(tmp_AVX_512.sse[1]);
        tmp_AVX_512.sse[2] = esl_sse_expf(tmp_AVX_512.sse[2]);  
        tmp_AVX_512.sse[3] = esl_sse_expf(tmp_AVX_512.sse[3]);
        om->tfv_AVX_512[j++] = tmp_AVX_512.avx512;
      }
    }

  /* And finally the DD's, which are at the end of the optimized tfv vector; (j is already there) */
  for (k = 1, q = 0; q < nq_AVX_512; q++, k++)
    {
      for (z = 0; z < 16; z++) tmp_AVX_512.x[z] = (k+z*nq_AVX_512 < M) ? P7P_TSC(gm, k+z*nq_AVX_512, p7P_DD) : -eslINFINITY;
      tmp_AVX_512.sse[0] = esl_sse_expf(tmp_AVX_512.sse[0]);  // Hack because we don't currently have AVX version of expf
      tmp_AVX_512.sse[1] = esl_sse_expf(tmp_AVX_512.sse[1]);
      tmp_AVX_512.sse[2] = esl_sse_expf(tmp_AVX_512.sse[2]);  
      tmp_AVX_512.sse[3] = esl_sse_expf(tmp_AVX_512.sse[3]);
      om->tfv_AVX_512[j++] = tmp_AVX_512.avx512;
    }

  /* Specials. (These are actually in exactly the same order in om and
   *  gm, but we copy in general form anyway.)
   */
  om->xf[p7O_E][p7O_LOOP] = expf(gm->xsc[p7P_E][p7P_LOOP]);  
  om->xf[p7O_E][p7O_MOVE] = expf(gm->xsc[p7P_E][p7P_MOVE]);
  om->xf[p7O_N][p7O_LOOP] = expf(gm->xsc[p7P_N][p7P_LOOP]);
  om->xf[p7O_N][p7O_MOVE] = expf(gm->xsc[p7P_N][p7P_MOVE]);
  om->xf[p7O_C][p7O_LOOP] = expf(gm->xsc[p7P_C][p7P_LOOP]);
  om->xf[p7O_C][p7O_MOVE] = expf(gm->xsc[p7P_C][p7P_MOVE]);
  om->xf[p7O_J][p7O_LOOP] = expf(gm->xsc[p7P_J][p7P_LOOP]);
  om->xf[p7O_J][p7O_MOVE] = expf(gm->xsc[p7P_J][p7P_MOVE]);

  return eslOK;
  #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}

/*------------ end, conversions to P7_OPROFILE ------------------*/

/*******************************************************************
 * 3. Conversion from optimized P7_OPROFILE to compact score arrays
 *******************************************************************/

/* Function:  p7_oprofile_GetFwdTransitionArray()
 * Synopsis:  Retrieve full 32-bit float transition probabilities from an
 *            optimized profile into a flat array
 *
 * Purpose:   Extract an array of <type> (e.g. p7O_II) transition probabilities
 *            from the underlying <om> profile. In SIMD implementations,
 *            these are striped and interleaved, making them difficult to
 *            directly access.
 *
 * Args:      <om>   - optimized profile, containing transition information
 *            <type> - transition type (e.g. p7O_II)
 *            <arr>  - preallocated array into which floats will be placed
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    (no abnormal error conditions)
 */
int
p7_oprofile_GetFwdTransitionArray_avx512(const P7_OPROFILE *om, int type, float *arr )
{
#ifdef HAVE_AVX512
  int     nq_AVX_512  = P7_NVF_AVX_512(om->M);     /* # of striped vectors needed            */
  int i_AVX_512, j_AVX_512;
  union { __m512 v; float x[16]; } tmp_AVX_512; /* used to align and read simd minivectors               */


  for (i_AVX_512=0; i_AVX_512<nq_AVX_512; i_AVX_512++) {
    // because DD transitions are held at the end of the tfv array
    tmp_AVX_512.v = om->tfv_AVX_512[ (type==p7O_DD ?  nq_AVX_512*7+i_AVX_512 :  type+7*i_AVX_512) ];
    for (j_AVX_512=0; j_AVX_512<16; j_AVX_512++)
      if ( i_AVX_512+1+ j_AVX_512*nq_AVX_512 < om->M+1)
        arr[i_AVX_512+1+ j_AVX_512*nq_AVX_512]      = tmp_AVX_512.x[j_AVX_512];
  }

  return eslOK;
#endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}

/* Function:  p7_oprofile_GetMSVEmissionScoreArray()
 * Synopsis:  Retrieve MSV residue emission scores from an optimized
 *            profile into an array
 *
 * Purpose:   Extract an implicitly 2D array of 8-bit int MSV residue
 *            emission scores from an optimized profile <om>. <arr> must
 *            be allocated by the calling function to be of size
 *            ( om->abc->Kp * ( om->M  + 1 )), and indexing into the array
 *            is done as  [om->abc->Kp * i +  c ] for character c at
 *            position i.
 *
 *            In SIMD implementations, the residue scores are striped
 *            and interleaved, making them somewhat difficult to
 *            directly access. Faster access is desired, for example,
 *            in SSV back-tracking of a high-scoring diagonal
 *
 * Args:      <om>   - optimized profile, containing transition information
 *            <arr>  - preallocated array into which scores will be placed
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    (no abnormal error conditions)
 */
int
p7_oprofile_GetMSVEmissionScoreArray_avx512(const P7_OPROFILE *om, uint8_t *arr )
{
#ifdef HAVE_AVX512 
  int x, q, z, k;
 int      M   = om->M;    /* length of the query                                          */
  int      K   = om->abc->Kp;

  union { __m512i v; uint8_t i[64]; } tmp_AVX_512; /* used to align and read simd minivectors           */
  int      nq_AVX_512  = P7_NVB_AVX_512(M);     /* segment length; total # of striped vectors needed            */

  

  int cell_cnt = (om->M + 1) * K;
  // SSE-striped array
  for (x = 0; x < K ; x++) {
    for (q = 0, k = 1; q < nq_AVX_512; q++, k++) {
      tmp_AVX_512.v = om->rbv_AVX_512[x][q];
      for (z=0; z<32; z++)
        if (  (K * (k+z*nq_AVX_512) + x) < cell_cnt) {
          arr[ K * (k+z*nq_AVX_512) + x ] = tmp_AVX_512.i[z];
        }
    }
  }

  return eslOK;
  #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}


/* Function:  p7_oprofile_GetFwdEmissionScoreArray()
 * Synopsis:  Retrieve Fwd (float) residue emission scores from an optimized
 *            profile into an array
 *
 * Purpose:   Extract an implicitly 2D array of 32-bit float Fwd residue
 *            emission scores from an optimized profile <om>. <arr> must
 *            be allocated by the calling function to be of size
 *            ( om->abc->Kp * ( om->M  + 1 )), and indexing into the array
 *            is done as  [om->abc->Kp * i +  c ] for character c at
 *            position i.
 *
 *            In SIMD implementations, the residue scores are striped
 *            and interleaved, making them somewhat difficult to
 *            directly access.
 *
 * Args:      <om>   - optimized profile, containing transition information
 *            <arr>  - preallocated array into which scores will be placed
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    (no abnormal error conditions)
 */
int
p7_oprofile_GetFwdEmissionScoreArray_avx512(const P7_OPROFILE *om, float *arr )
{
  #ifdef HAVE_AVX512
  int x, q, z, k;
  union { __m512 v; __m128 v2[4]; float f[16]; } tmp; /* used to align and read simd minivectors               */
  int      M   = om->M;    /* length of the query                                          */
  int      K   = om->abc->Kp;
  int      nq  = P7_NVF_AVX_512(M);     /* segment length; total # of striped vectors needed            */
  int cell_cnt = (om->M + 1) * K;

  for (x = 0; x < K; x++) {
      for (q = 0, k = 1; q < nq; q++, k++) {
        tmp.v = om->rfv_AVX_512[x][q];
        tmp.v2[0] = esl_sse_logf(tmp.v2[0]);  // Slow, but we shouldn't call this function that often
        tmp.v2[1] = esl_sse_logf(tmp.v2[1]);
        tmp.v2[2] = esl_sse_logf(tmp.v2[2]);
        tmp.v2[3] = esl_sse_logf(tmp.v2[3]);
        for (z = 0; z < 16; z++)
          if (  (K * (k+z*nq) + x) < cell_cnt)
            arr[ K * (k+z*nq) + x ] = tmp.f[z];
      }
  }
  return eslOK;
  #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}

/* Function:  p7_oprofile_GetFwdEmissionArray()
 * Synopsis:  Retrieve Fwd (float) residue emission values from an optimized
 *            profile into an array
 *
 * Purpose:   Extract an implicitly 2D array of 32-bit float Fwd residue
 *            emission values from an optimized profile <om>, converting
 *            back to emission values based on the background. <arr> must
 *            be allocated by the calling function to be of size
 *            ( om->abc->Kp * ( om->M  + 1 )), and indexing into the array
 *            is done as  [om->abc->Kp * i +  c ] for character c at
 *            position i.
 *
 *            In SIMD implementations, the residue scores are striped
 *            and interleaved, making them somewhat difficult to
 *            directly access.
 *
 * Args:      <om>   - optimized profile, containing transition information
 *            <bg>   - background frequencies
 *            <arr>  - preallocated array into which scores will be placed
 *
 * Returns:   <eslOK> on success.
 *
 * Throws:    (no abnormal error conditions)
 */
int
p7_oprofile_GetFwdEmissionArray_avx512(const P7_OPROFILE *om, P7_BG *bg, float *arr )
{
  int x, q, z, k;
#ifdef HAVE_AVX512 // this function has not been ported to AVX yet
  union { __m512 v; __m128 v2[4]; float f[16]; } tmp; /* used to align and read simd minivectors               */
  int      M   = om->M;    /* length of the query                                          */
  int      Kp  = om->abc->Kp;
  int      K   = om->abc->K;
  int      nq  = P7_NVF_AVX_512(M);     /* segment length; total # of striped vectors needed            */
  int cell_cnt = (om->M + 1) * Kp;

  for (x = 0; x < K; x++) {
      for (q = 0, k = 1; q < nq; q++, k++) {
        tmp.v = om->rfv_AVX_512[x][q];
        for (z = 0; z < 16; z++)
          if (  (Kp * (k+z*nq) + x) < cell_cnt)
            arr[ Kp * (k+z*nq) + x ] = tmp.f[z] * bg->f[x];
      }
  }

  //degeneracy emissions for each position
  for (x = 0; x <= M; x++)
    esl_abc_FExpectScVec(om->abc, arr+Kp*x, bg->f);
  return eslOK;
   #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}
/*------------ end, conversions from P7_OPROFILE ------------------*/


/*****************************************************************
 * 4. Debugging and development utilities.
 *****************************************************************/


/* oprofile_dump_mf()
 * 
 * Dump the MSVFilter part of a profile <om> to <stdout>.
 */
int
oprofile_dump_mf_avx512(FILE *fp, const P7_OPROFILE *om)
{
  int     M   = om->M;		/* length of the query                                          */
#ifdef HAVE_AVX512
  int     nq_AVX_512  = P7_NVB_AVX_512(M);     /* segment length; total # of striped vectors needed            */
  union { __m512i v; uint8_t i[64]; } tmp_AVX_512; /* used to align and read simd minivectors       */

  int     x;			/* counter over residues                                        */
  int     q;			/* q counts over total # of striped vectors, 0..nq-1            */
  int     k;			/* counter over nodes 1..M                                      */
  int     z;			/* counter within elements of one SIMD minivector               */

/* This will generate gibberish if more than one of p7_build_SSE, p7_build_AVX2, and p7_build_AVX512
  are set */

  /* Header (rearranged column numbers, in the vectors)  */
  fprintf(fp, "     ");
  for (k =1, q = 0; q < nq_AVX_512; q++, k++)
    {
      fprintf(fp, "[ ");
      for (z = 0; z < 64; z++) 
  if (k+z*nq_AVX_512 <= M) fprintf(fp, "%4d ", k+z*nq_AVX_512);
  else             fprintf(fp, "%4s ", "xx");
      fprintf(fp, "]");
    }
  fprintf(fp, "\n");

  /* Table of residue emissions */
  for (x = 0; x < om->abc->Kp; x++)
    {
      fprintf(fp, "(%c): ", om->abc->sym[x]); 

      for (q = 0; q < nq_AVX_512; q++)
  {
    fprintf(fp, "[ ");
    _mm512_store_si512(&tmp_AVX_512.v, om->rbv_AVX_512[x][q]);
    for (z = 0; z < 64; z++) fprintf(fp, "%4d ", tmp_AVX_512.i[z]);
    fprintf(fp, "]");
  }
      fprintf(fp, "\n");
    }
  fprintf(fp, "\n");


  fprintf(fp, "t_EC,EJ:    %4d\n",  om->tec_b);
  fprintf(fp, "t_NB,JB,CT: %4d\n",  om->tjb_b);
  fprintf(fp, "t_BMk:      %4d\n",  om->tbm_b);
  fprintf(fp, "scale:      %.2f\n", om->scale_b);
  fprintf(fp, "base:       %4d\n",  om->base_b);
  fprintf(fp, "bias:       %4d\n",  om->bias_b);
  fprintf(fp, "Q:          %4d\n",  nq_AVX_512);  
  fprintf(fp, "M:          %4d\n",  M);  
  return eslOK;
  #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}


/* oprofile_dump_vf()
 * 
 * Dump the ViterbiFilter part of a profile <om> to <stdout>.
 */
int
oprofile_dump_vf_avx512(FILE *fp, const P7_OPROFILE *om)
{
#ifdef HAVE_AVX512  
  int     M   = om->M;		/* length of the query                                          */
  int     nq  = P7_NVW(M);     /* segment length; total # of striped vectors needed            */
  int     x;			/* counter over residues                                        */
  int     q;			/* q counts over total # of striped vectors, 0..nq-1            */
  int     k;			/* the usual counter over model nodes 1..M                      */
  int     kb;			/* possibly offset base k for loading om's TSC vectors          */
  int     z;			/* counter within elements of one SIMD minivector               */
  int     t;			/* counter over transitions 0..7 = p7O_{BM,MM,IM,DM,MD,MI,II,DD}*/
  int     j;			/* counter in interleaved vector arrays in the profile          */
  union { __m512i v; int16_t i[32]; } tmp; /* used to align and read simd minivectors           */

  /* Emission score header (rearranged column numbers, in the vectors)  */
  fprintf(fp, "     ");
  for (k =1, q = 0; q < nq; q++, k++)
    {
      fprintf(fp, "[ ");
      for (z = 0; z < 32; z++) 
	if (k+z*nq <= M) fprintf(fp, "%6d ", k+z*nq);
	else             fprintf(fp, "%6s ", "xx");
      fprintf(fp, "]");
    }
  fprintf(fp, "\n");

  /* Table of residue emissions */
  for (x = 0; x < om->abc->Kp; x++)
    {
      fprintf(fp, "(%c): ", om->abc->sym[x]); 

      /* Match emission scores (insert emissions are assumed zero by design) */
      for (q = 0; q < nq; q++)
	{
	  fprintf(fp, "[ ");
	  tmp.v =  om->rwv_AVX_512[x][q];
	  for (z = 0; z < 32; z++) fprintf(fp, "%6d ", tmp.i[z]);
	  fprintf(fp, "]");
	}
      fprintf(fp, "\n");
    }
  fprintf(fp, "\n");

  /* Transitions */
  for (t = p7O_BM; t <= p7O_II; t++)
    {
      switch (t) {
      case p7O_BM: fprintf(fp, "\ntBM: "); break;
      case p7O_MM: fprintf(fp, "\ntMM: "); break;
      case p7O_IM: fprintf(fp, "\ntIM: "); break;
      case p7O_DM: fprintf(fp, "\ntDM: "); break;
      case p7O_MD: fprintf(fp, "\ntMD: "); break;
      case p7O_MI: fprintf(fp, "\ntMI: "); break;
      case p7O_II: fprintf(fp, "\ntII: "); break;
      }

      for (k = 1, q = 0; q < nq; q++, k++)
	{
	  switch (t) {
	  case p7O_BM: kb = k;                 break; 
	  case p7O_MM: kb = (1 + (nq+k-2)) % nq; break; /* MM, DM, IM quads rotated by +1  */
	  case p7O_IM: kb = (1 + (nq+k-2)) % nq; break;  
	  case p7O_DM: kb = (1 + (nq+k-2)) % nq; break;  
	  case p7O_MD: kb = k;                 break; /* the remaining ones are straight up  */
	  case p7O_MI: kb = k;                 break; 
	  case p7O_II: kb = k;                 break; 
	  }
	  fprintf(fp, "[ ");
	  for (z = 0; z < 32; z++) 
	    if (kb+z*nq <= M) fprintf(fp, "%6d ", kb+z*nq);
	    else              fprintf(fp, "%6s ", "xx");
	  fprintf(fp, "]");
	}
      fprintf(fp, "\n     ");	  
      for (q = 0; q < nq; q++)
	{
	  fprintf(fp, "[ ");
	  tmp.v =  om->twv_AVX_512[q*7 + t];
	  for (z = 0; z < 32; z++) fprintf(fp, "%6d ", tmp.i[z]);
	  fprintf(fp, "]");
	}
      fprintf(fp, "\n");	  
    }

  /* DD transitions */
  fprintf(fp, "\ntDD: ");
  for (k =1, q = 0; q < nq; q++, k++)
    {
      fprintf(fp, "[ ");
      for (z = 0; z < 32; z++) 
	if (k+z*nq <= M) fprintf(fp, "%6d ", k+z*nq);
	else             fprintf(fp, "%6s ", "xx");
      fprintf(fp, "]");
    }
  fprintf(fp, "\n     ");	  
  for (j = nq*7, q = 0; q < nq; q++, j++)
    {
      fprintf(fp, "[ ");
      tmp.v =  om->twv_AVX_512[j];
      for (z = 0; z < 32; z++) fprintf(fp, "%6d ", tmp.i[z]);
      fprintf(fp, "]");
    }
  fprintf(fp, "\n");	  

  fprintf(fp, "E->C: %6d    E->J: %6d\n", om->xw[p7O_E][p7O_MOVE], om->xw[p7O_E][p7O_LOOP]);
  fprintf(fp, "N->B: %6d    N->N: %6d\n", om->xw[p7O_N][p7O_MOVE], om->xw[p7O_N][p7O_LOOP]);
  fprintf(fp, "J->B: %6d    J->J: %6d\n", om->xw[p7O_J][p7O_MOVE], om->xw[p7O_J][p7O_LOOP]);
  fprintf(fp, "C->T: %6d    C->C: %6d\n", om->xw[p7O_C][p7O_MOVE], om->xw[p7O_C][p7O_LOOP]);

  fprintf(fp, "scale: %6.2f\n", om->scale_w);
  fprintf(fp, "base:  %6d\n",   om->base_w);
  fprintf(fp, "bound: %6d\n",   om->ddbound_w);
  fprintf(fp, "Q:     %6d\n",   nq);  
  fprintf(fp, "M:     %6d\n",   M);  

  return eslOK;
 #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}


/* oprofile_dump_fb()
 * 
 * Dump the Forward/Backward part of a profile <om> to <stdout>.
 * <width>, <precision> control the floating point output:
 *  8,5 is a reasonable choice for prob space,
 *  5,2 is reasonable for log space.
 */
int
oprofile_dump_fb_avx512(FILE *fp, const P7_OPROFILE *om, int width, int precision)
{
 #ifdef HAVE_AVX512 
  int     M   = om->M;		/* length of the query                                          */
  int     nq  = P7_NVF(M);     /* segment length; total # of striped vectors needed            */
  int     x;			/* counter over residues                                        */
  int     q;			/* q counts over total # of striped vectors, 0..nq-1            */
  int     k;			/* the usual counter over model nodes 1..M                      */
  int     kb;			/* possibly offset base k for loading om's TSC vectors          */
  int     z;			/* counter within elements of one SIMD minivector               */
  int     t;			/* counter over transitions 0..7 = p7O_{BM,MM,IM,DM,MD,MI,II,DD}*/
  int     j;			/* counter in interleaved vector arrays in the profile          */
  union { __m512 v; float x[16]; } tmp; /* used to align and read simd minivectors               */
  /* Residue emissions */
  for (x = 0; x < om->abc->Kp; x++)
    {
      fprintf(fp, "(%c): ", om->abc->sym[x]); 
      for (k =1, q = 0; q < nq; q++, k++)
	{
	  fprintf(fp, "[ ");
	  for (z = 0; z < 16; z++) 
	    if (k+z*nq <= M) fprintf(fp, "%*d ", width, k+z*nq);
	    else             fprintf(fp, "%*s ", width, "xx");
	  fprintf(fp, "]");
	}
      fprintf(fp, "\nmat: ");
      for (q = 0; q < nq; q++)
	{
	  fprintf(fp, "[ ");
	  tmp.v = om->rfv_AVX_512[x][q];
	  for (z = 0; z < 16; z++) fprintf(fp, "%*.*f ", width, precision, tmp.x[z]);
	  fprintf(fp, "]");
	}
      fprintf(fp, "\n\n");
    }

  /* Transitions */
  for (t = p7O_BM; t <= p7O_II; t++)
    {
      switch (t) {
      case p7O_BM: fprintf(fp, "\ntBM: "); break;
      case p7O_MM: fprintf(fp, "\ntMM: "); break;
      case p7O_IM: fprintf(fp, "\ntIM: "); break;
      case p7O_DM: fprintf(fp, "\ntDM: "); break;
      case p7O_MD: fprintf(fp, "\ntMD: "); break;
      case p7O_MI: fprintf(fp, "\ntMI: "); break;
      case p7O_II: fprintf(fp, "\ntII: "); break;
      }
      for (k = 1, q = 0; q < nq; q++, k++)
	{
	  switch (t) {
	  case p7O_MM:/* MM, DM, IM quads rotated by +1  */
	  case p7O_IM:
	  case p7O_DM:
		  kb = (1 + (nq+k-2)) % nq;
		  break;
	  case p7O_BM:/* the remaining ones are straight up  */
	  case p7O_MD:
	  case p7O_MI:
	  case p7O_II:
		  kb = k;
		  break;
	  }
	  fprintf(fp, "[ ");
	  for (z = 0; z < 16; z++) 
	    if (kb+z*nq <= M) fprintf(fp, "%*d ", width, kb+z*nq);
	    else              fprintf(fp, "%*s ", width, "xx");
	  fprintf(fp, "]");
	}
      fprintf(fp, "\n     ");	  
      for (q = 0; q < nq; q++)
	{
	  fprintf(fp, "[ ");
	  tmp.v = om->tfv_AVX_512[q*7 + t];
	  for (z = 0; z < 16; z++) fprintf(fp, "%*.*f ", width, precision, tmp.x[z]);
	  fprintf(fp, "]");
	}
      fprintf(fp, "\n");	  
    }

  /* DD transitions */
  fprintf(fp, "\ntDD: ");
  for (k =1, q = 0; q < nq; q++, k++)
    {
      fprintf(fp, "[ ");
      for (z = 0; z < 16; z++) 
	if (k+z*nq <= M) fprintf(fp, "%*d ", width, k+z*nq);
	else             fprintf(fp, "%*s ", width, "xx");
      fprintf(fp, "]");
    }
  fprintf(fp, "\n     ");	  
  for (j = nq*7, q = 0; q < nq; q++, j++)
    {
      fprintf(fp, "[ ");
      tmp.v = om->tfv_AVX_512[j];
      for (z = 0; z < 16; z++) fprintf(fp, "%*.*f ", width, precision, tmp.x[z]);
      fprintf(fp, "]");
    }
  fprintf(fp, "\n");	  
  
  /* Specials */
  fprintf(fp, "E->C: %*.*f    E->J: %*.*f\n", width, precision, om->xf[p7O_E][p7O_MOVE], width, precision, om->xf[p7O_E][p7O_LOOP]);
  fprintf(fp, "N->B: %*.*f    N->N: %*.*f\n", width, precision, om->xf[p7O_N][p7O_MOVE], width, precision, om->xf[p7O_N][p7O_LOOP]);
  fprintf(fp, "J->B: %*.*f    J->J: %*.*f\n", width, precision, om->xf[p7O_J][p7O_MOVE], width, precision, om->xf[p7O_J][p7O_LOOP]);
  fprintf(fp, "C->T: %*.*f    C->C: %*.*f\n", width, precision, om->xf[p7O_C][p7O_MOVE], width, precision, om->xf[p7O_C][p7O_LOOP]);
  fprintf(fp, "Q:     %d\n",   nq);  
  fprintf(fp, "M:     %d\n",   M);  
  return eslOK;
   #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}

/* Function:  p7_oprofile_Compare()
 * Synopsis:  Compare two optimized profiles for equality.
 * Incept:    SRE, Wed Jan 21 13:29:10 2009 [Janelia]
 *
 * Purpose:   Compare the contents of <om1> and <om2>; return 
 *            <eslOK> if they are effectively identical profiles,
 *            or <eslFAIL> if not.
 * 
 *            Floating point comparisons are done to a tolerance
 *            of <tol> using <esl_FCompare()>.
 *            
 *            If a comparison fails, an informative error message is
 *            left in <errmsg> to indicate why.
 *            
 *            Internal allocation sizes are not compared, only the
 *            data.
 *            
 * Args:      om1    - one optimized profile to compare
 *            om2    - the other
 *            tol    - floating point comparison tolerance; see <esl_FCompare()>
 *            errmsg - ptr to array of at least <eslERRBUFSIZE> characters.
 *            
 * Returns:   <eslOK> on effective equality;  <eslFAIL> on difference.
 */
int
p7_oprofile_Compare_avx512(const P7_OPROFILE *om1, const P7_OPROFILE *om2, float tol, char *errmsg)
{
 #ifdef HAVE_AVX512 
  int q, r, x, y;

  int Q4_AVX_512  = P7_NVF_AVX_512(om1->M);
  int Q8_AVX_512  = P7_NVW_AVX_512(om1->M);
  int Q16_AVX_512 = P7_NVB_AVX_512(om1->M); 
  union { __m512i v; uint8_t c[64]; } a16_AVX_512, b16_AVX_512;
  union { __m512i v; int16_t w[32];  } a8_AVX_512,  b8_AVX_512;
  union { __m512  v; float   x[16];  } a4_AVX_512,  b4_AVX_512;

  if (om1->mode      != om2->mode)      ESL_FAIL(eslFAIL, errmsg, "comparison failed: mode");
  if (om1->L         != om2->L)         ESL_FAIL(eslFAIL, errmsg, "comparison failed: L");
  if (om1->M         != om2->M)         ESL_FAIL(eslFAIL, errmsg, "comparison failed: M");
  if (om1->nj        != om2->nj)        ESL_FAIL(eslFAIL, errmsg, "comparison failed: nj");
  if (om1->abc->type != om2->abc->type) ESL_FAIL(eslFAIL, errmsg, "comparison failed: alphabet type");

  /* MSVFilter part */
  for (x = 0; x < om1->abc->Kp; x++)
    for (q = 0; q < Q16_AVX_512; q++)
      {
  a16_AVX_512.v = om1->rbv_AVX_512[x][q]; b16_AVX_512.v = om2->rbv_AVX_512[x][q];
  for (r = 0; r < 32; r++) if (a16_AVX_512.c[r] != b16_AVX_512.c[r]) ESL_FAIL(eslFAIL, errmsg, "comparison failed: rb[%d] elem %d", q, r);
      }
  if (om1->tbm_b     != om2->tbm_b)     ESL_FAIL(eslFAIL, errmsg, "comparison failed: tbm_b");
  if (om1->tec_b     != om2->tec_b)     ESL_FAIL(eslFAIL, errmsg, "comparison failed: tec_b");
  if (om1->tjb_b     != om2->tjb_b)     ESL_FAIL(eslFAIL, errmsg, "comparison failed: tjb_b");
  if (om1->scale_b   != om2->scale_b)   ESL_FAIL(eslFAIL, errmsg, "comparison failed: scale_b");
  if (om1->base_b    != om2->base_b)    ESL_FAIL(eslFAIL, errmsg, "comparison failed: base_b");
  if (om1->bias_b    != om2->bias_b)    ESL_FAIL(eslFAIL, errmsg, "comparison failed: bias_b");

  /* ViterbiFilter() part */

  for (x = 0; x < om1->abc->Kp; x++)
    for (q = 0; q < Q8_AVX_512; q++)
      {
  a8_AVX_512.v = om1->rwv_AVX_512[x][q]; b8_AVX_512.v = om2->rwv_AVX_512[x][q];
  for (r = 0; r < 32; r++) if (a8_AVX_512.w[r] != b8_AVX_512.w[r]) ESL_FAIL(eslFAIL, errmsg, "comparison failed: rw[%d] elem %d", q, r);
      }
  for (q = 0; q < 8*Q16_AVX_512; q++)
    {
      a8_AVX_512.v = om1->twv_AVX_512[q]; b8_AVX_512.v = om2->twv_AVX_512[q];
      for (r = 0; r < 32; r++) if (a8_AVX_512.w[r] != b8_AVX_512.w[r]) ESL_FAIL(eslFAIL, errmsg, "comparison failed: tw[%d] elem %d", q, r);
    }

  for (x = 0; x < p7O_NXSTATES; x++)
    for (y = 0; y < p7O_NXTRANS; y++)
      if (om1->xw[x][y] != om2->xw[x][y]) ESL_FAIL(eslFAIL, errmsg, "comparison failed: xw[%d][%d]", x, y);
  if (om1->scale_w   != om2->scale_w)   ESL_FAIL(eslFAIL, errmsg, "comparison failed: scale");
  if (om1->base_w    != om2->base_w)    ESL_FAIL(eslFAIL, errmsg, "comparison failed: base");
  if (om1->ddbound_w != om2->ddbound_w) ESL_FAIL(eslFAIL, errmsg, "comparison failed: ddbound_w");
  
  /* Forward/Backward part */

  for (x = 0; x < om1->abc->Kp; x++)
    for (q = 0; q < Q4_AVX_512; q++)
      {
  a4_AVX_512.v = om1->rfv_AVX_512[x][q]; b4_AVX_512.v = om2->rfv_AVX_512[x][q];
  for (r = 0; r < 16; r++) if (esl_FCompare(a4_AVX_512.x[r], b4_AVX_512.x[r], tol) != eslOK)  ESL_FAIL(eslFAIL, errmsg, "comparison failed: rf[%d] elem %d", q, r);
      }
  for (q = 0; q < 8*Q4_AVX_512; q++)
    {
      a4_AVX_512.v = om1->tfv_AVX_512[q]; b4_AVX_512.v = om2->tfv_AVX_512[q];
      for (r = 0; r < 16; r++) if (a4_AVX_512.x[r] != b4_AVX_512.x[r]) ESL_FAIL(eslFAIL, errmsg, "comparison failed: tf[%d] elem %d", q, r);
    }

  for (x = 0; x < p7O_NXSTATES; x++)
    if (esl_vec_FCompare(om1->xf[x], om2->xf[x], p7O_NXTRANS, tol) != eslOK) ESL_FAIL(eslFAIL, errmsg, "comparison failed: xf[%d] vector", x);

   for (x = 0; x < p7_NOFFSETS; x++)
     if (om1->offs[x] != om2->offs[x]) ESL_FAIL(eslFAIL, errmsg, "comparison failed: offs[%d]", x);

   if (esl_strcmp(om1->name,      om2->name)      != 0) ESL_FAIL(eslFAIL, errmsg, "comparison failed: name");
   if (esl_strcmp(om1->acc,       om2->acc)       != 0) ESL_FAIL(eslFAIL, errmsg, "comparison failed: acc");
   if (esl_strcmp(om1->desc,      om2->desc)      != 0) ESL_FAIL(eslFAIL, errmsg, "comparison failed: desc");
   if (esl_strcmp(om1->rf,        om2->rf)        != 0) ESL_FAIL(eslFAIL, errmsg, "comparison failed: ref");
   if (esl_strcmp(om1->mm,        om2->mm)        != 0) ESL_FAIL(eslFAIL, errmsg, "comparison failed: mm");
   if (esl_strcmp(om1->cs,        om2->cs)        != 0) ESL_FAIL(eslFAIL, errmsg, "comparison failed: cs");
   if (esl_strcmp(om1->consensus, om2->consensus) != 0) ESL_FAIL(eslFAIL, errmsg, "comparison failed: consensus");
   
   if (esl_vec_FCompare(om1->evparam, om2->evparam, p7_NEVPARAM, tol) != eslOK) ESL_FAIL(eslFAIL, errmsg, "comparison failed: evparam vector");
   if (esl_vec_FCompare(om1->cutoff,  om2->cutoff,  p7_NCUTOFFS, tol) != eslOK) ESL_FAIL(eslFAIL, errmsg, "comparison failed: cutoff vector");
   if (esl_vec_FCompare(om1->compo,   om2->compo,   p7_MAXABET,  tol) != eslOK) ESL_FAIL(eslFAIL, errmsg, "comparison failed: compo vector");

   return eslOK;
    #endif //HAVE_AVX512
  #ifndef HAVE_AVX512
  return eslENORESULT;
  #endif //HAVE_AVX512
}

/*------------ end, P7_OPROFILE debugging tools  ----------------*/





/*****************************************************************
 * @LICENSE@
 *   
 * SVN $Id$
 * SVN $URL$
 *****************************************************************/
