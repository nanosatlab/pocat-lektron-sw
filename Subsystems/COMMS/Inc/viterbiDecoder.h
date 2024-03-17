#ifndef _VITERBI_DECODER_H_
#define _VITERBI_DECODER_H_

#include "convCodeParams.h"
#include "convHelpers.h"
#include <stdbool.h>

//Note, this is being written in pure C to match
//the output of Laminar and to avoid any extra C++
//logic.  Unfortunatly, that means we don't have access
//to templates.
//To sidestep this, we will define the code properties in
//another file which is included.  Only one instance is
//supported at a time using this method but some external
//scripting could be used to generate multiple discriptions

//***** Decoder Options *******
#define MAX_PKT_LEN_UNCODED_BITS (1024*16) //The max packet length in uncoded bits
#define TRACEBACK_LEN (5*K)

//For new traceback mechanism
#define TRACEBACK_BUFFER_LEN (MAX_PKT_LEN_UNCODED_BITS) //In bits
//TODO: Add support for block traceback
//When performing block like traceback, the block size is the TRACEBACK_BUFFER_LEN-TRACEBACK_LEN
//The traceback logic still occurs over the 
//***** End Options ******

#define NUM_STATES (POW2(k*S))

#define FORCE_NO_POPCNT_DECODER
// #define SIMPLE_MIN

//If true, k=1 specialized decoder will simplify the butterfly computation
//by relying on the input bit being included in each generator
//If this is not true for the generator polynomial being used, do NOT
//enable this
//See "Viterbi Decoding Techniques for the TMS320C55x DSP Generation" by Henry Hendrix
//for annother explanation.
#define USE_POLY_SYMMETRY

//TODO: Look into re-normalizing metrics.  Can we set an upper bound on the difference between nodes in the trellis?

#define MAX_EDGE_WEIGHT (n) //With hamming distance as the metric, the max difference occurs if all bits are different

#define MAX_PKT_LEN_SEGMENTS (MAX_PKT_LEN_UNCODED_BITS + S)

#if k==1
    //Using renormalization
    //in the k=1 implementation
    #define METRIC_TYPE uint8_t
    #define METRIC_MAX UINT8_MAX
#else
    #if MAX_EDGE_WEIGHT*MAX_PKT_LEN_SEGMENTS <= POW2(8)
        #define METRIC_TYPE uint8_t
    #elif MAX_EDGE_WEIGHT*MAX_PKT_LEN_SEGMENTS <= POW2(16)
        #define METRIC_TYPE uint16_t
    #elif MAX_EDGE_WEIGHT*MAX_PKT_LEN_SEGMENTS <= POW2(32)
        #define METRIC_TYPE uint32_t
    #else
        #define METRIC_TYPE uint64_t
    #endif
#endif

//This defines the index of the comparison to be used when evaluating edges.  For 2 codes bits, there are 4 possibilities, 00, 01, 10, 11.
#if MAX_EDGE_WEIGHT <= POW2(8)
    #define EDGE_METRIC_INDEX_TYPE uint8_t 
#elif MAX_EDGE_WEIGHT <= POW2(16)
    #define EDGE_METRIC_INDEX_TYPE uint16_t 
#elif MAX_EDGE_WEIGHT <= POW2(32)
    #define EDGE_METRIC_INDEX_TYPE uint32_t 
#else
    #define EDGE_METRIC_INDEX_TYPE uint64_t 
#endif

#define TRACEBACK_TYPE uint8_t
#define TRACEBACK_BITS 8
// #if TRACEBACK_LEN*k <= 8
//     #define TRACEBACK_TYPE uint8_t
// #elif TRACEBACK_LEN*k <= 16
//     #define TRACEBACK_TYPE uint16_t
// #elif TRACEBACK_LEN*k <= 32
//     #define TRACEBACK_TYPE uint32_t
// #else
//     #define TRACEBACK_TYPE uint64_t
// #endif

#if k==1
    #define VITERBI_DECODER_HARD viterbiDecoderHardButterflyk1
    #define VITERBI_INIT viterbiInitButterflyk1
    #define VITERBI_RESET resetViterbiDecoderHardButterflyk1
#else
    #define VITERBI_DECODER_HARD viterbiDecoderHard
    #define VITERBI_INIT viterbiInit
    #define VITERBI_RESET resetViterbiDecoderHard
#endif

#define TRACEBACK_BYTES ((TRACEBACK_BUFFER_LEN+S*k)/TRACEBACK_BITS + 1) //+1 to handle non-multiple of 8 in preproecessor.  TODO: Implement proper rounding

/**
 * State for the viterbi decoder between calls
 * 
 */
typedef struct{
    //Code Configuration
    #ifdef USE_POLY_SYMMETRY
        EDGE_METRIC_INDEX_TYPE edgeCodedBitsSymm[NUM_STATES/2];
    #endif
    EDGE_METRIC_INDEX_TYPE edgeCodedBits[POW2(k)][NUM_STATES];
    //This contains the coded bits corresponding to the given endge.
    //There is one entry for each edge with the ith edge of the jth node having index
    //j*2^k+i in this array.
    //The index stored points to the metric of the coded bits corresponding to the particular branch.
    //For n=2, there are 4 possible coded bit sequences corresponding to 00, 01, 10, 11
    //The hamming distance will only be computed once per possible coded bit segments
    //The codes bits can be used as an index into the computed hamming distances.
    //The edge index represents the coded bits to append to the traceback

    //Decoder State
    //Using 2 arrays which will be swapped in each itteration
    //Allows us to write to a des
    METRIC_TYPE nodeMetricsA[NUM_STATES] __attribute__ ((aligned (64)));
    TRACEBACK_TYPE traceBackA[NUM_STATES] __attribute__ ((aligned (64)));
    METRIC_TYPE nodeMetricsB[NUM_STATES];
    TRACEBACK_TYPE traceBackB[NUM_STATES];

    //Change these to be pointers to fixed sized arrays
    //See https://stackoverflow.com/questions/1810083/c-pointers-pointing-to-an-array-of-fixed-size
    //Is under K&R C section 5.12 "Complicated Declarations"
    METRIC_TYPE (* restrict nodeMetricsCur)[NUM_STATES];
    TRACEBACK_TYPE (* restrict traceBackCur)[NUM_STATES];
    METRIC_TYPE (* restrict nodeMetricsNext)[NUM_STATES];
    TRACEBACK_TYPE (* restrict traceBackNext)[NUM_STATES];

    unsigned int iteration; //Used to track when to start making traceback decisions
    unsigned int renormCounter;
    uint8_t decodeCarryOver;
    uint8_t decodeCarryOverCount;

    //Traceback as a series of  buffers
    //One buffer for each node but arranged such that
    //the different states are contiguous for a single access
    //in time (which helps with vectorization)
    //When traceback occurs, the traceback cursor is reset
    //Circular buffering and wraparound checking is therefore not required
    TRACEBACK_TYPE tracebackBufs[(TRACEBACK_BUFFER_LEN+S*k)][NUM_STATES] __attribute__ ((aligned (64)));
} viterbiHardState_t;

/**
 * @brief Performs hard decision viterbi decoding of the specified code.
 * 
 * @note The code is expected to begin in the specified beginning state and end in the 0 state.
 *       Ending in the 
 * 
 * @param codedSegments an array of k bit codedSegments.  Each segment is in a seperate byte.  BLOCK_SIZE k-bit segements are passed at a time.
 * @param uncoded an array of uncoded bytes.  It is asumed the transmission is in big endian order.  Within a k bit segment, it is assumed it was sent in big endian order so simply appending the index (uncoded bits) corresponding to the edge can simply be shifted onto the word.  If the coded message is not a multiple of the block size, fill the block with 0's.
 * @param segmentsIn The number of coded segements being provided, which may be less than the block length.
 * @param last If true, returns the remaining traceback and resets after this iteration
 * @returns The number of uncoded bytes returned
 */ 
int viterbiDecoderHard(viterbiHardState_t* restrict state, uint8_t* restrict codedSegments, uint8_t* restrict uncoded, int segmentsIn, bool last);

/**
 * @brief Swaps the node metric and traceback arrays.  Used to update both the node metrics and traceback arrays after a trellis iteration.
 * 
 * Using 2 buffers allows us to avoid copy operations from an intermediate array
 */
void swapViterbiArrays(viterbiHardState_t* state);

int viterbiConfigCheck();

void viterbiInit(viterbiHardState_t* state);

void resetViterbiDecoderHard(viterbiHardState_t* state);

uint8_t calcHammingDist(uint8_t a, uint8_t b, int bits);

int argminPathMetrics(const METRIC_TYPE (*metrics)[POW2(k)]);

int argminNodeMetrics(const METRIC_TYPE (*metrics)[NUM_STATES]);

int argmin2(const METRIC_TYPE (*metrics)[2]);
int argmin4(const METRIC_TYPE (*metrics)[4]);
int argmin8(const METRIC_TYPE (*metrics)[8]);
int argmin16(const METRIC_TYPE (*metrics)[16]);
int argmin32(const METRIC_TYPE (*metrics)[32]);
int argmin64(const METRIC_TYPE (*metrics)[64]);

//Include the specialization headers
#include "viterbiDecoderButterflyk1.h"

#endif