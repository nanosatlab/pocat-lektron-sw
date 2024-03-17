/*
 * sgp.c
 *
 *  Created on: 18 mar. 2022
 *      Author: jose_
 */
#include "string.h"
#include "stdlib.h"
#include "sgp.h"
#include "math.h"
#include "satutl.h"
#include "flash.h"
#define ST_SIZE 256


#define PI 3.14159265358979323846
#define asind(x) (asin( x ) / PI * 180)
#define atan2d(x,y) (atan2(x,y) / PI * 180)


int sign(float a){
    if(a < 0.0)
        return -1;
    if(a > 0.0)
        return 1;
    return 0;
}

float* linspace(float x1, float x2, int n) {

 float *x = calloc(n, sizeof(float));

 float step = (x2 - x1) / (float)(n - 1);

 for (int i = 0; i < n; i++) {
     x[i] = x1 + ((float)i * step);
 }

return x;
}

float FMod2Pi(float x){
	float y;
	float kpi   = 3.14159265358979323846;
	float twoPi = 2*kpi;
	float k;
	k = floor(x/twoPi);
	y = x - k*twoPi;
	if( y < 0 ){
		y = y + twoPi;
	}
	return y;
}


float kepler(float u, float aYNSL, float aXNSL, float tol ){

	float c, s;
	float ePW = u;
	float delta = 1;
	float ktr = 1;
	while(abs(delta/ePW)>tol){
		c = cos(ePW);
		s = sin(ePW);
		delta = (u - aYNSL*c + aXNSL*s - ePW)/(1 - aYNSL*s - aXNSL*c);
		if(abs(delta)>=0.95){
			delta = 0.95*sign(delta);
		}
		ePW = ePW + delta;
		ktr = ktr + 1;
	}
	return ePW;
}

void updateTLE(tle_data *tle){

	uint8_t data[141];
	//TLE address = 0x08008020
	//read the data from the TLE
	Read_Flash(0x08008020, data, 141);
	//assignt the first 69 characters(the first TLE line) to the first line
	memcpy(tle->line1, data, 69);
	//assignt the last 69 characters(the second TLE line) to the second line
	for(int i = 70; i<138; i++){

		tle->line2[i] = data[i];
	}

}



void sgp(orbit_t orbit, int nPts, float *xvel, float *yvel, float *zvel,float *xcoord, float *ycoord, float *zcoord, float actualunixtime){

	tle_data tle;
	read_twoline(tle, &orbit);
    float x[nPts], y[nPts], z[nPts], vx[nPts], vy[nPts], vz[nPts];
    float p, fS0, wS0, LS, aYNSL, a, dT, aXNSL, L, e, cI0, sI0, e0Sq, a1, d1, a0, p0, q0, L0, aux, dFDT, dWDT, tol, cf, sf, ci, si, cuk, suk;
    float pos[3], pos2[3], pos3[3];
    float vel[3];
    float *tVec;
    float eps = powf(2,-52);
	float kE = 0.0743669161;
	float aE = 1.0;
	float j2 = 1.082616e-3;
	float j3 = -0.253881e-5;
	float twoThirds = 2.0/3.0;
	tVec = linspace(actualunixtime/60,(actualunixtime+1)/60,nPts);
	// Values independent of time since epoch
	cI0  = cos(orbit.eqinc);
	sI0  = sin(orbit.eqinc);
	e0Sq = powf(orbit.ecc,2);
	a1   = pow((kE/orbit.rev),twoThirds);
	d1   = 0.75*j2*powf((aE/a1),2)*(3*powf(cI0,2) - 1)/powf(1 - e0Sq, 1.5);
	a0   = a1*(1 - d1/3 - powf(d1,2) - (134/81)*powf(d1,3));
	p0   = a0*(1 - e0Sq);
	q0   = a0*(1 - orbit.ecc);
	L0   = orbit.mnan + orbit.argp + orbit.ascn;
	aux    = 3*j2*powf(aE/p0,2)*orbit.rev;
	dFDT = -aux*cI0/2;
	dWDT =  aux*(5*powf(cI0,2) - 1)/4;
	tol  = eps;
    float rK, uK, fK, iK, rDot, rFDot, u, ePW, c, s, eCosE, eSinE, eLSq, pL, r, sinU, cosU, sin2U, cos2U;

	for(int k = 0; k<nPts; k++){
		dT = tVec[k];
		a = a0*powf((orbit.rev/(orbit.rev + (2*orbit.n0Dot + 3*orbit.n0DDot*dT)*dT)),twoThirds);
		if( a > q0 ){
			e = 1 - q0/a;
		}else{
			e = 1e-6;
		}
		p     = a*(1 - powf(e,2));
		fS0   = orbit.ascn + dFDT*dT;
		wS0   = orbit.argp + dWDT*dT;
		LS    = L0 + (orbit.rev + dWDT + dFDT)*dT + powf(orbit.n0Dot*dT,2) + powf(orbit.n0DDot*dT,3);
		aux     = 0.5*(j3/j2)*aE*sI0/p;
		aYNSL = e*sin(wS0) - aux;
		aXNSL = e*cos(wS0);
		L     = FMod2Pi(LS - 0.5*aux*aXNSL*(3 + 5*cI0)/(1 + cI0));
		float u      = FMod2Pi(L - fS0);
		//SOLVE KEPLER'S EQUATION
		ePW    = kepler( u, aYNSL, aXNSL, tol );
		c      = cos(ePW);
		s      = sin(ePW);
		eCosE  = aXNSL*c + aYNSL*s;
		eSinE  = aXNSL*s - aYNSL*c;
		eLSq   = powf(aXNSL,2) + powf(aYNSL,2);
		pL     = a*(1 - eLSq);
		r      = a*(1 - eCosE);
		rDot   = kE*sqrt(a)*eSinE/r;
		rFDot  = kE*sqrt(pL)/r;
	    aux     = eSinE/(1 + sqrt(1-eLSq));
		sinU   = (a/r)*(s - aYNSL - aXNSL*aux);
		cosU   = (a/r)*(c - aXNSL + aYNSL*aux);
		u      = atan2(sinU,cosU);
		cos2U  = 2*powf(cosU,2) - 1;
		sin2U  = 2*sinU*cosU;
		aux      = j2*powf((aE/pL),2);
		rK     = r    + 0.25 *aux*pL*powf(sI0,2)*cos2U;
		uK     = u    - 0.125*aux*(7*powf(cI0,2) - 1)*sin2U;
		fK     = fS0  + 0.75 *aux*cI0*sin2U;
		iK     = orbit.eqinc + 0.75 *aux*sI0*cI0*cos2U;
	    cf = cos(fK); //cf
        sf = sin(fK); //sf
	    ci = cos(iK); //cI
	    si = sin(iK); //sI
	    pos[0] = -sf*ci; //M[0]
	    pos[1] = cf*ci; //M[1]
	    pos[2] = si; //M[2]
	    pos2[0] = cf; //N[0]
	    pos2[1] = sf; //N[1]
	    pos2[2] = 0;    //N[2]
	    cuk = cos(uK); //cUK
	    suk = sin(uK); //sUK
	    pos3[0] = pos[0]*suk+pos2[0]*cuk; //U
	    pos3[1] = pos[1]*suk+pos2[1]*cuk;
	    pos3[2] = pos[2]*suk+pos2[2]*cuk;
	    vel[0] = pos[0]*cuk-pos2[0]*suk;//V
	    vel[1] = pos[1]*cuk-pos2[1]*suk;
	    vel[2] = pos[2]*cuk-pos2[2]*suk;
	    vx[k]= pos3[0]*rDot + rFDot*vel[0];
	    vy[k]= pos3[1]*rDot + rFDot*vel[1];
	    vz[k]= pos3[2]*rDot + rFDot*vel[2];
	    x[k] = pos3[0]*rK*6378.135;
	    y[k] = pos3[1]*rK*6378.135;
	    z[k] = pos3[2]*rK*6378.135;

	}
	for(int i = 0; i<nPts; i++){
	    xcoord[i] = x[i];
	    ycoord[i] = y[i];
	    zcoord[i] = z[i];
	    xvel[i] = vx[i];
	    yvel[i] = vy[i];
	    zvel[i] = vz[i];
	}

}



int checkposition(orbit_t orbit, int nPts, float actualunixtime, float latmax, float latmin, float lonmax, float lonmin){

	float xcoord, ycoord, zcoord, xvel, yvel, zvel, lat, lon;

	sgp(orbit, nPts, &xvel, &yvel, &zvel, &xcoord, &ycoord, &zcoord, actualunixtime);
	lat = asind(zcoord/sqrt(pow(xcoord,2)+ pow(ycoord,2)+ pow(zcoord,2)));
	lon = atan2d(ycoord,xcoord);
	if( lat<= latmax && lat>= latmin && lon<=lonmax && lon>=lonmin){
		return 1;
	}
	return 0;

}
