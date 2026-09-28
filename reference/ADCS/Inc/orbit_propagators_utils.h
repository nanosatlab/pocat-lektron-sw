/*
 * orbit_propagators_utils.h
 *
 *  Created on: 3 abr. 2022
 *      Author: jose_
 */

#ifndef __ORBIT_PROPAGATOR_H__
#define __ORBIT_PROPAGATOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "sgp.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#ifndef M_PI
#define M_PI  3.141592653589793
#endif /* MSDOS */

#define PI 3.14159265359

#define TWOPI   (2.0*PI)    /* Optimising compiler will deal with this! */
#define PB2     (0.5*PI)
#define PI180   (PI/180.0)

/* Geodetic transf. defines */
#define OMEGAE  7.29211586e-5                  //  Earth rotation rate in rad/s
//#define F 1/298.257223563                   //  WGS-84 Flattening.
#define ECCN (F*(2.0 - F))                  //  Eccentricity.
#define R_0 (double)6378137                 //  WGS-84 equatorial radius (m).
#define R_P (double)R_0*(1 - Fwgs_84)       //  Polar radius (m).

/* Defines for ecef2lla */
//#define A 6378137.0
//#define B 6356752.3142

#define M2KM 1/1000
#define KM2M 1000

#define sind(x) (sin(fmod((x),360) * PI / 180))
#define asind(x) (asin( x ) / PI * 180)
#define cosd(x) (cos(fmod((x),360) * PI / 180))
#define acosd(x) (acos( x ) / PI * 180)

#define     DEG2RAD(x)      (x * M_PI) / 180.0

typedef union __attribute__ ((__packed__)) _vec2{
    double raw[2];
    struct __attribute__ ((__packed__)) {
        double x;
        double y;
    }f;
}vec2;

typedef union __attribute__ ((__packed__)) _vec3{
    double raw[3];
    struct __attribute__ ((__packed__)) {
        double x;
        double y;
        double z;
    }f;
}vec3;

typedef union __attribute__ ((__packed__)) _xyz_t{

        double x;
        double y;
        double z;

}xyz_t;


typedef union __attribute__ ((__packed__)) _mat3{
    double raw[3][3];
    struct __attribute__ ((__packed__)) {
        double m_11;
        double m_12;
        double m_13;
        double m_21;
        double m_22;
        double m_23;
        double m_31;
        double m_32;
        double m_33;
    }f;
}mat3;

extern double SGDP4_jd0;

double      j_day(unsigned int unix_timestamp);
void        jd2date(double jday, int *year, int *month, int *day, int *hour, int *minute, int *second);

void        ecef2eci(double gst, vec3 ec_ecef, vec3 *vec_eci);
void        eci2llh(double jd_actual, vec3 eci, vec3 *llh);
void        ecef2llh(vec3 ecef, vec3 *llh);
void        llh2ecef(vec3 llh, vec3 *ecef);
void        llh2eci(double jd_actual, vec3 llh, vec3 *eci);
void        eci2ecef(double gst, vec3 vec_eci, vec3 *vec_ecef);
void        enu2ecef(vec3 sat_llh, vec3 vec_enu, vec3 *vec_ecef);

void        greenwidtchtime(double jd, double *gst);

//void        propagate_orbit(orbit_t *orbit_definition, double jd_actual, vec3 *sat_pos, vec3 *sat_vel);
void        sun_position(double julian_day, vec3 pos_llh, vec3 *sun_pos_eci);

#ifdef __cplusplus
}
#endif

#endif
