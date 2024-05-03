/*
 * igrf13.h
 *
 *  Created on: 3 abr. 2022
 *      Author: jose_
 */

#ifndef INC_IGRF13_H_
#define INC_IGRF13_H_

#include "orbit_propagators_utils.h"

/*  Subroutines used  */
void igrf13_ngdc(double jday, vec3 xsat_llh, vec3 *mag_eci, vec3 *mag_ecef);

#endif /* INC_IGRF13_H_ */
