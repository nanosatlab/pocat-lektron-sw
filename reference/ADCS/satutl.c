/*
 * satutl.c
 *
 *  Created on: Apr 4, 2022
 *      Author: jose_
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sgp.h"
#include "orbit_propagators_utils.h"
#include "satutl.h"

static long i_read(char *str, int start, int stop);
static double d_read(char *str, int start, int stop);

#define ST_SIZE 256
#define PI 3.14159265359
#define dayToMin 1440


#define RAD(x) (x*PI180)
/* ====================================================================
   Read orbit parameters for "satno" in file "filename", return -1 if
   failed to find the corresponding data. Call with satno = 0 to get the
   next elements of whatever sort.
   ==================================================================== */

int read_twoline(tle_data tle, orbit_t *orb)
{
	long search_satno;
    static char search_with_0[ST_SIZE];
    static char search_without_0[ST_SIZE];

    char *st1, *st2;
    double bm, bx, cm, cx;

    st1 = tle.line1;
    st2 = tle.line2;

    search_satno = atol(st1+2);

    sprintf(search_without_0, "2 %5ld", search_satno);
    sprintf(search_with_0, "2 %05ld", search_satno);

    if((strncmp(st2, search_with_0, 7) != 0)) {
        if((strncmp(st2, search_without_0, 7) != 0)) {
            printf("Bad TLE input format\n");
            return -1;
        }
    }

    orb->ep_year = (int)i_read(st1, 19, 20);

    if(orb->ep_year < 57) orb->ep_year += 2000;
    else orb->ep_year += 1900;

    orb->ep_day =       d_read(st1, 21, 32);//epochDay

    bm = d_read(st1, 54, 59) * 1.0e-5;
    bx = d_read(st1, 60, 61);
    orb->bstar = bm * pow(10.0, bx);
    orb->n0Dot = d_read(st1, 54, 59)*2*PI/pow(dayToMin,2);//n0Dot
    cm = d_read(st1, 45, 52);
    cx = d_read(st1, 54, 59);
    orb->n0DDot = cm*pow(10.0,cx)*2*PI/pow(dayToMin,3)/1e5;//n0DDot

    orb->eqinc = RAD(d_read(st2,  9, 16));//i0
    orb->ascn = RAD(d_read(st2, 18, 25));//f0
    orb->ecc  =     d_read(st2, 27, 33) * 1.0e-7;//e0
    orb->argp = RAD(d_read(st2, 35, 42));//w0
    orb->mnan = RAD(d_read(st2, 44, 51));//M0
    orb->rev  =     d_read(st2, 53, 63)*2*PI/dayToMin;//n0
    orb->norb =     i_read(st2, 64, 68);//orbitRev

    orb->satno = search_satno;

    return 0;
}

/* ==================================================================
   Locate the first non-white space character, return location.
   ================================================================== */

//static char *st_start(char *buf)
//{
//    if(buf == '\0') return buf;
//
//    while(*buf != '\0' && isspace(*buf)) buf++;
//
//return buf;
//}

/* ==================================================================
   Mimick the FORTRAN formatted read (assumes array starts at 1), copy
   characters to buffer then convert.
   ================================================================== */

static long i_read(char *str, int start, int stop)
{
    long itmp=0;
    char *buf, *tmp;
    int ii;

    start--;    /* 'C' arrays start at 0 */
    stop--;

    tmp = buf = (char *)vector(stop-start+2, sizeof(char));

    for(ii = start; ii <= stop; ii++)
        {
        *tmp++ = str[ii];   /* Copy the characters. */
        }
    *tmp = '\0';            /* NUL terminate */

    itmp = atol(buf);       /* Convert to long integer. */
    free(buf);

return itmp;
}

/* ==================================================================
   Mimick the FORTRAN formatted read (assumes array starts at 1), copy
   characters to buffer then convert.
   ================================================================== */

static double d_read(char *str, int start, int stop)
{
    double dtmp=0;
    char *buf, *tmp;
    int ii;

    start--;
    stop--;

    tmp = buf = (char *)vector(stop-start+2, sizeof(char));

    for(ii = start; ii <= stop; ii++)
        {
        *tmp++ = str[ii];   /* Copy the characters. */
        }
    *tmp = '\0';            /* NUL terminate */

    dtmp = atof(buf);       /* Convert to long integer. */
    free(buf);

return dtmp;
}

/* ==================================================================
   Allocate and check an all-zero array of memory (storage vector).
   ================================================================== */

void *vector(size_t num, size_t size){

	void *ptr;

    ptr = calloc(num, size);
    if(ptr == NULL)
        {
       // fatal_error("vector: Allocation failed %u * %u", num, size);
        }

    return ptr;
}

/* ====================================================================== */

