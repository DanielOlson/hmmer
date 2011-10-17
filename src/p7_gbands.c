#include "p7_config.h"

#include "easel.h"

#include "p7_gbands.h"

P7_GBANDS *
p7_gbands_Create(int L, int M)
{
  P7_GBANDS *bnd           = NULL;
  int        init_segalloc = 4;
  int        init_rowalloc = 64;
  int        status;

  ESL_ALLOC(bnd, sizeof(P7_GBANDS));
  bnd->nseg  = 0;
  bnd->nrow  = 0;
  bnd->L     = L;
  bnd->M     = M;
  bnd->ncell = 0;
  bnd->imem  = NULL;
  bnd->kmem  = NULL;


  ESL_ALLOC(bnd->imem, sizeof(int) * init_segalloc * 2); /* *2: for ia, ib pairs */
  ESL_ALLOC(bnd->kmem, sizeof(int) * init_rowalloc * p7_GBANDS_NK);
  bnd->segalloc = init_segalloc;
  bnd->rowalloc = init_rowalloc;

  return bnd;
  
 ERROR:
  p7_gbands_Destroy(bnd);
  return NULL;
}

int
p7_gbands_Reinit(P7_GBANDS *bnd, int L, int M)
{
  /* Currently, no allocation depends on L, M; we just keep copies of them */
  bnd->nseg  = 0;
  bnd->nrow  = 0;
  bnd->L     = L;
  bnd->M     = M;
  bnd->ncell = 0;
  return eslOK;
}

int
p7_gbands_Reuse(P7_GBANDS *bnd)
{
  bnd->nseg  = 0;
  bnd->nrow  = 0;
  bnd->L     = 0;
  bnd->M     = 0;
  bnd->ncell = 0;
  return eslOK;
}

/* Function:  
 * Synopsis:  
 *
 * Purpose:   
 *            <p7_gbands_Append()> calls must be made in ascending <i> order,
 *            from <i == 1..L>.
 * Args:      
 *
 * Returns:   
 *
 * Throws:    (no abnormal error conditions)
 *
 * Xref:      
 */
int
p7_gbands_Append(P7_GBANDS *bnd, int i, int ka, int kb)
{
  int status;

  if (bnd->nseg == 0 || 
      i > 1 + bnd->imem[(bnd->nseg-1)*2 +1]) /* i > ib[cur_g] + 1; need to start a  new segment */
    {
      if (bnd->nseg == bnd->segalloc && (status = p7_gbands_GrowSegs(bnd)) != eslOK) goto ERROR;
      bnd->imem[bnd->nseg*2]   = i; /* ia */
      bnd->imem[bnd->nseg*2+1] = i; /* ib */
      bnd->nseg++;
    }
  else	/* else, append i onto previous segment by incrementing ib */
    bnd->imem[(bnd->nseg-1)*2+1] += 1; /* equiv to setting = i */

  if (bnd->nrow == bnd->rowalloc && (status = p7_gbands_GrowRows(bnd)) != eslOK) goto ERROR;
  bnd->kmem[bnd->nrow*p7_GBANDS_NK]   = ka;
  bnd->kmem[bnd->nrow*p7_GBANDS_NK+1] = kb;
  bnd->nrow  += 1;
  bnd->ncell += kb-ka+1;
  return eslOK;

 ERROR:
  return status;
}


int
p7_gbands_GrowSegs(P7_GBANDS *bnd)
{
  int new_segalloc = bnd->segalloc * 2; /* grow by doubling */
  int status;

  ESL_REALLOC(bnd->imem, sizeof(int) * new_segalloc * 2);
  bnd->segalloc = new_segalloc;
  return eslOK;
  
 ERROR:
  return status;
}

int
p7_gbands_GrowRows(P7_GBANDS *bnd)
{
  int new_rowalloc = bnd->rowalloc * 2;
  int status;

  ESL_REALLOC(bnd->kmem, sizeof(int) * new_rowalloc * p7_GBANDS_NK);
  bnd->rowalloc = new_rowalloc;
  return eslOK;

 ERROR:
  return status;
}

void
p7_gbands_Destroy(P7_GBANDS *bnd)
{
  if (bnd) {
    if (bnd->imem) free(bnd->imem);
    if (bnd->kmem) free(bnd->kmem);
    free(bnd);
  }
}



/* Function:  
 * Synopsis:  
 *
 * Purpose:   
 *           Also serves to demonstrate standard iteration method,
 *           over segments and rows.
 * Args:      
 *
 * Returns:   
 *
 * Throws:    (no abnormal error conditions)
 *
 * Xref:      
 */
int
p7_gbands_Dump(FILE *ofp, P7_GBANDS *bnd)
{
  int  g, i;
  int *bnd_ip = bnd->imem;
  int *bnd_kp = bnd->kmem;
  int  ia, ib;
  int  ka, kb;

  i = 0;
  for (g = 0; g < bnd->nseg; g++)
    {
      ia = *bnd_ip; bnd_ip++;
      ib = *bnd_ip; bnd_ip++;
      if (ia > i+1) fprintf(ofp, "...\n");
      
      for (i = ia; i <= ib; i++)
	{
	  ka = *bnd_kp; bnd_kp++;
	  kb = *bnd_kp; bnd_kp++;

	  fprintf(ofp, "%6d %6d %6d\n", i, ka, kb);
	}
    }
  if (i <= bnd->L) fprintf(ofp, "...\n");
  return eslOK;
}

    
  
