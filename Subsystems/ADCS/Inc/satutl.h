/*
 * satutl.h
 *
 *  Created on: 7 abr. 2022
 *      Author: jose_
 */
#include "sgp.h"

#ifndef INC_SATUTL_H_
#define INC_SATUTL_H_

#define ST_SIZE 256




void *vector(size_t num, size_t size);
int read_twoline(tle_data tle, orbit_t *orb);


#endif /* INC_SATUTL_H_ */
