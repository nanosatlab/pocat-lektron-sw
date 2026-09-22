/* 
 * File:   adcs.h
 * Author: Albert Fabregas
 *
 * Created on 22 de diciembre de 2022, 10:00
 */
#ifndef SGP4_H
#define SGP4_H

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct{
        double t,a;
        double error;
        double mo;
        double mdot;
        double argpo, argpdot;
        double nodeo, nodedot;
        double nodecf;
        double cc1, cc4, cc5;
        double bstar;
        double t2cof, t3cof, t4cof, t5cof;
        int isimp;
        double omgcof, xmcof;
        double eta, delmo;
        double d2, d3, d4;
        double sinmao;
        double no, ecco, inclo;
        char method, init;
        double atime, xli, xni;
        double d2201, d2211, d3210, d3222, d4410, d4422, d5220, d5232, d5421;
        double d5433, dedt, del1, del2, del3,didt, dmdt, dnodt, domdt;
        double irez, gsto, xfact, xlamo;
        double e3, ee2, peo, pgho, pho, pinco, plo, se2, se3, sgh2, sgh3, sgh4;
        double sh2, sh3, si2, si3, sl2, sl3, sl4,xgh2, xgh3, xgh4, xh2, xh3;
        double xi2, xi3, xl2, xl3, xl4,zmol, zmos, xinit;
        double aycof, xlcof;
        double con41, x1mth2, x7mth1, x7thm1;

        double epochyr, epochdays, ndot, nddot;
        double alta, altp;

        double jdsatepoch;

    }satrec;

    void prinn();

    void readTwoLine(char const *longstr1, char const *longstr2, satrec *s);

    void days2mdh(int year, double days, double *mon, double *day, double *hr, double *minute, double *sec);

    void getgravc(int whichconst);

    void jday(int yr, double mon, double day, double hr, double min, double sec, double *jday);

    void init(double ecco, double epoch, double inclo,double no, 
            double *ainv,double *ao,double *con41,double *con42,double *cosio,double *cosio2,
            double *einv,double *eccsq,char *method,double *omeosq, double *posq,double *rp,
            double *rteosq, double *sinio, satrec *s);

    double gstime(double jdut1);

    void sgp4(satrec *s, double tsince, double *r, double *v);

#ifdef __cplusplus
}
#endif

#endif /* SGP4_H */