#ifndef _VITERBI_DECODER_BUTTERFLYk1_H_
#define _VITERBI_DECODER_BUTTERFLYk1_H_

#include "viterbiDecoder.h"

int viterbiDecoderHardButterflyk1(viterbiHardState_t* restrict state, uint8_t* restrict codedSegments, uint8_t* restrict uncoded, int segmentsIn, bool last);

void viterbiInitButterflyk1(viterbiHardState_t* state);

void resetViterbiDecoderHardButterflyk1(viterbiHardState_t* state);

// METRIC_TYPE minMetric(const METRIC_TYPE (*metrics)[NUM_STATES]);
// METRIC_TYPE minMetric2(const METRIC_TYPE (*metrics)[2]);
// METRIC_TYPE minMetric4(const METRIC_TYPE (*metrics)[4]);
// METRIC_TYPE minMetric8(const METRIC_TYPE (*metrics)[8]);
// METRIC_TYPE minMetric16(const METRIC_TYPE (*metrics)[16]);
// METRIC_TYPE minMetric32(const METRIC_TYPE (*metrics)[32]);
// METRIC_TYPE minMetric64(const METRIC_TYPE (*metrics)[64]);
METRIC_TYPE minMetricGeneric(const METRIC_TYPE (*metrics)[NUM_STATES]);

#endif