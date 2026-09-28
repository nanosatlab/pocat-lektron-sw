/*
 * sgp.h
 *
 *  Created on: 18 mar. 2022
 *      Author: jose_
 */



#ifndef INC_SGP_H_
#define INC_SGP_H_
#define ST_SIZE 256

typedef struct __attribute__ ((__packed__)) orbit_t {
	int ep_year;
	long norb;
	float ep_day;
	float bstar;
	float eqinc;
	float ascn;
	float ecc;
	float argp;
	float mnan;
	float rev;
	long satno;
	float n0Dot;
	float n0DDot;

}orbit_t;

typedef struct __attribute__ ((__packed__)) tle_data {
	char line1[ST_SIZE];
	char line2[ST_SIZE];

}tle_data;



float FMod2Pi(float x);

float* linspace(float x1, float x2, int n);

void sgp(orbit_t orbit, int nPts, float *xvel, float *yvel, float *zvel,float *xcoord, float *ycoord, float *zcoord, float actualunixtime);

int sign(float x);

float kepler(float u, float aYNSL, float aXNSL, float tol );

int checkposition(orbit_t orbit, int nPts, float actualunixtime, float latmax, float latmin, float lonmax, float lonmin);

void updateTLE(tle_data *tle);



#endif /* INC_SGP_H_ */

