#ifndef _CONV_CODE_PARAMS_H_
#define _CONV_CODE_PARAMS_H_

#include <stdint.h>

//The following convolutional code perameters are named following the conventions in "Digital Communications" 4th Ed. by John G. Proakis, 2000, Chapter 8.2 "Convolutional Codes"

#define K (7) //Constraint length (in k bit chunks)
#define k (1) //Number of bits shifted into FSM at a time

#define S ((K)-1) //The number of k bit chunks included in the state (the semantics for the tapped delay includes the current input which has not yet become state)

#define n (2) //The number of coded output bits

#define Rc ((double) k/n) //The rate of the code (as a double)

#define STARTING_STATE (0) //The starting state of the encoder

//The generator polynomials
//See the corresponding C file
extern const uint64_t g[n];

#endif
