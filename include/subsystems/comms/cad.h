#pragma once

typedef enum { CAD_RESULT_FREE = 0, CAD_RESULT_GIVEUP = 1 } cad_result_t;

cad_result_t cad_precheck(void);
