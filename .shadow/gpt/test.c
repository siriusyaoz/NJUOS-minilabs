#include "thread-sync.h"
#include "thread.h"
#include <stddef.h>
struct param {
  int step;
  int num_threads;
  float *out;
  float *inp;
  float *weight;
  float *bias;
  int b;
  int T;
  int C;
  int OC;
} param;

void compute_block(int tid) {
  int t = tid - 1;
  int step = param.step;
  float *out = param.out;
  float *inp = param.inp;
  float *weight = param.weight;
  float *bias = param.bias;
  int b = param.b;
  int T = param.T;
  int C = param.C;
  int OC = param.OC;

  int start = (tid - 1) * step;
  int end = (tid == param.num_threads) ? T : start + step;
  for (int t = start; t < end; t++) {
    float *out_bt = out + b * T * OC + t * OC;
    float *inp_bt = inp + b * T * C + t * C;
    for (int o = 0; o < OC; o++) {
      float val = (bias != NULL) ? bias[o] : 0.0f;
      float *wrow = weight + o * C;
      for (int i = 0; i < C; i++) {
        val += inp_bt[i] * wrow[i];
      }
      out_bt[o] = val;
    }
  }
}

void matmul_forward(float *out, float *inp, float *weight, float *bias, int B,
                    int T, int C, int OC) {
  // most of the running time is spent here and in matmul_backward
  // OC is short for "output channels"
  // inp is (B,T,C), weight is (OC, C), bias is (OC)
  // out will be (B,T,OC)
  int num_threads = 4;
  num_threads = (num_threads > T) ? T : num_threads;
  int step = T / num_threads;
  param = (struct param){
      step, num_threads, out, inp, weight, bias, B, T, C, OC,
  };
  for (int i = 0; i < num_threads; i++) {
    create(compute_block);
  }
  join();
}
