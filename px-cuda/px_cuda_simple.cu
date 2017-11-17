#include <string.h>
#include "easel.h"
#include "esl_dsqdata.h"
#include <x86intrin.h>
#include <math.h>
#include "esl_sse.h"
#include "hmmer.h"
#include "px_cuda.h"

#define MAX_BAND_WIDTH 2
#define NEGINFMASK 0x80808080
#define MAX(a, b, c)\
  a = (b > c) ? b:c;
//asm("max.f32 %0, %1, %2;" : "=f"(a): "f"(b), "f"(c));

//  a = (b > c) ? b:c;

#define NUM_REPS 10

char * restripe_char(char *source, int source_chars_per_vector, int dest_chars_per_vector, int source_length, int *dest_length);
int *restripe_char_to_int(char *source, int source_chars_per_vector, int dest_ints_per_vector, int source_length, int *dest_length);

__device__  uint calc_band_1(const __restrict__ uint8_t *dsq, int L, int Q, int q, uint ** rbv, volatile uint *residue_buffer){
  uint sv0 = NEGINFMASK, xE=NEGINFMASK, *rsc;
  int offset;
  // loop 1 of SSV

  rsc = rbv[*dsq] +(q <<5);
  if(threadIdx.x < 8){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();
  #pragma unroll 4
  for(offset = (q <<5); offset < (((Q-1)<<5)); dsq++){
    offset += 32;
    rsc = rbv[*dsq] + offset;
    sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);  
    if(threadIdx.x < 8){
      ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
    }
    __syncwarp();
    xE  = __vmaxs4(xE, sv0);     
  }
    //convert step
    sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);  
    xE  = __vmaxs4(xE, sv0);  
    sv0 = __byte_perm(sv0, __shfl_up_sync(0xffffffff, sv0, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
    if(threadIdx.x == 0){
      sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
    }
  //loop 2 of SSV
  rsc = rbv[*dsq];
  if(threadIdx.x < 8){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();
  offset = 0;
  #pragma unroll 4
  for(int row = Q-q; row < L-Q; row++){ // merge the nested loops in original SSV to support unrolling
    offset +=32;
    dsq++;
    rsc = rbv[*dsq] + offset;
    sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);  
    if(threadIdx.x < 8){
      ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
    }
    __syncwarp();
    xE  = __vmaxs4(xE, sv0);  
    if(offset == ((Q-1) <<5)){
      //convert step
      sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);  
      xE  = __vmaxs4(xE, sv0);  
      sv0 = __byte_perm(sv0, __shfl_up_sync(0xffffffff, sv0, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
      if(threadIdx.x == 0){
        sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
      }
      dsq++;
      rsc = rbv[*dsq];
      if(threadIdx.x < 8){
        ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
      }
      __syncwarp();
      row++;
      offset = 0;
    }
  }

  //Loop 3 of SSV
  rsc = rbv[*dsq];
  if(threadIdx.x < 8){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();
  #pragma unroll 4
  for(offset = 0; offset < ((Q-1) <<5);){
    offset += 32;
    dsq++;
    rsc = rbv[*dsq] + offset;

    sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);  
    if(threadIdx.x < 8){
      ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
    }
    __syncwarp(); 
    xE  = __vmaxs4(xE, sv0);
  }
  //convert step
  sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);  
  xE  = __vmaxs4(xE, sv0);  

  sv0 = __byte_perm(sv0, __shfl_up_sync(0xffffffff, sv0, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  if(threadIdx.x == 0){
    sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  }
  return xE;   
}

__device__  uint calc_band_2(const __restrict__ uint8_t *dsq, int L, int Q, int q, uint ** rbv, volatile uint *residue_buffer){
  uint sv0 = NEGINFMASK, sv1 = NEGINFMASK, xE=NEGINFMASK, *rsc;
  int offset;
  // loop 1 of SSV

  rsc = rbv[*dsq] + (q<<5);
  if(threadIdx.x < 16){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();
  #pragma unroll 4

  for(offset = (q <<5); offset < (((Q-2)<<5)); dsq++){
    offset += 32;
    rsc = rbv[*dsq] + offset;
    sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);
    sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]);  
    if(threadIdx.x < 16){
      ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
    }
    __syncwarp();
    xE  = __vmaxs4(xE, sv0); 
    xE  = __vmaxs4(xE, sv1);     
  }
    //convert step
  sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]); 
  sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]);  
  xE  = __vmaxs4(xE, sv0); 
  xE  = __vmaxs4(xE, sv1);   
  sv1 = __byte_perm(sv1, __shfl_up_sync(0xffffffff, sv1, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  if(threadIdx.x == 0){
    sv1 = __byte_perm(sv1, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  }
  dsq++;
  offset += 32;
  rsc = rbv[*dsq]+offset;
  if(threadIdx.x < 16){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();

  //convert step
  sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]); 
  sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]);  
  xE  = __vmaxs4(xE, sv0); 
  xE  = __vmaxs4(xE, sv1);   
  sv0 = __byte_perm(sv0, __shfl_up_sync(0xffffffff, sv0, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  if(threadIdx.x == 0){
    sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  }
  dsq++;

  //loop 2 of SSV
  rsc = rbv[*dsq];
  if(threadIdx.x < 16){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();
  offset = 0;
  #pragma unroll 4
  for(int row = Q-q; row < L-Q; row++){ // merge the nested loops in original SSV to support unrolling
    offset +=32;
    dsq++;
    rsc = rbv[*dsq] + offset;
    sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]);  
    sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]); 
    if(threadIdx.x < 16){
      ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
    }
    __syncwarp();
    xE  = __vmaxs4(xE, sv0);  
    xE  = __vmaxs4(xE, sv1);  

    if(offset >= ((Q-2) <<5)){
      //convert step
      sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]); 
      sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]);  
      xE  = __vmaxs4(xE, sv0); 
      xE  = __vmaxs4(xE, sv1);   
      sv1 = __byte_perm(sv1, __shfl_up_sync(0xffffffff, sv1, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
      if(threadIdx.x == 0){
        sv1 = __byte_perm(sv1, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
      }
      dsq++;
      offset+=32;
      rsc = rbv[*dsq]+offset;
      if(threadIdx.x < 16){
        ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
      }
      __syncwarp();
      row++;
      //convert step
      sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]); 
      sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]);  
      xE  = __vmaxs4(xE, sv0); 
      xE  = __vmaxs4(xE, sv1);   
      sv0 = __byte_perm(sv0, __shfl_up_sync(0xffffffff, sv0, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
      if(threadIdx.x == 0){
        sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
      }
      dsq++;
      offset=0;
      rsc = rbv[*dsq];
      if(threadIdx.x < 16){
        ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
      }
      __syncwarp();
      row++;
    }
  }

  //Loop 3 of SSV
  rsc = rbv[*dsq];
  if(threadIdx.x < 16){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();
//  #pragma unroll 4
  for(offset = 0; offset < ((Q-2) <<5);){
    offset += 32;
    dsq++;
    rsc = rbv[*dsq] + offset;
    sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]); 
    sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]); 
 
    if(threadIdx.x < 16){
      ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
    }
    __syncwarp(); 
    xE  = __vmaxs4(xE, sv0);
    xE  = __vmaxs4(xE, sv1);

  }
  //convert step
  sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]); 
  sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]);  
  xE  = __vmaxs4(xE, sv0); 
  xE  = __vmaxs4(xE, sv1);   
  sv1 = __byte_perm(sv1, __shfl_up_sync(0xffffffff, sv1, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  if(threadIdx.x == 0){
    sv1 = __byte_perm(sv1, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  }
  dsq++;
  offset +=32;
  rsc = rbv[*dsq]+offset;
  if(threadIdx.x < 16){
    ((uint4 *)residue_buffer)[threadIdx.x] = ((uint4*)rsc)[threadIdx.x];
  }
  __syncwarp();

  //convert step
  sv0   = __vaddss4(sv0, residue_buffer[threadIdx.x]); 
  sv1   = __vaddss4(sv1, residue_buffer[threadIdx.x+32]);  
  xE  = __vmaxs4(xE, sv0); 
  xE  = __vmaxs4(xE, sv1);   
  sv0 = __byte_perm(sv0, __shfl_up_sync(0xffffffff, sv0, 1), 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  if(threadIdx.x == 0){
    sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
  }
  return xE;   
}


__global__
void SSV_cuda(const __restrict__ uint8_t *dsq, int L, P7_OPROFILE *om, int8_t *retval){
  __shared__ uint4 shared_buffer[1024 *3];  //allocate one big lump that takes up all our shared memory
  int  Q = ((((om->M)-1) / (128)) + 1);
  int8_t *my_buffer= (int8_t*)(shared_buffer + (threadIdx.y * 96));
  uint *residue_buffer = (uint*) my_buffer;
  uint **rbv = (uint **)(my_buffer + 512); // residue buffer = 128B
  uint8_t *my_dsq_buffer = ((uint8_t *)rbv) + 224; // 27 ptrs, rounded up to multiple of 16 bytes.
  const uint8_t *dsq_ptr;
  // needs to scale w abc->Kp

  for(int i =0; i < 27; i++){
    rbv[i]=(uint *)(om->rbv[i]);
  }

  int q;
  int last_q = 0;
  uint xE = NEGINFMASK, sv_shuffle;
  int bands;
  int num_reps, i;


  for(num_reps = 0; num_reps < NUM_REPS; num_reps++){
    if(L <= 800){
      if(threadIdx.x == 0){
        memcpy(my_dsq_buffer, dsq, L);
      }
      __syncwarp();
      dsq_ptr = my_dsq_buffer;
    }
    else{
      dsq_ptr = dsq;
    }
  last_q = 0; // reset this at start of filter
  /* Use the highest number of bands but no more than MAX_BANDS */
  bands = (Q + MAX_BAND_WIDTH - 1) / MAX_BAND_WIDTH;
  for (i = 0; i < bands; i++) 
    {
      q      = (Q * (i + 1)) / bands;
      switch(q-last_q){
        case 1:
          xE = __vmaxs4(xE, calc_band_1(dsq_ptr, L, Q, last_q, rbv, residue_buffer));
          break;
       case 2:
          xE = __vmaxs4(xE, calc_band_2(dsq_ptr, L, Q, last_q, rbv, residue_buffer));
          break;

        default:
          printf("Illegal band width %d\n", q-last_q);
      }

      last_q = q;
    }
  }
// Done with main loop.  Now reduce answer vector (xE) to one byte for return
  // Reduce 32 4x8-bit quantities to 16
  sv_shuffle = __shfl_down(xE, 16); 
  if(threadIdx.x < 16){ // only bottom half of the cores continue from here
  

    xE = __vmaxs4(xE, sv_shuffle);

    // Reduce 6 4x8-bit quantities to 8
    sv_shuffle = __shfl_down(xE, 8); 
    if(threadIdx.x < 8){ // only bottom half of the cores continue from here
  

      xE = __vmaxs4(xE, sv_shuffle);

      // Reduce 8 4x8-bit quantities to 4
      sv_shuffle = __shfl_down(xE, 4); 
      if(threadIdx.x < 4){ // only bottom half of the cores continue from here

        xE = __vmaxs4(xE, sv_shuffle);

        // Reduce 4 4x8-bit quantities to 2
        sv_shuffle = __shfl_down(xE, 2); 
        if(threadIdx.x < 2){ // only bottom half of the cores continue from here

          xE = __vmaxs4(xE, sv_shuffle);
          // Reduce 2 4x8-bit quantities to 1

          sv_shuffle = __shfl_down(xE, 1);  
          if(threadIdx.x < 1){ // only bottom half of the cores continue from here

            xE = __vmaxs4(xE, sv_shuffle);

            // now, reduce the final 32 bit quantity to one 8-bit quantity.

            sv_shuffle = xE >> 16;

            xE = __vmaxs4(xE, sv_shuffle);

            sv_shuffle = xE >> 8;

            xE = __vmaxs4(xE, sv_shuffle);
            if((blockIdx.y == 0) &&(threadIdx.y ==0) && (threadIdx.x == 0)){ // only one thread writes result
              *retval = xE & 255; // low 8 bits of the word is the final result
            }
          }
        }
      }
    }
  }

  return; 
}  

/*
__global__ 
//__launch_bounds__(1024,2)
void SSV_cuda_takeone(const __restrict__ uint8_t *dsq, int L, const __restrict__ P7_OPROFILE *om, int8_t *retval){
  __shared__ uint shared_buffer[1024 *12];  //allocate one big lump that takes up all our shared memory
  int8_t *my_buffer; 
  my_buffer = (int8_t *)(shared_buffer + (threadIdx.y * 384));
  int8_t *dsq_ptr;
  int Q;
  uint sv_shuffle, *rsc;
  Q = ((((om->M)-1) / (128)) + 1);
  int xE, row, column, sv, sv0, sv1, sv2, sv3;
  uint **rbv = (uint **)my_buffer + (L+(sizeof(uint)-1))/sizeof(uint);
  for(int i =0; i < 27; i++){
    rbv[i]=(uint *)om->rbv[i];
  }
  for(int reps = 0; reps <NUM_REPS; reps++){
    if(threadIdx.x ==0){
      memcpy(my_buffer, (void *)dsq, L);
    }
    xE = NEGINFMASK; 

    for (int band = 0; band < Q/4; band+=4) {
      if(threadIdx.x==0){
              printf("Running band %d, Q/4 = %d\n", band, Q/4);
            }
        sv0 = NEGINFMASK;
        sv1 = NEGINFMASK;
        sv2 = NEGINFMASK;
        sv3 = NEGINFMASK;
        dsq_ptr = my_buffer;
        int offset;
        // loop 1 of SSV
        for(row = 0, offset = (band <<5)+threadIdx.x; row < (Q-band-4); row++){
            rsc = rbv[*dsq_ptr] + offset;
            offset += 128;
            dsq_ptr++;
            sv0   = __vaddss4(sv0, *rsc);
            sv1   = __vaddss4(sv1, *(rsc+32));
            sv2   = __vaddss4(sv2, *(rsc+64));
            sv3   = __vaddss4(sv3, *(rsc+96));
            xE  = __vmaxs4(xE, sv0); 
            xE  = __vmaxs4(xE, sv1); 
            xE  = __vmaxs4(xE, sv2);
            xE  = __vmaxs4(xE, sv3);      
        }
        //convert step
        rsc = rbv[*dsq_ptr] + offset;
        sv0   = __vaddss4(sv0, *rsc); 
        xE  = __vmaxs4(xE, sv);  
        sv_shuffle = __shfl_up(sv0, 1);  // Get the next core up's value of SV
        if(threadIdx.x == 0){
          sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of neginfmask in the low byte of sv
        }
        else{
          sv0 = __byte_perm(sv0, sv_shuffle, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
        }
        //loop 2 of SSV
        for(; row < L-Q; row++, dsq_ptr++){
          rsc = rbv[*dsq_ptr] + threadIdx.x;
          for(offset =threadIdx.x, column =0; column < Q-1; column++, row++){
            sv0   = __vaddss4(sv0, *rsc);
            sv1 =  __vaddss4(sv1, *(rsc+32));
            sv2 =  __vaddss4(sv2, *(rsc+64));
            sv3 =  __vaddss4(sv3, *(rsc+96));
            xE  = __vmaxs4(xE, sv0);  
            xE  = __vmaxs4(xE, sv1);  
            xE  = __vmaxs4(xE, sv2);  
            xE  = __vmaxs4(xE, sv3);   
            dsq_ptr++;
            offset +=128;
            rsc = rbv[*dsq_ptr] + offset;
            xE  = __vmaxs4(xE, sv);  
          }
           //convert step
          rsc = rbv[*dsq_ptr] + offset;
          sv0   = __vaddss4(sv0, *rsc); 
          xE  = __vmaxs4(xE, sv0);  

          sv_shuffle = __shfl_up(sv0, 1);  // Get the next core up's value of SV
          if(threadIdx.x == 0){
            sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of neginfmask in the low byte of sv
          }
          else{
            sv0 = __byte_perm(sv0, sv_shuffle, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
          }
        }

        //Loop 3 of SSV
        for(column = 0, offset = threadIdx.x; column < (Q-1); column++){
          rsc = rbv[my_buffer[row]] + offset;
          row++; 
          offset += 32;
          sv0   = __vaddss4(sv0, *rsc); 
          xE  = __vmaxs4(xE, sv0);
        }
        //convert step
        rsc = rbv[my_buffer[row]] + offset;
        sv0   = __vaddss4(sv0, *rsc); 
        xE  = __vmaxs4(xE, sv0);  
        sv_shuffle = __shfl_up(sv0, 1);  // Get the next core up's value of SV
        if(threadIdx.x == 0){
          sv0 = __byte_perm(sv0, NEGINFMASK, 0x2107); //left-shifts sv by one byte, puts the high byte of neginfmask in the low byte of sv
        }
        else{
          sv0 = __byte_perm(sv0, sv_shuffle, 0x2107); //left-shifts sv by one byte, puts the high byte of sv_shuffle in the low byte of sv
        }   
    }
  }

  // Done with main loop.  Now reduce answer vector (xE) to one byte for return
  // Reduce 32 4x8-bit quantities to 16
  sv_shuffle = __shfl_down(xE, 16); 
  if(threadIdx.x < 16){ // only bottom half of the cores continue from here
  

    xE = __vmaxs4(xE, sv_shuffle);

    // Reduce 6 4x8-bit quantities to 8
    sv_shuffle = __shfl_down(xE, 8); 
    if(threadIdx.x < 8){ // only bottom half of the cores continue from here
  

      xE = __vmaxs4(xE, sv_shuffle);

      // Reduce 8 4x8-bit quantities to 4
      sv_shuffle = __shfl_down(xE, 4); 
      if(threadIdx.x < 4){ // only bottom half of the cores continue from here

        xE = __vmaxs4(xE, sv_shuffle);

        // Reduce 4 4x8-bit quantities to 2
        sv_shuffle = __shfl_down(xE, 2); 
        if(threadIdx.x < 2){ // only bottom half of the cores continue from here

          xE = __vmaxs4(xE, sv_shuffle);
          // Reduce 2 4x8-bit quantities to 1

          sv_shuffle = __shfl_down(xE, 1);  
          if(threadIdx.x < 1){ // only bottom half of the cores continue from here

            xE = __vmaxs4(xE, sv_shuffle);

            // now, reduce the final 32 bit quantity to one 8-bit quantity.

            sv = xE >> 16;

            xE = __vmaxs4(xE, sv);

            sv = xE >> 8;

            xE = __vmaxs4(xE, sv);
            if((blockIdx.y == 0) &&(threadIdx.y ==0) && (threadIdx.x == 0)){ // only one thread writes result
              *retval = xE & 255; // low 8 bits of the word is the final result
            }
          }
        }
      }
    }
  }

  return;
}  
*/

// GPU kernel that copies values from the CPU version of an OPROFILE to one on the GPU.  Should generally only be called on one GPU core
__global__ void copy_oprofile_values_to_card(P7_OPROFILE *the_profile, float tauBM, float scale_b, float scale_w, int16_t base_w, int16_t ddbound_w, int L, int M, int V, int max_length, int allocM, int allocQb, int allocQw, int allocQf, int mode, float nj, int is_shadow, int8_t **rbv){

  the_profile->tauBM = tauBM;
  the_profile->scale_b = scale_b;
  the_profile->scale_w = scale_w;
  the_profile->base_w = base_w;
  the_profile->ddbound_w = ddbound_w;
  the_profile->L = L;
  the_profile->M = M;
  the_profile->V = V;
  the_profile->max_length = max_length;
  the_profile->allocM = allocM;
  the_profile->allocQb = allocQb;
  the_profile->allocQw = allocQw;
  the_profile->allocQf = allocQf;
  the_profile->mode = mode;
  the_profile->nj = nj;
  the_profile->is_shadow = is_shadow;
  the_profile->rbv = rbv;
}


// GPU kernel that initializes a filtermx structure
__global__ void initialize_filtermx_on_card(P7_FILTERMX *the_filtermx){
  the_filtermx->M = 0;
  the_filtermx->Vw = 64; // 32 cores * 32 bits = 1024 bits = 128 bytes = 64 * 16 bits
  the_filtermx->allocM = 0;
  the_filtermx->dp = NULL;
  the_filtermx->type = p7F_SSVFILTER;
}


// allocates and populates a P7_OPROFILE structure on a CUDA card that matches the one passed as its argument
P7_OPROFILE *create_oprofile_on_card(P7_OPROFILE *the_profile){
  P7_OPROFILE *cuda_OPROFILE;
  cudaError_t err;
  int Q = P7_Q(the_profile->M, the_profile->V);

  if(cudaMalloc(&cuda_OPROFILE, sizeof(P7_OPROFILE)) != cudaSuccess){

    err = cudaGetLastError();
    printf("Error: %s\n", cudaGetErrorString(err));
    p7_Fail((char *) "Unable to allocate memory in create_oprofile_on_card");
  }

  // allocate and copy over rbv 2-D array
  unsigned int **cuda_rbv;
  if(cudaMalloc(&cuda_rbv, the_profile->abc->Kp * sizeof(unsigned int *)) != cudaSuccess){
    p7_Fail((char *) "Unable to allocate memory in create_oprofile_on_card");
  }
  int i;
  char *restriped_rbv;
  int restriped_rbv_size;

  unsigned int **cuda_rbv_temp = cuda_rbv; // use this variable to copy rbv pointers into CUDA array 
  for(i = 0; i < the_profile->abc->Kp; i++){
    int *cuda_rbv_entry;
  restriped_rbv = restripe_char ((char*)(the_profile->rbv[i]), the_profile->V, 128, Q * the_profile->V, &restriped_rbv_size);
  //restriped_rbv = (int *) restripe_char((char *)(the_profile->rbv[i]), the_profile->V, 128, Q * the_profile->V, &restriped_rbv_size);

    if(cudaMalloc(&cuda_rbv_entry, restriped_rbv_size) != cudaSuccess){
      p7_Fail((char *) "Unable to allocate memory in create_oprofile_on_card");
    }

    if(cudaMemcpy(cuda_rbv_entry, restriped_rbv, restriped_rbv_size, cudaMemcpyHostToDevice) != cudaSuccess){
      p7_Fail((char *) "Unable to copy data in create_oprofile_on_card");
    }

    if(cudaMemcpy(cuda_rbv_temp, &cuda_rbv_entry, sizeof(int *) , cudaMemcpyHostToDevice) != cudaSuccess){
      p7_Fail((char *) "Unable to copy data in create_oprofile_on_card");
    }
    cuda_rbv_temp +=1;
  }
 

  // copy over base parameters.  Only call this kernel on one core because it just assigns values to fields in the data structure and has no parallelism
  copy_oprofile_values_to_card<<<1,1>>>(cuda_OPROFILE, the_profile->tauBM, the_profile->scale_b, the_profile->scale_w, the_profile->base_w, the_profile->ddbound_w, the_profile->L, the_profile->M, the_profile->V, the_profile->max_length, the_profile->allocM, the_profile->allocQb, the_profile->allocQw, the_profile->allocQf, the_profile->mode, the_profile->nj, the_profile->is_shadow, (int8_t **) cuda_rbv);

 return cuda_OPROFILE;
}

void destroy_oprofile_on_card(P7_OPROFILE *cpu_oprofile, P7_OPROFILE *cuda_oprofile){
  int i;
  for(i = 0; i < cpu_oprofile->abc->Kp; i++){
    cudaFree(cuda_oprofile->rbv[i]);
  }
  cudaFree(cuda_oprofile->rbv);
  cudaFree(cuda_oprofile);
}

P7_FILTERMX *create_filtermx_on_card(){
  P7_FILTERMX *the_filtermx;
  
  if(cudaMalloc(&the_filtermx, sizeof(P7_FILTERMX)) != cudaSuccess){
    p7_Fail((char *) "Unable to allocate memory in create_filtermx_on_card");
  }
  initialize_filtermx_on_card<<<1,1>>>(the_filtermx);
  return the_filtermx;
}


char * restripe_char(char *source, int source_chars_per_vector, int dest_chars_per_vector, int source_length, int *dest_length){
  char *dest;
  int dest_num_vectors, unpadded_dest_vectors;
  int source_num_vectors;
  source_num_vectors = source_length/source_chars_per_vector;
  if(source_num_vectors * source_chars_per_vector != source_length){
    source_num_vectors++;
  }
  unpadded_dest_vectors = source_length/dest_chars_per_vector;
  if(unpadded_dest_vectors * dest_chars_per_vector != source_length){
    unpadded_dest_vectors++;  //round up if source length isn't a multiple of the dest vector length
  }
 // printf("Unpadded_dest_vectors = %d. ", unpadded_dest_vectors);
  dest_num_vectors = unpadded_dest_vectors + MAX_BAND_WIDTH -1; // add extra vectors for SSV wrap-around

  dest = (char *) malloc(dest_num_vectors * dest_chars_per_vector);
  *dest_length = dest_num_vectors * dest_chars_per_vector;

  int source_pos, dest_pos;
  int i;

  for(i = 0; i < source_length; i++){
    source_pos = ((i % source_num_vectors) * source_chars_per_vector) + (i / source_num_vectors);
    dest_pos = ((i % unpadded_dest_vectors) * dest_chars_per_vector) + (i / unpadded_dest_vectors);
    dest[dest_pos] = (int) source[source_pos];
  }

  // pad out the dest vector with zeroes if necessary
  for(; i < unpadded_dest_vectors * dest_chars_per_vector; i++){
      dest_pos = ((i % unpadded_dest_vectors) * dest_chars_per_vector) + (i / unpadded_dest_vectors);
    //  printf("Padding 0 at location %d \n", dest_pos);
    dest[dest_pos] = 0;
  }

  // add the extra copies of the early vectors to support the SSV wrap-around
  for(int source = 0; i < dest_num_vectors * dest_chars_per_vector; i++){
    dest[i] = dest[source];
   // printf("Padding from location %d to location %d\n", source, i);
    source++;
  }

  return dest;

}


/*int *restripe_char_to_int(char *source, int source_chars_per_vector, int dest_ints_per_vector, int source_length, int *dest_length){
  int *dest;
  int dest_num_vectors, source_num_vectors, unpadded_dest_vectors;

  source_num_vectors = source_length/source_chars_per_vector;
  if(source_num_vectors * source_chars_per_vector != source_length){
    source_num_vectors++;
  }
  unpadded_dest_vectors = source_length/dest_ints_per_vector;
  if(unpadded_dest_vectors * dest_ints_per_vector != source_length){
    unpadded_dest_vectors++;  //round up if source length isn't a multiple of the dest vector length
  }
 // printf("Unpadded_dest_vectors = %d. ", unpadded_dest_vectors);
  dest_num_vectors = unpadded_dest_vectors + MAX_BAND_WIDTH -1; // add extra vectors for SSV wrap-around
  dest = (int *) malloc(dest_num_vectors * dest_ints_per_vector * sizeof(int));
  *dest_length = dest_num_vectors * dest_ints_per_vector *sizeof(int);
  //printf("Padded dest_num_vectors = %d. Dest_length = %d\n", dest_num_vectors, *dest_length);

  int source_pos, dest_pos;
  int i;

  for(i = 0; i < source_length; i++){
    source_pos = ((i % source_num_vectors) * source_chars_per_vector) + (i / source_num_vectors);
    dest_pos = ((i % unpadded_dest_vectors) * dest_ints_per_vector) + (i / unpadded_dest_vectors);
    dest[dest_pos] = (int) source[source_pos];
  }

  // pad out the dest vector with zeroes if necessary
  for(; i < unpadded_dest_vectors * dest_ints_per_vector; i++){
      dest_pos = ((i % unpadded_dest_vectors) * dest_ints_per_vector) + (i / unpadded_dest_vectors);
    //  printf("Padding 0 at location %d \n", dest_pos);
    dest[dest_pos] = 0;
  }

  // add the extra copies of the early vectors to support the SSV wrap-around
  for(int source = 0; i < dest_num_vectors * dest_ints_per_vector; i++){
    dest[i] = dest[source];
   // printf("Padding from location %d to location %d\n", source, i);
    source++;
  }

  return dest;
}
*/

int
p7_SSVFilter_shell_sse(const ESL_DSQ *dsq, int L, const __restrict__
  P7_OPROFILE *om, P7_FILTERMX *fx, float *ret_sc, P7_OPROFILE *card_OPROFILE, P7_FILTERMX *card_FILTERMX)
{
  int      Q          = P7_Q(om->M, p7_VWIDTH_SSE);
  __m128i  hv         = _mm_set1_epi8(-128);
  __m128i  neginfmask = _mm_insert_epi8( _mm_setzero_si128(), -128, 0);
  __m128i *dp;
  __m128i *rbv;
  __m128i  mpv;
  __m128i  sv;
  int8_t   h, *card_h;
  int      i,q;
  int      status;

  cudaEvent_t start, stop;
  cudaEventCreate(&start);
  cudaEventCreate(&stop);
  float milliseconds, seconds, gcups;
  //char *card_rbv= NULL;
  uint8_t *card_dsq;
  int warps_per_block;
  dim3 threads_per_block, num_blocks;
  cudaError_t err;
  if (( status = p7_filtermx_Reinit(fx, om->M) ) != eslOK) goto FAILURE;
  fx->M    = om->M;
  fx->Vw   = p7_VWIDTH_SSE / sizeof(int16_t); // A hack. FILTERMX wants Vw in units of int16_t. 
  fx->type = p7F_SSVFILTER;
  dp       = (__m128i *) fx->dp;
  cudaMalloc((void **) &card_h, 1);
  err = cudaGetLastError();
  cudaMalloc((void**)  &card_dsq, L+8);  //Pad out so that we can grab dsq four bytes at a time
  cudaMemcpy(card_dsq, (dsq+ 1), L, cudaMemcpyHostToDevice);

 // SSV_start_cuda<<<1, 32>>>(card_FILTERMX, (unsigned int *) card_hv, (unsigned int *) card_mpv, om->M);
  cudaEventRecord(start);
  num_blocks.x = 1;
  num_blocks.y = 20;
  num_blocks.z = 1;
  warps_per_block = 32;
  threads_per_block.x = 32;
  threads_per_block.y = warps_per_block;
  threads_per_block.z = 1;
  /*
  int **check_array, **check_array_cuda;
  check_array = (int **) malloc(L * sizeof(int *));
  cudaMalloc((void **) &check_array_cuda, (L * sizeof(int *)));
  for(int temp = 0; temp < L; temp++){
    int *row_array;
    cudaMalloc((void **) &row_array, (om->M * sizeof(int)));
    check_array[temp] = row_array;
  }
  cudaMemcpy(check_array_cuda, check_array, (L* sizeof(int)), cudaMemcpyHostToDevice);
 */

  SSV_cuda <<<num_blocks, threads_per_block>>>(card_dsq, L, card_OPROFILE, card_h);
  int8_t h_compare;
  cudaMemcpy(&h_compare, card_h, 1, cudaMemcpyDeviceToHost);
  cudaEventRecord(stop);

  cudaEventSynchronize(stop);
  milliseconds = 0;
  cudaEventElapsedTime(&milliseconds, start, stop);
  seconds = milliseconds/1000;
  gcups = ((((float) (om->M * L) *(float) NUM_REPS)/seconds)/1e9) * (float)(num_blocks.x * num_blocks.y *num_blocks.z) * (float)warps_per_block;
  printf("M = %d, L = %d, seconds = %f, GCUPS = %f\n", om->M, L, seconds, gcups); 
  

  err = cudaGetLastError();
  if(err != cudaSuccess){
    printf("Error: %s\n", cudaGetErrorString(err));
  }
 
  mpv = hv;
  for (q = 0; q < Q; q++)
    dp[q] = hv;

  for (i = 1; i <= L; i++)
    {
      rbv = (__m128i *) om->rbv[dsq[i]];

      
      for (q = 0; q < Q; q++)
        {
          sv    = _mm_adds_epi8(mpv, rbv[q]);
          hv    = _mm_max_epi8(hv, sv);
          mpv   = dp[q];
          dp[q] = sv;
        }  
      mpv = esl_sse_rightshift_int8(sv, neginfmask);

      // Compare CUDA result and SSE
 /*     cudaMemcpy(restriped_rbv, card_dp, card_rbv_length, cudaMemcpyDeviceToHost);
      for(j = 0; j < om->M; j++){
          int cuda_index = (j/card_Q) + ((j %card_Q) * 128);
          int cpu_index = (j/Q) + ((j %Q) * om->V);
          char *cuda_dp_char = (char *)restriped_rbv;
          char *cpu_dp_char = (char *) dp;
          char cuda_value = cuda_dp_char[cuda_index];
          char cpu_value = cpu_dp_char[cpu_index];
          if(cpu_value != cuda_value){
            printf("SSV dp miss-match at row %d, position %d: %d (CUDA) vs %d (CPU), indices were %d (CUDA), %d (CPU)\n", i, j, cuda_value, cpu_value, cuda_index, cpu_index);
          }
      }
*/
    //  free(restriped_rbv);
    }
  h = esl_sse_hmax_epi8(hv);
  cudaFree(card_h);
  cudaFree(card_dsq);
// cudaFree(check_array_cuda)

  if(h != h_compare){
    printf("Final result miss-match: %d (CUDA) vs %d (CPU)\n", h_compare, h);
  }

  if (h == 127)  
    { *ret_sc = eslINFINITY; return eslERANGE; }
  else if (h > -128)
    { 
      *ret_sc = ((float) h + 128.) / om->scale_b + om->tauBM - 2.0;   // 2.0 is the tauNN/tauCC "2 nat approximation"
      *ret_sc += 2.0 * logf(2.0 / (float) (L + 2));                   
      return eslOK;
    }
  else 
    {
      *ret_sc = -eslINFINITY;
      return eslOK;
    }
    
 FAILURE:
  *ret_sc = -eslINFINITY;
  return status;
}


static ESL_OPTIONS options[] = {
  /* name           type      default  env  range  toggles reqs incomp  help                               docgroup*/
  { (char *) "-h",        eslARG_NONE,  FALSE,  NULL, NULL,   NULL,  NULL, NULL,  (char *) "show brief help on version and usage",  0 },
  {  (char *) "-s",        eslARG_INT,      (char *) "0",  NULL, NULL,   NULL,  NULL, NULL,  (char *) "set random number seed to <n>",         0 },
  {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static char usage[]  = "[-options] <hmmfile> <seqfile>";
static char banner[] = "px, the first parallel tests of H4";

int
main(int argc, char **argv)
{
  ESL_GETOPTS    *go      = p7_CreateDefaultApp(options, 2, argc, argv, banner, usage);
  char           *hmmfile = esl_opt_GetArg(go, 1);
  char           *seqfile = esl_opt_GetArg(go, 2);
  ESL_ALPHABET   *abc     = NULL;
  P7_HMMFILE     *hfp     = NULL;
  P7_BG          *bg      = NULL;
  P7_HMM         *hmm     = NULL;
  P7_PROFILE     *gm      = NULL;
  P7_OPROFILE    *om      = NULL;
  ESL_DSQDATA    *dd      = NULL;
  P7_ENGINE      *eng     = NULL;
  ESL_DSQDATA_CHUNK *chu = NULL;
  int             ncore   = 1;
  int  i;
  int             status;

  /* Read in one HMM */
  if (p7_hmmfile_OpenE(hmmfile, NULL, &hfp, NULL) != eslOK) p7_Fail( (char *) "Failed to open HMM file %s", hmmfile);
  if (p7_hmmfile_Read(hfp, &abc, &hmm)            != eslOK) p7_Fail( (char *) "Failed to read HMM");
  
  /* Configure a profile from the HMM */
  bg = p7_bg_Create(abc);
  gm = p7_profile_Create (hmm->M, abc);
  om = p7_oprofile_Create(hmm->M, abc);
  p7_profile_Config   (gm, hmm, bg);
  p7_oprofile_Convert (gm, om);
  P7_OPROFILE *card_OPROFILE;
  card_OPROFILE = create_oprofile_on_card((P7_OPROFILE *) om);
  P7_FILTERMX *card_FILTERMX;
  card_FILTERMX = create_filtermx_on_card();
  p7_bg_SetFilter(bg, om->M, om->compo);

  //uint64_t sequence_id = 0;
  uint64_t num_hits = 0;
  int count;
  cudaGetDeviceCount(&count);
  printf("Found %d CUDA devices\n", count);
  /* Open sequence database */
  status = esl_dsqdata_Open(&abc, seqfile, ncore, &dd);
  if      (status == eslENOTFOUND) p7_Fail( (char *) "Failed to open dsqdata files:\n  %s",    dd->errbuf);
  else if (status == eslEFORMAT)   p7_Fail( (char *) "Format problem in dsqdata files:\n  %s", dd->errbuf);
  else if (status != eslOK)        p7_Fail( (char *) "Unexpected error in opening dsqdata (code %d)", status);

  eng = p7_engine_Create(abc, NULL, NULL, gm->M, 400);

  while (( status = esl_dsqdata_Read(dd, &chu)) == eslOK && num_hits < 5)  
    {
      for (i = 0; i < 5 /* chu->N */; i++)
	{
	  p7_bg_SetLength(bg, (int) chu->L[i]);            // TODO: remove need for cast
	  p7_oprofile_ReconfigLength(om, (int) chu->L[i]); //         (ditto)
	  
	  //	  printf("seq %d %s\n", chu->i0+i, chu->name[i]);
    float ssv_score;

    p7_SSVFilter_shell_sse(chu->dsq[i], chu->L[i], om, eng->fx ,&ssv_score, card_OPROFILE, card_FILTERMX);
	 

	  p7_engine_Reuse(eng);
    num_hits++;
	}
      esl_dsqdata_Recycle(dd, chu);
    }
    printf("Saw %ld sequences\n", num_hits);
  /*esl_dsqdata_Close(dd);
  p7_oprofile_Destroy(om);
  p7_profile_Destroy(gm);
  p7_hmm_Destroy(hmm);
  p7_bg_Destroy(bg);
  p7_hmmfile_Close(hfp);
  esl_alphabet_Destroy(abc);
  esl_getopts_Destroy(go); */
  //destroy_oprofile_on_card(om, card_OPROFILE);
  exit(0);
}





