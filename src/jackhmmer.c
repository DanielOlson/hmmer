/* jackhmmer: iterative search of a protein sequence against a protein database
 * 
 * SRE, Thu Dec 11 15:20:27 2008 [Janelia] [maestro Bear McCreary, conducting]
 * SVN $Id$
 */
#include "p7_config.h"

#include <stdio.h>
#include <stdlib.h>

#include "easel.h"
#include "esl_alphabet.h"
#include "esl_dmatrix.h"
#include "esl_getopts.h"
#include "esl_scorematrix.h"
#include "esl_sq.h"
#include "esl_sqio.h"
#include "esl_stopwatch.h"


#include "hmmer.h"

#define CONOPTS "--fast,--hand"                            /* Exclusive options for model construction                    */
#define EFFOPTS "--eent,--eclust,--eset,--enone"           /* Exclusive options for effective sequence number calculation */
#define WGTOPTS "--wgsc,--wblosum,--wpb,--wnone,--wgiven"  /* Exclusive options for relative weighting                    */
#define RNGOPTS "--Rdet,--Rseed,-Rarb"                     /* Exclusive options for controlling run-to-run variation      */

static ESL_OPTIONS options[] = {
  /* name           type         default   env  range   toggles     reqs   incomp                             help                                                  docgroup*/
  { "-h",           eslARG_NONE,   FALSE, NULL, NULL,      NULL,    NULL,  NULL,                          "show brief help on version and usage",                         1 },
  { "-o",           eslARG_OUTFILE,FALSE, NULL, NULL,      NULL,    NULL,  NULL,                          "direct output to file <f>, not stdout",                        1 },

  { "--popen",      eslARG_REAL,   "0.1", NULL, "0<=x<0.5",NULL,    NULL,  NULL,                          "gap open probability",                                         2 },
  { "--pextend",    eslARG_REAL,   "0.4", NULL, "0<=x<1",  NULL,    NULL,  NULL,                          "gap extend probability",                                       2 },
  { "--mxfile",     eslARG_INFILE,  NULL, NULL, NULL,      NULL,    NULL,  NULL,                          "substitution score matrix [default: BLOSUM62]",                2 },

  { "-E",           eslARG_REAL, "0.001", NULL, "x>0",     NULL,    NULL,  "--cut_ga,--cut_nc,--cut_tc",  "E-value cutoff for reporting significant sequence hits",       3 },
  { "-T",           eslARG_REAL,   FALSE, NULL, "x>0",     NULL,    NULL,  "--cut_ga,--cut_nc,--cut_tc",  "bit score cutoff for reporting significant sequence hits",     3 },
  { "-Z",           eslARG_REAL,   FALSE, NULL, "x>0",     NULL,    NULL,  NULL,                          "set # of comparisons done, for E-value calculation",           3 },
  { "--domE",       eslARG_REAL, "0.001", NULL, "x>0",     NULL,    NULL,  "--cut_ga,--cut_nc,--cut_tc",  "E-value cutoff for reporting individual domains",              3 },
  { "--domT",       eslARG_REAL,   FALSE, NULL, "x>0",     NULL,    NULL,  "--cut_ga,--cut_nc,--cut_tc",  "bit score cutoff for reporting individual domains",            3 },
  { "--domZ",       eslARG_REAL,   FALSE, NULL, "x>0",     NULL,    NULL,  NULL,                          "set # of significant seqs, for domain E-value calculation",    3 },
  { "--cut_ga",     eslARG_NONE,   FALSE, NULL, NULL,      NULL,    NULL,  "--seqE,--seqT,--domE,--domT", "use GA gathering threshold bit score cutoffs in <hmmfile>",    3 },
  { "--cut_nc",     eslARG_NONE,   FALSE, NULL, NULL,      NULL,    NULL,  "--seqE,--seqT,--domE,--domT", "use NC noise threshold bit score cutoffs in <hmmfile>",        3 },
  { "--cut_tc",     eslARG_NONE,   FALSE, NULL, NULL,      NULL,    NULL,  "--seqE,--seqT,--domE,--domT", "use TC trusted threshold bit score cutoffs in <hmmfile>",      3 },

  { "--max",        eslARG_NONE,   FALSE, NULL, NULL,      NULL,    NULL, "--F1,--F2,--F3",               "Turn all heuristic filters off (less speed, more power)",      4 },
  { "--F1",         eslARG_REAL,  "0.02", NULL, NULL,      NULL,    NULL, "--max",                        "Stage 1 (MSV) threshold: promote hits w/ P <= F1",             4 },
  { "--F2",         eslARG_REAL,  "1e-3", NULL, NULL,      NULL,    NULL, "--max",                        "Stage 2 (Vit) threshold: promote hits w/ P <= F2",             4 },
  { "--F3",         eslARG_REAL,  "1e-5", NULL, NULL,      NULL,    NULL, "--max",                        "Stage 3 (Fwd) threshold: promote hits w/ P <= F3",             4 },
  { "--biasfilter", eslARG_NONE,   FALSE, NULL, NULL,      NULL,    NULL, "--max",                        "turn on composition bias filter (more speed, less power)",     4 },
  { "--nonull2",    eslARG_NONE,    NULL, NULL, NULL,      NULL,    NULL,    NULL,                        "turn off biased composition score corrections",                4 },
/* Alternate model construction strategies */
  { "--fast",       eslARG_NONE,   FALSE, NULL, NULL,   CONOPTS,    NULL,    NULL, "assign cols w/ >= symfrac residues as consensus",       5 },
  { "--hand",       eslARG_NONE,"default",NULL, NULL,   CONOPTS,    NULL,    NULL, "manual construction (requires reference annotation)",   5 },
  { "--symfrac",    eslARG_REAL,   "0.5", NULL, "0<=x<=1", NULL,"--fast",    NULL, "sets sym fraction controlling --fast construction",     5 },
/* Alternate relative sequence weighting strategies */
  { "--wgsc",       eslARG_NONE,"default",NULL, NULL,   WGTOPTS,    NULL,    NULL, "Gerstein/Sonnhammer/Chothia tree weights",             6 },
  { "--wblosum",    eslARG_NONE,   FALSE, NULL, NULL,   WGTOPTS,    NULL,    NULL, "Henikoff simple filter weights",                       6 },
  { "--wpb",        eslARG_NONE,   FALSE, NULL, NULL,   WGTOPTS,    NULL,    NULL, "Henikoff position-based weights",                      6 },
  { "--wnone",      eslARG_NONE,   FALSE, NULL, NULL,   WGTOPTS,    NULL,    NULL, "don't do any relative weighting; set all to 1",        6 },
  { "--wgiven",     eslARG_NONE,   FALSE, NULL, NULL,   WGTOPTS,    NULL,    NULL, "use weights as given in MSA file",                     6 },
  { "--pbswitch",   eslARG_INT,   "1000", NULL,"n>0",      NULL,    NULL,    NULL, "set failover to efficient PB wgts at > <n> seqs",      6 },
  { "--wid",        eslARG_REAL,  "0.62", NULL,"0<=x<=1",  NULL,"--wblosum", NULL, "for --wblosum: set identity cutoff",                   6 },
/* Alternate effective sequence weighting strategies */
  { "--eent",       eslARG_NONE,"default",NULL, NULL,   EFFOPTS,    NULL,    NULL, "adjust eff seq # to achieve relative entropy target",  7 },
  { "--eclust",     eslARG_NONE,   FALSE, NULL, NULL,   EFFOPTS,    NULL,    NULL, "eff seq # is # of single linkage clusters",            7 },
  { "--enone",      eslARG_NONE,   FALSE, NULL, NULL,   EFFOPTS,    NULL,    NULL, "no effective seq # weighting: just use nseq",          7 },
  { "--eset",       eslARG_REAL,    NULL, NULL, NULL,   EFFOPTS,    NULL,    NULL, "set eff seq # for all models to <x>",                  7 },
  { "--ere",        eslARG_REAL,    NULL, NULL,"x>0",      NULL, "--eent",   NULL, "for --eent: set target relative entropy to <x>",       7 },
  { "--eX",         eslARG_REAL,   "6.0", NULL,"x>0",      NULL, "--eent","--ere", "for --eent: set minimum total rel ent param to <x>",   7 },
  { "--eid",        eslARG_REAL,  "0.62", NULL,"0<=x<=1",  NULL,"--eclust",  NULL, "for --eclust: set fractional identity cutoff to <x>",  7 },
/* Control of E-value calibration */
  { "--EvL",         eslARG_INT,   "100", NULL,"n>0",      NULL,    NULL,    NULL, "length of sequences for Viterbi Gumbel mu fit",                8 },   
  { "--EvN",         eslARG_INT,   "200", NULL,"n>0",      NULL,    NULL,    NULL, "number of sequences for Viterbi Gumbel mu fit",                8 },   
  { "--EfL",         eslARG_INT,   "100", NULL,"n>0",      NULL,    NULL,    NULL, "length of sequences for Forward exp tail mu fit",              8 },   
  { "--EfN",         eslARG_INT,   "200", NULL,"n>0",      NULL,    NULL,    NULL, "number of sequences for Forward exp tail mu fit",              8 },   
  { "--Eft",         eslARG_REAL, "0.04", NULL,"0<x<1",    NULL,    NULL,    NULL, "tail mass for Forward exponential tail mu fit",                8 },   
/* Control of run-to-run variation in RNG */
  { "--Rdet",       eslARG_NONE,"default",NULL, NULL,   RNGOPTS,    NULL,    NULL, "reseed RNG to minimize run-to-run stochastic variation",       9 },
  { "--Rseed",       eslARG_INT,    NULL, NULL, NULL,   RNGOPTS,    NULL,    NULL, "reseed RNG with fixed seed",                                   9 },
  { "--Rarb",       eslARG_NONE,    NULL, NULL, NULL,   RNGOPTS,    NULL,    NULL, "seed RNG arbitrarily; allow run-to-run stochastic variation",  9 },
/* Other options */
  { "--textw",       eslARG_INT,   "120", NULL, "n>=120",  NULL,    NULL,"--notextw","set max width of ASCII text output lines",                  10 },
  { "--notextw",    eslARG_NONE,    NULL, NULL, NULL,      NULL,    NULL,"--textw",  "unlimit ASCII text output line width",                      10 },
#ifdef HAVE_MPI
  // { "--stall",      eslARG_NONE,   FALSE, NULL, NULL,      NULL,  NULL,  NULL, "arrest after start: for debugging MPI under gdb",          4 },  
  // { "--mpi",        eslARG_NONE,   FALSE, NULL, NULL,      NULL,  NULL,  NULL, "run as an MPI parallel program",                           4 },
#endif 
 {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options] <query seqfile> <target seqdb>";
static char banner[] = "iteratively search a protein sequence against a protein database";

/* process_commandline()
 * Take argc, argv, and options; parse the command line;
 * display help/usage info.
 */
static void 
process_commandline(int argc, char **argv, ESL_GETOPTS **ret_go, char **ret_qfile, char **ret_dbfile)
{
  ESL_GETOPTS *go = NULL;

  if ((go = esl_getopts_Create(options))     == NULL)     p7_Die("Internal failure creating options object");
  if (esl_opt_ProcessCmdline(go, argc, argv) != eslOK)  { printf("Failed to parse command line: %s\n", go->errbuf); goto ERROR; }
  if (esl_opt_VerifyConfig(go)               != eslOK)  { printf("Failed to parse command line: %s\n", go->errbuf); goto ERROR; }

  /* help format: */
  if (esl_opt_GetBoolean(go, "-h") == TRUE) 
    {
      p7_banner(stdout, argv[0], banner);
      esl_usage(stdout, argv[0], usage);
      puts("\nwhere basic options are:");
      esl_opt_DisplayHelp(stdout, go, 1, 2, 120); /* 1= group; 2 = indentation; 120=textwidth*/

      puts("\nOptions controlling scoring system in iteration one:");
      esl_opt_DisplayHelp(stdout, go, 2, 2, 120); 

      puts("\nOptions controlling significance thresholds for reporting:");
      esl_opt_DisplayHelp(stdout, go, 3, 2, 120); 

      puts("\nOptions controlling acceleration heuristics:");
      esl_opt_DisplayHelp(stdout, go, 4, 2, 120); 

      puts("\nOptions controlling model construction after first iteration:");
      esl_opt_DisplayHelp(stdout, go, 5, 2, 120); 

      puts("\nOptions controlling relative weights in models after first iteration:");
      esl_opt_DisplayHelp(stdout, go, 6, 2, 120); 

      puts("\nOptions controlling effective seq number in models after first iteration:");
      esl_opt_DisplayHelp(stdout, go, 7, 2, 120); 

      puts("\nOptions controlling E value calibration:");
      esl_opt_DisplayHelp(stdout, go, 8, 2, 120); 

      puts("\nOptions controlling run-to-run variation due to random number generation:");
      esl_opt_DisplayHelp(stdout, go, 9, 2, 120); 

      puts("\nOther expert options:");
      esl_opt_DisplayHelp(stdout, go, 10, 2, 120); 
      exit(0);
    }

  if (esl_opt_ArgNumber(go)                 != 2)    { puts("Incorrect number of command line arguments.");    goto ERROR; }
  if ((*ret_qfile  = esl_opt_GetArg(go, 1)) == NULL) { puts("Failed to get <qfile> argument on command line"); goto ERROR; }
  if ((*ret_dbfile = esl_opt_GetArg(go, 2)) == NULL) { puts("Failed to get <seqdb> argument on command line"); goto ERROR; }

  *ret_go = go;
  return;
  
 ERROR:  /* all errors handled here are user errors, so be polite.  */
  esl_usage(stdout, argv[0], usage);
  puts("\nwhere basic options are:");
  esl_opt_DisplayHelp(stdout, go, 1, 2, 120); /* 1= group; 2 = indentation; 120=textwidth*/
  printf("\nTo see more help on available options, do %s -h\n\n", argv[0]);
  exit(1);  
}

static int
output_header(FILE *ofp, ESL_GETOPTS *go, char *qfile, char *dbfile)
{
  p7_banner(ofp, go->argv[0], banner);
  
  fprintf(ofp, "# query sequence file:             %s\n", qfile);
  fprintf(ofp, "# target sequence database:        %s\n", dbfile);
  if (! esl_opt_IsDefault(go, "-o"))          fprintf(ofp, "# output directed to file:          %s\n",      esl_opt_GetString(go, "-o"));
  if (! esl_opt_IsDefault(go, "--popen"))     fprintf(ofp, "# gap open probability:             %f\n",      esl_opt_GetReal  (go, "--popen"));
  if (! esl_opt_IsDefault(go, "--pextend"))   fprintf(ofp, "# gap extend probability:           %f\n",      esl_opt_GetReal  (go, "--pextend"));
  if (! esl_opt_IsDefault(go, "--mxfile"))    fprintf(ofp, "# subst score matrix:               %s\n",      esl_opt_GetString(go, "--mxfile"));
  if (! esl_opt_IsDefault(go, "-E"))          fprintf(ofp, "# sequence E-value threshold:    <= %g\n",      esl_opt_GetReal(go, "-E"));
  if (! esl_opt_IsDefault(go, "-T"))          fprintf(ofp, "# sequence bit score threshold:  <= %g\n",      esl_opt_GetReal(go, "-T"));
  if (! esl_opt_IsDefault(go, "-Z"))          fprintf(ofp, "# sequence search space set to:     %.0f\n",    esl_opt_GetReal(go, "-Z"));
  if (! esl_opt_IsDefault(go, "--domE"))      fprintf(ofp, "# domain E-value threshold:      <= %g\n",      esl_opt_GetReal(go, "--domE"));
  if (! esl_opt_IsDefault(go, "--domT"))      fprintf(ofp, "# domain bit score threshold:    <= %g\n",      esl_opt_GetReal(go, "--domT"));
  if (! esl_opt_IsDefault(go, "--domZ"))      fprintf(ofp, "# domain search space set to:       %.0f\n",    esl_opt_GetReal(go, "--domZ"));
  if (! esl_opt_IsDefault(go, "--cut_ga"))    fprintf(ofp, "# using GA bit score thresholds:    yes\n"); 
  if (! esl_opt_IsDefault(go, "--cut_nc"))    fprintf(ofp, "# using NC bit score thresholds:    yes\n");
  if (! esl_opt_IsDefault(go, "--cut_tc"))    fprintf(ofp, "# using TC bit score thresholds:    yes\n");
  if (! esl_opt_IsDefault(go, "--max"))       fprintf(ofp, "# Max sensitivity mode:             on [all heuristic filters off]\n");
  if (! esl_opt_IsDefault(go, "--F1"))        fprintf(ofp, "# MSV filter P threshold:        <= %g\n",      esl_opt_GetReal(go, "--F1"));
  if (! esl_opt_IsDefault(go, "--F2"))        fprintf(ofp, "# Vit filter P threshold:        <= %g\n",      esl_opt_GetReal(go, "--F2"));
  if (! esl_opt_IsDefault(go, "--F3"))        fprintf(ofp, "# Fwd filter P threshold:        <= %g\n",      esl_opt_GetReal(go, "--F3"));
  if (! esl_opt_IsDefault(go, "--biasfilter"))fprintf(ofp, "# biased composition HMM filter:    on\n");
  if (! esl_opt_IsDefault(go, "--nonull2"))   fprintf(ofp, "# null2 bias corrections:           off\n");
  if (! esl_opt_IsDefault(go, "--fast"))      fprintf(ofp, "# model architecture construction:  fast/heuristic\n");
  if (! esl_opt_IsDefault(go, "--hand"))      fprintf(ofp, "# model architecture construction:  hand-specified by RF annotation\n");
  if (! esl_opt_IsDefault(go, "--symfrac"))   fprintf(ofp, "# sym fraction for model structure: %.3f\n", esl_opt_GetReal(go, "--symfrac"));
  if (! esl_opt_IsDefault(go, "--wgsc"))      fprintf(ofp, "# relative weighting scheme:        G/S/C\n");
  if (! esl_opt_IsDefault(go, "--wblosum"))   fprintf(ofp, "# relative weighting scheme:        BLOSUM filter\n");
  if (! esl_opt_IsDefault(go, "--wpb"))       fprintf(ofp, "# relative weighting scheme:        Henikoff PB\n");
  if (! esl_opt_IsDefault(go, "--wnone"))     fprintf(ofp, "# relative weighting scheme:        none\n");
  if (! esl_opt_IsDefault(go, "--pbswitch"))  fprintf(ofp, "# num seqs to failover to PB wgts:  %d\n",   esl_opt_GetInteger(go, "--pbswitch"));
  if (! esl_opt_IsDefault(go, "--wid"))       fprintf(ofp, "# frac id cutoff for BLOSUM wgts:   %f\n",   esl_opt_GetReal(go, "--wid"));
  if (! esl_opt_IsDefault(go, "--eent"))      fprintf(ofp, "# effective seq number scheme:      entropy weighting\n");
  if (! esl_opt_IsDefault(go, "--eclust"))    fprintf(ofp, "# effective seq number scheme:      single linkage clusters\n");
  if (! esl_opt_IsDefault(go, "--enone"))     fprintf(ofp, "# effective seq number scheme:      none\n");
  if (! esl_opt_IsDefault(go, "--eset"))      fprintf(ofp, "# effective seq number:             set to %f\n", esl_opt_GetReal(go, "--eset"));
  if (! esl_opt_IsDefault(go, "--ere") )      fprintf(ofp, "# rel entropy target for eweights:  %f\n",        esl_opt_GetReal(go, "--ere"));
  if (! esl_opt_IsDefault(go, "--eX") )       fprintf(ofp, "# min total relentropy, eweights:   %f\n",        esl_opt_GetReal(go, "--eX"));
  if (! esl_opt_IsDefault(go, "--eid") )      fprintf(ofp, "# frac id cutoff for --eclust:      %f\n",        esl_opt_GetReal(go, "--eid"));
  if (! esl_opt_IsDefault(go, "--EvL") )      fprintf(ofp, "# seq length, Vit Gumbel mu fit:    %d\n",     esl_opt_GetInteger(go, "--EvL"));
  if (! esl_opt_IsDefault(go, "--EvN") )      fprintf(ofp, "# seq number, Vit Gumbel mu fit:    %d\n",     esl_opt_GetInteger(go, "--EvN"));
  if (! esl_opt_IsDefault(go, "--EfL") )      fprintf(ofp, "# seq length, Fwd exp tau fit:      %d\n",     esl_opt_GetInteger(go, "--EfL"));
  if (! esl_opt_IsDefault(go, "--EfN") )      fprintf(ofp, "# seq number, Fwd exp tau fit:      %d\n",     esl_opt_GetInteger(go, "--EfN"));
  if (! esl_opt_IsDefault(go, "--Eft") )      fprintf(ofp, "# tail mass for Fwd exp tau fit:    %f\n",     esl_opt_GetReal   (go, "--Eft"));
  if (! esl_opt_IsDefault(go, "--Rdet") )     fprintf(ofp, "# RNG seed (run-to-run variation):  reseed deterministically; minimize variation\n");
  if (! esl_opt_IsDefault(go, "--Rseed") )    fprintf(ofp, "# RNG seed (run-to-run variation):  reseed to %d\n", esl_opt_GetInteger(go, "--Rseed"));
  if (! esl_opt_IsDefault(go, "--Rarb") )     fprintf(ofp, "# RNG seed (run-to-run variation):  one arbitrary seed; allow run-to-run variation\n");
  if (! esl_opt_IsDefault(go, "--textw"))     fprintf(ofp, "# max ASCII text line length:       %d\n",     esl_opt_GetInteger(go, "--textw"));
  if (! esl_opt_IsDefault(go, "--notextw"))   fprintf(ofp, "# max ASCII text line length:       unlimited\n");
  fprintf(ofp, "# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\n\n");
  return eslOK;
}





int
main(int argc, char **argv)
{
  ESL_GETOPTS     *go       = NULL;               /* application configuration options        */
  FILE            *ofp      = NULL;               /* output file for results (default stdout) */
  char            *qfile    = NULL;               /* file to read query sequence from         */
  char            *dbfile   = NULL;               /* file to read sequence(s) from            */
  int              qformat  = eslSQFILE_FASTA;    /* format of qfile                          */
  int              dbformat = eslSQFILE_FASTA;    /* format of dbfile                         */
  ESL_SQFILE      *qfp      = NULL;		  /* open qfile                               */
  ESL_SQFILE      *dbfp     = NULL;               /* open dbfile                              */
  ESL_ALPHABET    *abc      = NULL;               /* sequence alphabet                        */
  P7_BG           *bg       = NULL;               /* null model                               */
  P7_BUILDER      *bld      = NULL;               /* HMM construction configuration           */
  P7_PIPELINE     *pli      = NULL;		  /* accelerated HMM/seq comparison pipeline  */
  P7_TOPHITS      *hitlist  = NULL;      	  /* top-scoring sequence hits                */
  ESL_SQ          *qsq      = NULL;               /* query sequence                           */
  ESL_SQ          *dbsq     = NULL;               /* target sequence                          */
  P7_HMM          *hmm      = NULL;               /* query HMM                                */
  P7_OPROFILE     *om       = NULL;               /* optimized query profile                  */
  ESL_MSA         *msa      = NULL;               /* multiple alignment of sig hits           */
  ESL_STOPWATCH   *w        = NULL;               /* for timing                               */
  int              textw;
  int              iteration;
  int              maxiterations = 3;
  int              status;

  /* Initializations */
  process_commandline(argc, argv, &go, &qfile, &dbfile);    
  abc     = esl_alphabet_Create(eslAMINO);
  bg      = p7_bg_Create(abc);
  w       = esl_stopwatch_Create();
  if (esl_opt_GetBoolean(go, "--notextw")) textw = 0;
  else                                     textw = esl_opt_GetInteger(go, "--textw");

  p7_FLogsumInit();
  esl_stopwatch_Start(w);

  /* Initialize builder configuration */
  bld = p7_builder_Create(go, abc);
  status = p7_builder_SetScoreSystem(bld, esl_opt_GetString(go, "--mxfile"), NULL, esl_opt_GetReal(go, "--popen"), esl_opt_GetReal(go, "--pextend"));
  if (status != eslOK) p7_Fail("Failed to set single query seq score system:\n%s\n", bld->errbuf);

  /* Open the output file */
  if (esl_opt_IsDefault(go, "-o")) ofp = stdout;
  else {
    ofp = fopen(esl_opt_GetString(go, "-o"), "w");
    if (ofp == NULL) p7_Fail("Failed to open output file %s for writing\n", esl_opt_GetString(go, "-o"));
  }

  /* Open the target sequence database for sequential access. */
  status =  esl_sqfile_OpenDigital(abc, dbfile, dbformat, p7_SEQDBENV, &dbfp);
  if      (status == eslENOTFOUND) p7_Fail("Failed to open target sequence database %s for reading\n",      dbfile);
  else if (status == eslEFORMAT)   p7_Fail("Target sequence database file %s is empty or misformatted\n",   dbfile);
  else if (status == eslEINVAL)    p7_Fail("Can't autodetect format of a stdin or .gz seqfile");
  else if (status != eslOK)        p7_Die("Unexpected error %d opening target sequence database file %s\n", status, dbfile);
  
  /* Read in one and only one query sequence, and build a model of it  */
  qsq = esl_sq_CreateDigital(abc);
  dbsq = esl_sq_CreateDigital(abc);
  status = esl_sqfile_OpenDigital(abc, qfile, qformat, NULL, &qfp);
  if      (status == eslENOTFOUND) p7_Fail("Failed to open sequence file %s for reading\n",      qfile);
  else if (status == eslEFORMAT)   p7_Fail("Sequence file %s is empty or misformatted\n",        qfile);
  else if (status == eslEINVAL)    p7_Fail("Can't autodetect format of a stdin or .gz seqfile");
  else if (status != eslOK)        p7_Die ("Unexpected error %d opening sequence file %s\n", status, qfile);

  status = esl_sqio_Read(qfp, qsq);
  if      (status == eslEOF)       p7_Fail("Query sequence file %s is empty?", qfile);
  else if (status == eslEFORMAT)   p7_Fail("Query seq file %s parse failed, line %ld:\n%s\n", qfile, qfp->linenumber, qfp->errbuf);
  else if (status != eslOK)        p7_Die ("Unexpected error %d reading seq file %s\n", status, qfile);
  esl_sqfile_Close(qfp);


  /* The first block of output: configuration, and what the query is  */
  output_header(ofp, go, qfile, dbfile);
  fprintf(ofp, "Query:       %s  [L=%ld]\n", qsq->name, (long) qsq->n);
  if (qsq->acc[0]  != '\0') fprintf(ofp, "Accession:   %s\n", qsq->acc);
  if (qsq->desc[0] != '\0') fprintf(ofp, "Description: %s\n", qsq->desc);  

  fprintf(ofp, "             building query model ... "); fflush(ofp);
  p7_SingleBuilder(bld, qsq, bg, &hmm, NULL, &om); /* bypass HMM - all we need is the model */
  fprintf(ofp, "[done]\n\n");
  

  for (iteration = 1; iteration <= maxiterations; iteration++)
    {       /* We enter each iteration with an HMM and its optimized profile. */

      /* Create processing pipeline and top hits list. (TODO: reuse rather than recreate) */
      hitlist = p7_tophits_Create();
      pli     = p7_pipeline_Create(go, om->M, 400, p7_SEARCH_SEQS);  /* 400 is a dummy length for now */
      p7_pli_NewModel(pli, om, bg);

      /* Run each target sequence through the pipeline */
      while ((status = esl_sqio_Read(dbfp, dbsq)) == eslOK)
	{ 
	  p7_pli_NewSeq(pli, dbsq);
	  p7_bg_SetLength(bg, dbsq->n);
	  p7_oprofile_ReconfigLength(om, dbsq->n);
  
	  p7_Pipeline(pli, om, bg, dbsq, hitlist);

	  esl_sq_Reuse(dbsq);
	  p7_pipeline_Reuse(pli);
	}
      if      (status == eslEFORMAT) p7_Fail("Target database file %s parse failed, line %ld:\n%s\n", dbfile, dbfp->linenumber, dbfp->errbuf);
      else if (status != eslEOF)     p7_Die ("Unexpected error %d reading target database seq file %s\n", status, dbfile);


      /* Print the results. */
      p7_tophits_Sort(hitlist);
      p7_tophits_Threshold(hitlist, pli);
      p7_tophits_Targets(ofp, hitlist, pli, bg, textw); fprintf(ofp, "\n\n");
      p7_tophits_Domains(ofp, hitlist, pli, bg, textw); fprintf(ofp, "\n\n");

      /* Create alignment of the top hits */
      p7_tophits_Alignment(hitlist, abc, &msa);
      esl_msa_Digitize(abc,msa);
      esl_msa_SetName(msa, "iteration%d\n", iteration);

      /* Throw away old model. Build new one. */
      p7_hmm_Destroy(hmm);      hmm = NULL;
      p7_oprofile_Destroy(om);  om  = NULL;
      p7_Builder(bld, msa, bg, &hmm, NULL, NULL, &om);
      
      esl_sqfile_Position(dbfp, 0); /* rewind */
      esl_msa_Destroy(msa);
      p7_pipeline_Destroy(pli);
      p7_tophits_Destroy(hitlist);
    }

  esl_stopwatch_Stop(w);
  p7_pli_Statistics(ofp, pli, w);
  
  esl_stopwatch_Destroy(w);
  p7_oprofile_Destroy(om);
  p7_hmm_Destroy(hmm);
  esl_sq_Destroy(dbsq);
  esl_sq_Destroy(qsq);
  p7_bg_Destroy(bg);
  esl_alphabet_Destroy(abc);
  if (! esl_opt_IsDefault(go, "-o"))  fclose(ofp);
  esl_sqfile_Close(dbfp);
  esl_getopts_Destroy(go);
  return eslOK;
}
