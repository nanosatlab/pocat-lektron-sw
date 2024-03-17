/*
 * igrf13.c
 *
 *  Created on: 4 abr. 2022
 *      Author: jose_
 */

/****************************************************************************/
/*                                                                          */
/*     NGDC's Geomagnetic Field Modeling software for the IGRF and WMM      */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Disclaimer: This program has undergone limited testing. It is        */
/*     being distributed unoffically. The National Geophysical Data         */
/*     Center does not guarantee it's correctness.                          */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Version 7.0:                                                         */
/*     - input file format changed to                                       */
/*            -- accept new DGRF2005 coeffs with 0.01 nT precision          */
/*            -- make sure all values are separated by blanks               */
/*            -- swapped n and m: first is degree, second is order          */
/*     - new my_isnan function improves portablility                        */
/*     - corrected feet to km conversion factor                             */
/*     - fixed date conversion errors for yyyy,mm,dd format                 */
/*     - fixed lon/lat conversion errors for deg,min,sec format             */
/*     - simplified leap year identification                                */
/*     - changed comment: units of ddot and idot are arc-min/yr             */
/*     - added note that this program computes the secular variation as     */
/*            the 1-year difference, rather than the instantaneous change,  */
/*            which can be slightly different                               */
/*     - clarified that height is above ellipsoid, not above mean sea level */
/*            although the difference is negligible for magnetics           */
/*     - changed main(argv,argc) to usual definition main(argc,argv)        */
/*     - corrected rounding of angles close to 60 minutes                   */
/*     Thanks to all who provided bug reports and suggested fixes           */
/*                                                                          */
/*                                          Stefan Maus Jan-25-2010         */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Version 6.1:                                                         */
/*     Included option to read coordinates from a file and output the       */
/*     results to a new file, repeating the input and adding columns        */
/*     for the output                                                       */
/*                                          Stefan Maus Jan-31-2008         */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Version 6.0:                                                         */
/*     Bug fixes for the interpolation between models. Also added warnings  */
/*     for declination at low H and corrected behaviour at geogr. poles.    */
/*     Placed print-out commands into separate routines to facilitate       */
/*     fine-tuning of the tables                                            */
/*                                          Stefan Maus Aug-24-2004         */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*      This program calculates the geomagnetic field values from           */
/*      a spherical harmonic model.  Inputs required by the user are:       */
/*      a spherical harmonic model data file, coordinate preference,        */
/*      altitude, date/range-step, latitude, and longitude.                 */
/*                                                                          */
/*         Spherical Harmonic                                               */
/*         Model Data File       :  Name of the data file containing the    */
/*                                  spherical harmonic coefficients of      */
/*                                  the chosen model.  The model and path   */
/*                                  must be less than PATH chars.           */
/*                                                                          */
/*         Coordinate Preference :  Geodetic (WGS84 latitude and altitude   */
/*                                  above ellipsoid (WGS84),                */
/*                                  or geocentric (spherical, altitude      */
/*                                  measured from the center of the Earth). */
/*                                                                          */
/*         Altitude              :  Altitude above ellipsoid (WGS84). The   */
/*                                  program asks for altitude above mean    */
/*                                  sea level, because the altitude above   */
/*                                  ellipsoid is not known to most users.   */
/*                                  The resulting error is very small and   */
/*                                  negligible for most practical purposes. */
/*                                  If geocentric coordinate preference is  */
/*                                  used, then the altitude must be in the  */
/*                                  range of 6370.20 km - 6971.20 km as     */
/*                                  measured from the center of the earth.  */
/*                                  Enter altitude in kilometers, meters,   */
/*                                  or feet                                 */
/*                                                                          */
/*         Date                  :  Date, in decimal years, for which to    */
/*                                  calculate the values of the magnetic    */
/*                                  field.  The date must be within the     */
/*                                  limits of the model chosen.             */
/*                                                                          */
/*         Latitude              :  Entered in decimal degrees in the       */
/*                                  form xxx.xxx.  Positive for northern    */
/*                                  hemisphere, negative for the southern   */
/*                                  hemisphere.                             */
/*                                                                          */
/*         Longitude             :  Entered in decimal degrees in the       */
/*                                  form xxx.xxx.  Positive for eastern     */
/*                                  hemisphere, negative for the western    */
/*                                  hemisphere.                             */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*      Subroutines called :  degrees_to_decimal,julday,getshc,interpsh,    */
/*                            extrapsh,shval3,dihf,safegets                 */
/*                                                                          */
/****************************************************************************/

#include "igrf13.h"

static double coeff[54][7] = {
	    {1, 0, -29404.8000, 0.0000, 5.7000, 0.0000, 1},
	    {1, 1, -1450.9000, 4652.5000, 7.4000, -25.9000, 2},
	    {2, 0, -2499.6000, 0.0000, -11.0000, 0.0000, 3},
	    {2, 1, 2982.0000, -2991.6000, -7.0000, -30.2000, 4},
	    {2, 2, 1677.0000, -734.6000, -2.1000, -22.4000, 5},
	    {3, 0, 1363.2000, 0.0000, 2.2000, 0.0000, 6},
	    {3, 1, -2381.2000, -82.1000, -5.9000, 6.0000, 7},
	    {3, 2, 1236.2000, 241.9000, 3.1000, -1.1000, 8},
	    {3, 3, 525.7000, -543.4000, -12.0000, 0.5000, 9},
	    {4, 0, 903.0000, 0.0000, -1.2000, 0.0000, 10},
	    {4, 1, 809.5000, 281.9000, -1.6000, -0.1000, 11},
	    {4, 2, 86.3000, -158.4000, -5.9000, 6.5000, 12},
	    {4, 3, -309.4000, 199.7000, 5.2000, 3.6000, 13},
	    {4, 4, 48.0000, -349.7000, -5.1000, -5.0000, 14},
	    {5, 0, -234.3000, 0.0000, -0.3000, 0.0000, 15},
	    {5, 1, 363.2000, 47.7000, 0.5000, 0.0000, 16},
	    {5, 2, 187.8000, 208.3000, -0.6000, 2.5000, 17},
	    {5, 3, -140.7000, -121.2000, 0.2000, -0.6000, 18},
	    {5, 4, -151.2000, 32.3000, 1.3000, 3.0000, 19},
	    {5, 5, 13.5000, 98.9000, 0.9000, 0.3000, 20},
	    {6, 0, 66.0000, 0.0000, -0.5000, 0.0000, 21},
	    {6, 1, 65.5000, -19.1000, -0.3000, 0.0000, 22},
	    {6, 2, 72.9000, 25.1000, 0.4000, -1.6000, 23},
	    {6, 3, -121.5000, 52.8000, 1.3000, -1.3000, 24},
	    {6, 4, -36.2000, -64.5000, -1.4000, 0.8000, 25},
	    {6, 5, 13.5000, 8.9000, 0.0000, 0.0000, 26},
	    {6, 6, -64.7000, 68.1000, 0.9000, 1.0000, 27},
	    {7, 0, 80.6000, 0.0000, -0.1000, 0.0000, 28},
	    {7, 1, -76.7000, -51.5000, -0.2000, 0.6000, 29},
	    {7, 2, -8.2000, -16.9000, 0.0000, 0.6000, 30},
	    {7, 3, 56.5000, 2.2000, 0.7000, -0.8000, 31},
	    {7, 4, 15.8000, 23.5000, 0.1000, -0.2000, 32},
	    {7, 5, 6.4000, -2.2000, -0.5000, -1.1000, 33},
	    {7, 6, -7.2000, -27.2000, -0.8000, 0.1000, 34},
	    {7, 7, 9.8000, -1.8000, 0.8000, 0.3000, 35},
	    {8, 0, 23.7000, 0.0000, 0.0000, 0.0000, 36},
	    {8, 1, 9.7000, 8.4000, 0.1000, -0.2000, 37},
	    {8, 2, -17.6000, -15.3000, -0.1000, 0.6000, 38},
	    {8, 3, -0.5000, 12.8000, 0.4000, -0.2000, 39},
	    {8, 4, -21.1000, -11.7000, -0.1000, 0.5000, 40},
	    {8, 5, 15.3000, 14.9000, 0.4000, -0.3000, 41},
	    {8, 6, 13.7000, 3.6000, 0.3000, -0.4000, 42},
	    {8, 7, -16.5000, -6.9000, -0.1000, 0.5000, 43},
	    {8, 8, -0.3000, 2.8000, 0.4000, 0.0000, 44},
	    {9, 0, 5.0000, 0.0000, 0.0000, 0.0000, 45},
	    {9, 1, 8.4000, -23.4000, 0.0000, 0.0000, 46},
	    {9, 2, 2.9000, 11.0000, 0.0000, 0.0000, 47},
	    {9, 3, -1.5000, 9.8000, 0.0000, 0.0000, 48},
	    {9, 4, -1.1000, -5.1000, 0.0000, 0.0000, 49},
	    {9, 5, -13.2000, -6.3000, 0.0000, 0.0000, 50},
	    {9, 6, 1.1000, 7.8000, 0.0000, 0.0000, 51},
	    {9, 7, 8.8000, 0.4000, 0.0000, 0.0000, 52},
	    {9, 8, -9.3000, -1.4000, 0.0000, 0.0000, 53},
	    {9, 9, -11.9000, 9.6000, 0.0000, 0.0000, 54}
	};
#include "igrf13.h"

int my_isnan(double d)
{
    return (d != d);              /* IEEE: only NaN is not equal to itself */
}

#define NaN log(-1.0)
#define FT2KM (1.0/0.0003048)
#define RAD2DEG (180.0/PI)

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#define IEXT 0
#define FALSE 0
#define TRUE 1                  /* constants */
#define RECL 81

#define MAXINBUFF RECL + 14 + 15

#define EXT_COEFF1 (double)0
#define EXT_COEFF2 (double)0
#define EXT_COEFF3 (double)0

#define MAXDEG 9
#define MAXCOEFF (MAXDEG*(MAXDEG+2)+1) /* index starts with 1!, (from old Fortran?) */

/* Defining the needed model (to reduce computational time) */
#define MODEL "IGRF2020"
#define EPOCH 2020
#define MAX1 13
#define MAX2 8
#define MAX3 0
#define YRMIN 2020
#define YRMAX 2030
#define ALTMIN -1
#define ALTMAX 700

double gh1[MAXCOEFF];
double gh2[MAXCOEFF];
double gha[MAXCOEFF];              /* Geomag global variables */
double ghb[MAXCOEFF];
double d=0,f=0,h=0,i=0;
double dtemp,ftemp,htemp,itemp;
double x=0,y=0,z=0;
double xtemp,ytemp,ztemp;

static double julday(int month, int day, int year);
static int extrapsh();
static int shval3();
static int dihf();
static int getshc();

/****************************************************************************/
/*                                                                          */
/*                             Program Geomag                               */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*      This program, originally written in FORTRAN, was developed using    */
/*      subroutines written by                                              */
/*      A. Zunde                                                            */
/*      USGS, MS 964, Box 25046 Federal Center, Denver, Co.  80225          */
/*      and                                                                 */
/*      S.R.C. Malin & D.R. Barraclough                                     */
/*      Institute of Geological Sciences, United Kingdom.                   */
/*                                                                          */
/*      Translated                                                          */
/*      into C by    : Craig H. Shaffer                                     */
/*                     29 July, 1988                                        */
/*                                                                          */
/*      Rewritten by : David Owens                                          */
/*                     For Susan McLean                                     */
/*                                                                          */
/*      Maintained by: Adam Woods                                           */
/*      Contact      : geomag.models@noaa.gov                               */
/*                     National Geophysical Data Center                     */
/*                     World Data Center-A for Solid Earth Geophysics       */
/*                     NOAA, E/GC1, 325 Broadway,                           */
/*                     Boulder, CO  80303                                   */
/*                                                                          */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*      Some variables used in this program                                 */
/*                                                                          */
/*    Name         Type                    Usage                            */
/* ------------------------------------------------------------------------ */
/*                                                                          */
/*   a2,b2      Scalar Double          Squares of semi-major and semi-minor */
/*                                     axes of the reference spheroid used  */
/*                                     for transforming between geodetic or */
/*                                     geocentric coordinates.              */
/*                                                                          */
/*   minalt     Double array of MAXMOD Minimum height of model.             */
/*                                                                          */
/*   altmin     Double                 Minimum height of selected model.    */
/*                                                                          */
/*   altmax     Double array of MAXMOD Maximum height of model.             */
/*                                                                          */
/*   maxalt     Double                 Maximum height of selected model.    */
/*                                                                          */
/*   d          Scalar Double          Declination of the field from the    */
/*                                     geographic north (deg).              */
/*                                                                          */
/*   sdate      Scalar Double          start date inputted                  */
/*                                                                          */
/*   ddot       Scalar Double          annual rate of change of decl.       */
/*                                     (arc-min/yr)                         */
/*                                                                          */
/*   alt        Scalar Double          altitude above WGS84 Ellipsoid       */
/*                                                                          */
/*   epoch      Double array of MAXMOD epoch of model.                      */
/*                                                                          */
/*   ext        Scalar Double          Three 1st-degree external coeff.     */
/*                                                                          */
/*   latitude   Scalar Double          Latitude.                            */
/*                                                                          */
/*   longitude  Scalar Double          Longitude.                           */
/*                                                                          */
/*   gh1        Double array           Schmidt quasi-normal internal        */
/*                                     spherical harmonic coeff.            */
/*                                                                          */
/*   gh2        Double array           Schmidt quasi-normal internal        */
/*                                     spherical harmonic coeff.            */
/*                                                                          */
/*   gha        Double array           Coefficients of resulting model.     */
/*                                                                          */
/*   ghb        Double array           Coefficients of rate of change model.*/
/*                                                                          */
/*   i          Scalar Double          Inclination (deg).                   */
/*                                                                          */
/*   idot       Scalar Double          Rate of change of i (arc-min/yr).    */
/*                                                                          */
/*   igdgc      Integer                Flag for geodetic or geocentric      */
/*                                     coordinate choice.                   */
/*                                                                          */
/*   inbuff     Char a of MAXINBUF     Input buffer.                        */
/*                                                                          */
/*   irec_pos   Integer array of MAXMOD Record counter for header           */
/*                                                                          */
/*   stream  Integer                   File handles for an opened file.     */
/*                                                                          */
/*   fileline   Integer                Current line in file (for errors)    */
/*                                                                          */
/*   max1       Integer array of MAXMOD Main field coefficient.             */
/*                                                                          */
/*   max2       Integer array of MAXMOD Secular variation coefficient.      */
/*                                                                          */
/*   max3       Integer array of MAXMOD Acceleration coefficient.           */
/*                                                                          */
/*   mdfile     Character array of PATH  Model file name.                   */
/*                                                                          */
/*   minyr      Double                  Min year of all models              */
/*                                                                          */
/*   maxyr      Double                  Max year of all models              */
/*                                                                          */
/*   yrmax      Double array of MAXMOD  Max year of model.                  */
/*                                                                          */
/*   yrmin      Double array of MAXMOD  Min year of model.                  */
/*                                                                          */
/****************************************************************************/


void igrf12_ngdc(double jday, vec3 xsat_llh, vec3 *mag_eci, vec3 *mag_ecef)
{
    /*  Variable declaration  */
    int warn_H_strong;

    int max1, max2, nmax;
    int igdgc=3;

    long  irec_pos;

    double epoch, yrmin, yrmax;
    double minyr = 0, maxyr = 0;

    double altmin, altmax;
    double minalt, maxalt;
    double sdate=-1, alt=-999999, latitude=200, longitude=200;
    double ddot;
    double warn_H_val, warn_H_strong_val;

    double gst;

    int year, month, day, hour, minute, second;

    vec3 mag_enu;

    /* Initializations. */

    /*  Obtain the desired model file and read the data  */
    warn_H_val = 99999.0;
    warn_H_strong = 0;
    warn_H_strong_val = 99999.0;


    /* DESIRED MODEL: IGRF12 */
    irec_pos = 81;

    epoch = EPOCH;      /* epoch: 2015 */
    max1 = MAX1;        /* max1: 13 */
    max2 = MAX2;        /* max2: 8 */
    yrmin = YRMIN;      /* yrmin: 2020 */
    yrmax = YRMAX;      /* yrmax: 2030 */
    altmin = ALTMIN;    /* altmin: -1 (km) */
    altmax = ALTMAX;    /* altmax: 700 (km) */

    minyr=yrmin;
    maxyr=yrmax;

    /*** START ***/

    jd2date(jday, &year, &month, &day, &hour, &minute, &second);
    sdate = julday(month, day, year);

    if ((sdate < minyr) || (sdate >= maxyr + 1))
    {
        month=day=year=0;
        return;
    }
    /** GET INPUTS FOR IGRF12 **/

    /* Coordinate model: Geodetic (WGS84 latitude and altitude above mean sea level) */
    igdgc = 1;

    /* Latitude (-90 to 90) (- for Southern hemisphere) */
    latitude = xsat_llh.raw[0];

    if ((latitude < -90) || (latitude > 90))
    {
        return;
    }

    /* Longitude (-180 to 180) (- for Western hemisphere) */
    longitude = xsat_llh.raw[1];

    if ((longitude < -180) || (longitude > 180))
    {
        return;
    }

    /* Get altitude (in km's) min and max for selected model. */
    minalt=altmin;
    maxalt=altmax;

    /* Enter Altitude in km's */
    alt = xsat_llh.raw[2] * M2KM;

    if ((alt < minalt) || (alt > maxalt))
    {
        return;
    }

    /** This will compute everything needed for 1 point in time. **/
    getshc(1, irec_pos, max1, 1);
    getshc(0, irec_pos, max2, 2);

    nmax = extrapsh(sdate, epoch, max1, max2, 3);
    nmax = extrapsh(sdate+1, epoch, max1, max2, 4);

    /* Do the first calculations */
    shval3(igdgc, latitude, longitude, alt, nmax, 3,
           IEXT, EXT_COEFF1, EXT_COEFF2, EXT_COEFF3);
    dihf(3);
    shval3(igdgc, latitude, longitude, alt, nmax, 4,
           IEXT, EXT_COEFF1, EXT_COEFF2, EXT_COEFF3);
    dihf(4);

    ddot = ((dtemp - d) * RAD2DEG);

    if (ddot > 180.0) ddot -= 360.0;
    if (ddot <= -180.0) ddot += 360.0;

    ddot *= 60.0;

    d = d*(RAD2DEG);   i = i*(RAD2DEG);
    /* deal with geographic and magnetic poles */

    if (h < 100.0) /* at magnetic poles */
    {
        d = NaN;
        ddot = NaN;
        /* while rest is ok */
    }

    if (h < 1000.0)
    {
        warn_H_strong = 1;
        if (h<warn_H_strong_val) warn_H_strong_val = h;
    }
    else if (h < 5000.0 && !warn_H_strong)
    {
        if (h<warn_H_val) warn_H_val = h;
    }

    if (90.0-fabs(latitude) <= 0.001) /* at geographic poles */
    {
        x = NaN;
        y = NaN;
        d = NaN;
        ddot = NaN;
        warn_H_strong = 0;
        /* while rest is ok */
    }

    mag_enu.raw[0] =  y;
    mag_enu.raw[1] =  x;
    mag_enu.raw[2] = -z;
    enu2ecef(xsat_llh, mag_enu, mag_ecef);
    greenwidtchtime(jday, &gst);
    ecef2eci(gst, *mag_ecef, mag_eci);
}

/****************************************************************************/
/*                                                                          */
/*                           Subroutine julday                              */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Computes the decimal day of year from month, day, year.              */
/*     Supplied by Daniel Bergstrom                                         */
/*                                                                          */
/* References:                                                              */
/*                                                                          */
/* 1. Nachum Dershowitz and Edward M. Reingold, Calendrical Calculations,   */
/*    Cambridge University Press, 3rd edition, ISBN 978-0-521-88540-9.      */
/*                                                                          */
/* 2. Claus Tøndering, Frequently Asked Questions about Calendars,          */
/*    Version 2.9, http://www.tondering.dk/claus/calendar.html              */
/*                                                                          */
/****************************************************************************/

double julday(int month, int day, int year)
{
    int days[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    int leap_year = (((year % 4) == 0) &&
                     (((year % 100) != 0) || ((year % 400) == 0)));

    double day_in_year = (days[month - 1] + day + (month > 2 ? leap_year : 0));

    return ((double)year + (day_in_year / (365.0 + leap_year)));
}


/****************************************************************************/
/*                                                                          */
/*                           Subroutine getshc                              */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Reads spherical harmonic coefficients from the specified             */
/*     model into an array.                                                 */
/*                                                                          */
/*     Input:                                                               */
/*           stream     - Logical unit number                               */
/*           iflag      - Flag for SV equal to ) or not equal to 0          */
/*                        for designated read statements                    */
/*           strec      - Starting record number to read from model         */
/*           nmax_of_gh - Maximum degree and order of model                 */
/*                                                                          */
/*     Output:                                                              */
/*           gh1 or 2   - Schmidt quasi-normal internal spherical           */
/*                        harmonic coefficients                             */
/*                                                                          */
/*     FORTRAN                                                              */
/*           Bill Flanagan                                                  */
/*           NOAA CORPS, DESDIS, NGDC, 325 Broadway, Boulder CO.  80301     */
/*                                                                          */
/*     C                                                                    */
/*           C. H. Shaffer                                                  */
/*           Lockheed Missiles and Space Company, Sunnyvale CA              */
/*           August 15, 1988                                                */
/*                                                                          */
/****************************************************************************/

int getshc(iflag, strec, nmax_of_gh, gh)
int iflag;
long int  strec;
int       nmax_of_gh;
int       gh;
{
    char irat[9];
    int ii,m;
    int ios;
    double g,hh;
    int i;

    strcpy(irat, "IGRF2015");

    ii = 0;
    ios = 0;

    for(i = 0; i < 54; i++)
    {

        if (iflag == 1)
        {
            m        = (int)coeff[i][1];
            g        = coeff[i][2];
            hh       = coeff[i][3];
        }
        else
        {

            m        = (int)coeff[i][1];
            g        = coeff[i][4];
            hh       = coeff[i][5];
        }

        ii = ii + 1;
        switch(gh)
        {
            case 1:  gh1[ii] = g;
                break;
            case 2:  gh2[ii] = g;
                break;
            default: printf("\nError in subroutine getshc");
                break;
        }
        if (m != 0)
        {
            ii = ii+ 1;
            switch(gh)
            {
                case 1:  gh1[ii] = hh;
                    break;
                case 2:  gh2[ii] = hh;
                    break;
                default: printf("\nError in subroutine getshc");
                    break;
            }
        }
    }

    return(ios);
}

/****************************************************************************/
/*                                                                          */
/*                           Subroutine extrapsh                            */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Extrapolates linearly a spherical harmonic model with a              */
/*     rate-of-change model.                                                */
/*                                                                          */
/*     Input:                                                               */
/*           date     - date of resulting model (in decimal year)           */
/*           dte1     - date of base model                                  */
/*           nmax1    - maximum degree and order of base model              */
/*           gh1      - Schmidt quasi-normal internal spherical             */
/*                      harmonic coefficients of base model                 */
/*           nmax2    - maximum degree and order of rate-of-change model    */
/*           gh2      - Schmidt quasi-normal internal spherical             */
/*                      harmonic coefficients of rate-of-change model       */
/*                                                                          */
/*     Output:                                                              */
/*           gha or b - Schmidt quasi-normal internal spherical             */
/*                    harmonic coefficients                                 */
/*           nmax   - maximum degree and order of resulting model           */
/*                                                                          */
/*     FORTRAN                                                              */
/*           A. Zunde                                                       */
/*           USGS, MS 964, box 25046 Federal Center, Denver, CO.  80225     */
/*                                                                          */
/*     C                                                                    */
/*           C. H. Shaffer                                                  */
/*           Lockheed Missiles and Space Company, Sunnyvale CA              */
/*           August 16, 1988                                                */
/*                                                                          */
/****************************************************************************/

int extrapsh(date, dte1, nmax1, nmax2, gh)
double date;
double dte1;
int   nmax1;
int   nmax2;
int   gh;
{
    int   nmax;
    int   k, l;
    int   ii;
    double factor;

    factor = date - dte1;
    if (nmax1 == nmax2)
    {
        k =  nmax1 * (nmax1 + 2);
        nmax = nmax1;
    }
    else
    {
        if (nmax1 > nmax2)
        {
            k = nmax2 * (nmax2 + 2);
            l = nmax1 * (nmax1 + 2);
            switch(gh)
            {
                case 3:  for ( ii = k + 1; ii <= l; ++ii)
                {
                    gha[ii] = gh1[ii];
                }
                    break;
                case 4:  for ( ii = k + 1; ii <= l; ++ii)
                {
                    ghb[ii] = gh1[ii];
                }
                    break;
                default: printf("\nError in subroutine extrapsh");
                    break;
            }
            nmax = nmax1;
        }
        else
        {
            k = nmax1 * (nmax1 + 2);
            l = nmax2 * (nmax2 + 2);
            switch(gh)
            {
                case 3:  for ( ii = k + 1; ii <= l; ++ii)
                {
                    gha[ii] = factor * gh2[ii];
                }
                    break;
                case 4:  for ( ii = k + 1; ii <= l; ++ii)
                {
                    ghb[ii] = factor * gh2[ii];
                }
                    break;
                default: printf("\nError in subroutine extrapsh");
                    break;
            }
            nmax = nmax2;
        }
    }
    switch(gh)
    {
        case 3:  for ( ii = 1; ii <= k; ++ii)
        {
            gha[ii] = gh1[ii] + factor * gh2[ii];
        }
            break;
        case 4:  for ( ii = 1; ii <= k; ++ii)
        {
            ghb[ii] = gh1[ii] + factor * gh2[ii];
        }
            break;
        default: printf("\nError in subroutine extrapsh");
            break;
    }
    return(nmax);
}

/****************************************************************************/
/*                                                                          */
/*                           Subroutine shval3                              */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Calculates field components from spherical harmonic (sh)             */
/*     models.                                                              */
/*                                                                          */
/*     Input:                                                               */
/*           igdgc     - indicates coordinate system used; set equal        */
/*                       to 1 if geodetic, 2 if geocentric                  */
/*           latitude  - north latitude, in degrees                         */
/*           longitude - east longitude, in degrees                         */
/*           elev      - WGS84 altitude above ellipsoid (igdgc=1), or       */
/*                       radial distance from earth's center (igdgc=2)      */
/*           a2,b2     - squares of semi-major and semi-minor axes of       */
/*                       the reference spheroid used for transforming       */
/*                       between geodetic and geocentric coordinates        */
/*                       or components                                      */
/*           nmax      - maximum degree and order of coefficients           */
/*           iext      - external coefficients flag (=0 if none)            */
/*           ext1,2,3  - the three 1st-degree external coefficients         */
/*                       (not used if iext = 0)                             */
/*                                                                          */
/*     Output:                                                              */
/*           x         - northward component                                */
/*           y         - eastward component                                 */
/*           z         - vertically-downward component                      */
/*                                                                          */
/*     based on subroutine 'igrf' by D. R. Barraclough and S. R. C. Malin,  */
/*     report no. 71/1, institute of geological sciences, U.K.              */
/*                                                                          */
/*     FORTRAN                                                              */
/*           Norman W. Peddie                                               */
/*           USGS, MS 964, box 25046 Federal Center, Denver, CO.  80225     */
/*                                                                          */
/*     C                                                                    */
/*           C. H. Shaffer                                                  */
/*           Lockheed Missiles and Space Company, Sunnyvale CA              */
/*           August 17, 1988                                                */
/*                                                                          */
/****************************************************************************/

int shval3(igdgc, flat, flon, elev, nmax, gh, iext, ext1, ext2, ext3)
int   igdgc;
double flat;
double flon;
double elev;
int   nmax;
int   gh;
int   iext;
double ext1;
double ext2;
double ext3;
{
    double earths_radius = 6371.2;
    double dtr = 0.01745329;
    double slat;
    double clat;
    double ratio;
    double aa, bb, cc, dd;
    double sd;
    double cd;
    double r;
    double a2;
    double b2;
    double rr = 0;
    double fm,fn = 0.0;
    double sl[14];
    double cl[14];
    double p[119];
    double q[119];
    int ii,j,k,l,m,n;
    int npq;
    int ios;
    double argument;
    double power;
    a2 = 40680631.59;            /* WGS84 */
    b2 = 40408299.98;            /* WGS84 */
    ios = 0;
    r = elev;
    argument = flat * dtr;
    slat = sin( argument );
    if ((90.0 - flat) < 0.001)
    {
        aa = 89.999;            /*  300 ft. from North pole  */
    }
    else
    {
        if ((90.0 + flat) < 0.001)
        {
            aa = -89.999;        /*  300 ft. from South pole  */
        }
        else
        {
            aa = flat;
        }
    }
    argument = aa * dtr;
    clat = cos( argument );
    argument = flon * dtr;
    sl[1] = sin( argument );
    cl[1] = cos( argument );
    switch(gh)
    {
        case 3:  x = 0;
            y = 0;
            z = 0;
            break;
        case 4:  xtemp = 0;
            ytemp = 0;
            ztemp = 0;
            break;
        default: printf("\nError in subroutine shval3");
            break;
    }
    sd = 0.0;
    cd = 1.0;
    l = 1;
    n = 0;
    m = 1;
    npq = (nmax * (nmax + 3)) / 2;
    if (igdgc == 1)
    {
        aa = a2 * clat * clat;
        bb = b2 * slat * slat;
        cc = aa + bb;
        argument = cc;
        dd = sqrt( argument );
        argument = elev * (elev + 2.0 * dd) + (a2 * aa + b2 * bb) / cc;
        r = sqrt( argument );
        cd = (elev + dd) / r;
        sd = (a2 - b2) / dd * slat * clat / r;
        aa = slat;
        slat = slat * cd - clat * sd;
        clat = clat * cd + aa * sd;
    }
    ratio = earths_radius / r;
    argument = 3.0;
    aa = sqrt( argument );
    p[1] = 2.0 * slat;
    p[2] = 2.0 * clat;
    p[3] = 4.5 * slat * slat - 1.5;
    p[4] = 3.0 * aa * clat * slat;
    q[1] = -clat;
    q[2] = slat;
    q[3] = -3.0 * clat * slat;
    q[4] = aa * (slat * slat - clat * clat);

    for ( k = 1; k <= npq; ++k)
    {
        if (n < m)
        {
            m = 0;
            n = n + 1;
            argument = ratio;
            power =  n + 2;
            rr = pow(argument,power);
            fn = n;
        }
        fm = m;
        if (k >= 5)
        {
            if (m == n)
            {
                argument = (1.0 - 0.5/fm);
                aa = sqrt( argument );
                j = k - n - 1;
                p[k] = (1.0 + 1.0/fm) * aa * clat * p[j];
                q[k] = aa * (clat * q[j] + slat/fm * p[j]);
                sl[m] = sl[m-1] * cl[1] + cl[m-1] * sl[1];
                cl[m] = cl[m-1] * cl[1] - sl[m-1] * sl[1];
            }
            else
            {
                argument = fn*fn - fm*fm;
                aa = sqrt( argument );
                argument = ((fn - 1.0)*(fn-1.0)) - (fm * fm);
                bb = sqrt( argument )/aa;
                cc = (2.0 * fn - 1.0)/aa;
                ii = k - n;
                j = k - 2 * n + 1;
                p[k] = (fn + 1.0) * (cc * slat/fn * p[ii] - bb/(fn - 1.0) * p[j]);
                q[k] = cc * (slat * q[ii] - clat/fn * p[ii]) - bb * q[j];
            }
        }
        switch(gh)
        {
            case 3:  aa = rr * gha[l];
                break;
            case 4:  aa = rr * ghb[l];
                break;
            default: printf("\nError in subroutine shval3");
                break;
        }
        if (m == 0)
        {
            switch(gh)
            {
                case 3:  x = x + aa * q[k];
                    z = z - aa * p[k];
                    break;
                case 4:  xtemp = xtemp + aa * q[k];
                    ztemp = ztemp - aa * p[k];
                    break;
                default: printf("\nError in subroutine shval3");
                    break;
            }
            l = l + 1;
        }
        else
        {
            switch(gh)
            {
                case 3:  bb = rr * gha[l+1];
                    cc = aa * cl[m] + bb * sl[m];
                    x = x + cc * q[k];
                    z = z - cc * p[k];
                    if (clat > 0)
                    {
                        y = y + (aa * sl[m] - bb * cl[m]) *
                        fm * p[k]/((fn + 1.0) * clat);
                    }
                    else
                    {
                        y = y + (aa * sl[m] - bb * cl[m]) * q[k] * slat;
                    }
                    l = l + 2;
                    break;
                case 4:  bb = rr * ghb[l+1];
                    cc = aa * cl[m] + bb * sl[m];
                    xtemp = xtemp + cc * q[k];
                    ztemp = ztemp - cc * p[k];
                    if (clat > 0)
                    {
                        ytemp = ytemp + (aa * sl[m] - bb * cl[m]) *
                        fm * p[k]/((fn + 1.0) * clat);
                    }
                    else
                    {
                        ytemp = ytemp + (aa * sl[m] - bb * cl[m]) *
                        q[k] * slat;
                    }
                    l = l + 2;
                    break;
                default: printf("\nError in subroutine shval3");
                    break;
            }
        }
        m = m + 1;
    }

    if (iext != 0)
    {
        aa = ext2 * cl[1] + ext3 * sl[1];
        switch(gh)
        {
            case 3:   x = x - ext1 * clat + aa * slat;
                y = y + ext2 * sl[1] - ext3 * cl[1];
                z = z + ext1 * slat + aa * clat;
                break;
            case 4:   xtemp = xtemp - ext1 * clat + aa * slat;
                ytemp = ytemp + ext2 * sl[1] - ext3 * cl[1];
                ztemp = ztemp + ext1 * slat + aa * clat;
                break;
            default:  printf("\nError in subroutine shval3");
                break;
        }
    }
    switch(gh)
    {
        case 3:   aa = x;
            x = x * cd + z * sd;
            z = z * cd - aa * sd;
            break;
        case 4:   aa = xtemp;
            xtemp = xtemp * cd + ztemp * sd;
            ztemp = ztemp * cd - aa * sd;
            break;
        default:  printf("\nError in subroutine shval3");
            break;
    }
    return(ios);
}


/****************************************************************************/
/*                                                                          */
/*                           Subroutine dihf                                */
/*                                                                          */
/****************************************************************************/
/*                                                                          */
/*     Computes the geomagnetic d, i, h, and f from x, y, and z.            */
/*                                                                          */
/*     Input:                                                               */
/*           x  - northward component                                       */
/*           y  - eastward component                                        */
/*           z  - vertically-downward component                             */
/*                                                                          */
/*     Output:                                                              */
/*           d  - declination                                               */
/*           i  - inclination                                               */
/*           h  - horizontal intensity                                      */
/*           f  - total intensity                                           */
/*                                                                          */
/*     FORTRAN                                                              */
/*           A. Zunde                                                       */
/*           USGS, MS 964, box 25046 Federal Center, Denver, CO.  80225     */
/*                                                                          */
/*     C                                                                    */
/*           C. H. Shaffer                                                  */
/*           Lockheed Missiles and Space Company, Sunnyvale CA              */
/*           August 22, 1988                                                */
/*                                                                          */
/****************************************************************************/

int dihf (gh)
int gh;
{
    int ios;
    int j;
    double sn;
    double h2;
    double hpx;
    double argument, argument2;

    ios = gh;
    sn = 0.0001;

    switch(gh)
    {
        case 3:   for (j = 1; j <= 1; ++j)
        {
            h2 = x*x + y*y;
            argument = h2;
            h = sqrt(argument);       /* calculate horizontal intensity */
            argument = h2 + z*z;
            f = sqrt(argument);      /* calculate total intensity */
            if (f < sn)
            {
                d = NaN;        /* If d and i cannot be determined, */
                i = NaN;        /*       set equal to NaN         */
            }
            else
            {
                argument = z;
                argument2 = h;
                i = atan2(argument,argument2);
                if (h < sn)
                {
                    d = NaN;
                }
                else
                {
                    hpx = h + x;
                    if (hpx < sn)
                    {
                        d = PI;
                    }
                    else
                    {
                        argument = y;
                        argument2 = hpx;
                        d = 2.0 * atan2(argument,argument2);
                    }
                }
            }
        }
            break;
        case 4:   for (j = 1; j <= 1; ++j)
        {
            h2 = xtemp*xtemp + ytemp*ytemp;
            argument = h2;
            htemp = sqrt(argument);
            argument = h2 + ztemp*ztemp;
            ftemp = sqrt(argument);
            if (ftemp < sn)
            {
                dtemp = NaN;    /* If d and i cannot be determined, */
                itemp = NaN;    /*       set equal to 999.0         */
            }
            else
            {
                argument = ztemp;
                argument2 = htemp;
                itemp = atan2(argument,argument2);
                if (htemp < sn)
                {
                    dtemp = NaN;
                }
                else
                {
                    hpx = htemp + xtemp;
                    if (hpx < sn)
                    {
                        dtemp = PI;
                    }
                    else
                    {
                        argument = ytemp;
                        argument2 = hpx;
                        dtemp = 2.0 * atan2(argument,argument2);
                    }
                }
            }
        }
            break;
        default:  printf("\nError in subroutine dihf");
            break;
    }

    return(ios);
}

