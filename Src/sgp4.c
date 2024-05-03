#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sgp4.h"

double tumin, mu, radiusearthkm, xke, j2, j3, j4, j3oj2;

void prinn(){
    printf("hello worldddd");
}


//it is used in readtwoline!!
/*
%    Inputs:
%     satn        - satellite number
%     bstar       - sgp4 type drag coefficient              kg/m2er
%     ecco        - eccentricity
%     epoch       - epoch time in days from jan 0, 1950. 0 hr
%     argpo       - argument of perigee (output if ds)
%     inclo       - inclination
%     mo          - mean anomaly (output if ds)
%     no          - mean motion
%     nodeo      - right ascension of ascending node

%   outputs       :
%     satrec      - common values for subsequent calls
*/
void sgp4init(int whichconst, satrec *s, double xbstar, double xecco, double epoch,
            double xargpo, double xinclo, double xmo, double xno, double xnodeo){

    s->method='n';
    //this may not be necessary
    s->bstar=xbstar;
    s->ecco=xecco;
    s->argpo=xargpo;
    s->inclo=xinclo;
    s->mo=xmo;
    s->no=xno;
    s->nodeo=xnodeo;
    getgravc(whichconst);

    double ss=78.0/radiusearthkm +1.0;
    double qzms2t=pow( (120.0-78.0)/radiusearthkm, 4 );
    double x2o3 =2.0/3.0;

    double temp4=1.5e-12;
    s->init='y';
    s->t=0.0;

    double ainv, ao, con41, con42, cosio, cosio2, einv, eccsq, omeosq, posq, rp, rteosq, sinio;

    init(s->ecco, epoch, s->inclo, s->no, &ainv, &ao, &con41, &con42, &cosio,&cosio2,
            &einv, &eccsq, &s->method, &omeosq, &posq, &rp, &rteosq, &sinio, s);

    s->error=0;
    if(rp<1.0){
        s->error=5.0;
    }

    double sfour, qzms24, perige, pinvsq;


    if( (omeosq>=0.0) || (s->no>=0.0)) {
        s->isimp=0;
        if(rp<(220.0/radiusearthkm)+1.0){
            s->isimp=1;
        }
        sfour=ss;
        qzms24=qzms2t;
        perige=(rp-1.0)*radiusearthkm;

        if(perige<156.0){
            sfour=perige-78.0;
            if(perige<98.0){
                sfour=20.0;
            }
            qzms24=pow((120.0-sfour)/radiusearthkm, 4);
            sfour=sfour/radiusearthkm +1.0;
        }
        pinvsq=1.0/posq;

        double tsi, etasq, eeta, psisq, coef, coef1, cc2, cc3, cosio4;
        double __attribute__((unused)) temp1, temp2, temp3, xhdot1, xpidot;

        tsi=1.0/(ao-sfour);
        s->eta=ao*s->ecco*tsi;
        etasq=s->eta*s->eta;
        eeta=s->ecco*s->eta;
        psisq=(1.0-etasq);
        if(psisq<0.0){//abs(psisq)
            psisq=-psisq;
        }
        coef=qzms24*pow(tsi, 4);
        coef1=coef/pow(psisq, 3.5);
        cc2=coef1*s->no *(ao * (1.0 + 1.5 * etasq + eeta *
           (4.0 + etasq)) + 0.375 * j2 * tsi / psisq * s->con41 *
           (8.0 + 3.0 * etasq * (8.0 + etasq)));
        s->cc1=s->bstar*cc2;
        cc3=0.0;
        if(s->ecco>1.0e-4){
            cc3 = -2.0 * coef * tsi * j3oj2 * s->no * sinio / s->ecco;
        }

        s->x1mth2=1.0 - cosio2;
        s->cc4=2.0* s->no * coef1 * ao * omeosq *
           (s->eta * (2.0 + 0.5 * etasq) + s->ecco *
           (0.5 + 2.0 * etasq) - j2 * tsi / (ao * psisq) *
           (-3.0 * s->con41 * (1.0 - 2.0 * eeta + etasq *
           (1.5 - 0.5 * eeta)) + 0.75 * s->x1mth2 *
           (2.0 * etasq - eeta * (1.0 + etasq)) * cos(2.0 * s->argpo)));
        s->cc5=2.0 * coef1 * ao * omeosq * (1.0 + 2.75 *(etasq + eeta) + eeta * etasq);
        cosio4=cosio2 * cosio2;
        temp1  = 1.5 * j2 * pinvsq * s->no;
        temp2  = 0.5 * temp1 * j2 * pinvsq;
        temp3  = -0.46875 * j4 * pinvsq * pinvsq * s->no;
        s->mdot=s->no + 0.5 * temp1 * rteosq * s->con41 +
           0.0625 * temp2 * rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
        s->argpdot  = -0.5 * temp1 * con42 + 0.0625 * temp2 *
           (7.0 - 114.0 * cosio2 + 395.0 * cosio4) +
           temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
        xhdot1= -temp1 * cosio;
        s->nodedot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) +
           2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;
        xpidot =  s->argpdot+ s->nodedot;
        s->omgcof   = s->bstar * cc3 * cos(s->argpo);
        s->xmcof    = 0.0;
        if(s->ecco>1.0e-4){
            s->xmcof = -x2o3 * coef * s->bstar / eeta;
        }
        s->nodecf = 3.5 * omeosq * xhdot1 * s->cc1;
        s->t2cof   = 1.5 * s->cc1;

        // sgp4fix for divide by zero with xinco = 180 deg

        if(abs(cosio+1.0)>1.5e-12){
            s->xlcof   = -0.25 * j3oj2 * sinio *
              (3.0 + 5.0 * cosio) / (1.0 + cosio);
        }else{
            s->xlcof   = -0.25 * j3oj2 * sinio *
              (3.0 + 5.0 * cosio) / temp4;
        }
        s->aycof =-0.5 * j3oj2 * sinio;
        s->delmo = pow((1.0 + s->eta * cos(s->mo)), 3);
        s->sinmao = sin(s->mo);
        s->x7thm1 = 7.0 * cosio2 - 1.0;

        /* --------------- deep space initialization ------------- */
        //line 242 matlab
        if((2*M_PI/s->no)>=225.0){
            s->method='d';
            s->isimp=1;
            double __attribute__((unused)) tc=0.0;
            double __attribute__((unused)) inclm=s->inclo;

            //dscom
            //dpper

            double __attribute__((unused)) argpm=0.0;
            double __attribute__((unused)) nodem=0.0;
            double __attribute__((unused)) mm=0.0;

            //dsinit
        }

        if(s->isimp!=1){
            double cc1sq          = s->cc1 * s->cc1;
            s->d2    = 4.0 * ao * tsi * cc1sq;
            double temp = s->d2 * tsi * s->cc1 / 3.0;
            s->d3    = (17.0 * ao + sfour) * temp;
            s->d4    = 0.5 * temp * ao * tsi *
               (221.0 * ao + 31.0 * sfour) * s->cc1;
            s->t3cof = s->d2 + 2.0 * cc1sq;
            s->t4cof = 0.25 * (3.0 * s->d3 + s->cc1 *
               (12.0 * s->d2 + 10.0 * cc1sq));
            s->t5cof = 0.2 * (3.0 * s->d4 +
               12.0 * s->cc1 * s->d3 +
               6.0 * s->d2 * s->d2 +
               15.0 * cc1sq * (2.0 * s->d2 + cc1sq));
        }

    }//ifomeosq=0...

    /* finally propogate to zero epoch to initialise all others. */
    if(s->error==0){
        //sgp4(s,0.0);
    }
    s->init='n';


}

/*MAlab function: twoline2rvMOD*/
/*
%   inputs        :
%   longstr1      - TLE character string
%   longstr2      - TLE character string

%   outputs       :
%     satrec      - structure containing all the sgp4 satellite information

Example:
satrec prova;
    char ho[70]="1 32765U 08017A   09209.18550013  .00001988  00000-0  53832-4 0  3469";
    char nn[70]="2 32765  13.0144 166.4925 0314115 276.2764  80.1808 14.81449833 69409";
    readTwoLine(&ho, &nn, &prova);
*/
void readTwoLine(char const *longstr1, char const *longstr2, satrec *s){
    int whichconst=84;
    char __attribute__((unused)) typerun ='m';
    char __attribute__((unused)) typeinput='m';
    double deg2rad=M_PI/180.0; //deg/rad
    double xpdotp=1440.0/(2*M_PI);//[rev/day]/[rad/min]

    double __attribute__((unused)) revnum=0;
    double __attribute__((unused)) elnum=0;
    int year=0;
    s->error=0;
    
    char str1[70], str2[70];
    int i;
    for(i=0;i<68; i++){
        str1[i]=longstr1[i];
        str2[i]=longstr2[i];
    }

    for(i=10;i<16; i++){
        if(str1[i]==' '){
            str1[i]='_';
            //printf("\ndins el if\n");
        }
    }

    if(str1[44]!=' '){
        str1[43]=str1[44];
        //printf("\ndins el if\n");
    }
    str1[44]='.';

    if(str1[7]==' '){
        str1[7]='U';
        //printf("\ndins el if\n");
    }

    if(str1[9]==' '){
        str1[9]='.';
        //printf("\ndins el if\n");
    }

    for(i=45;i<50; i++){
        if(str1[i]==' '){
            str1[i]='0';
            //printf("\ndins el if\n");
        }
    }

    if(str1[51]==' '){
        str1[51]='0';
        //printf("\ndins el if\n");
    }
    if(str1[53]!=' '){
        str1[52]=str1[53];
        //printf("\ndins el if\n");
    }
    str1[53]='.';
    if(str1[62]==' '){
        str1[62]='0';
        //printf("\ndins el if\n");
    }
    
    /*MATLAB:   Vigilar length!!!!!!
    if ((length(longstr1) < 68) || (longstr1(68) == ' '))
        longstr1(68) = '0';
    end
    */

    //second line:
    str2[25]='.';

    for(i=26;i<33;i++){
        if(str2[i]==' '){
            str2[i]='0';
        }
    }



    //parse first line:
    char aux[14];

    int __attribute__((unused)) carnumb=(int) str1[0] -48;
    //str1(2-6) satnum;
    char __attribute__((unused)) clasification=str1[7];
    
    //intldesg
    for(i=9;i<17;i++){
        aux[i-9]=str1[i];
    }
    int __attribute__((unused)) intldesg=strtod(aux, NULL);
    for(i=9;i<17;i++){
        aux[i-9]=' ';
    }
    
    //epochyr
    for(i=18;i<20;i++){
        aux[i-18]=str1[i];
    }
    s->epochyr=strtod(aux, NULL);
    for(i=18;i<20;i++){
        aux[i-18]=' ';
    }
    //epochdays
    for(i=20;i<32;i++){
        aux[i-20]=str1[i];
    }
    s->epochdays=strtod(aux, NULL);
    for(i=20;i<32;i++){
        aux[i-20]=' ';
    }

    //ndot
    for(i=33;i<43;i++){
        aux[i-33]=str1[i];
    }
    s->ndot=strtod(aux, NULL);
    for(i=33;i<43;i++){
        aux[i-33]=' ';
    }

    //nddot
    for(i=43;i<50;i++){
        aux[i-43]=str1[i];
    }
    s->nddot=strtod(aux, NULL);
    for(i=43;i<50;i++){
        aux[i-43]=' ';
    }

    //nexp
    for(i=50;i<52;i++){
        aux[i-50]=str1[i];
    }
    double nexp=strtod(aux, NULL);
    for(i=50;i<52;i++){
        aux[i-50]=' ';
    }

    //bstar
    for(i=52;i<59;i++){
        aux[i-52]=str1[i];
    }
    s->bstar=strtod(aux, NULL);
    for(i=52;i<59;i++){
        aux[i-52]=' ';
    }

    //ibexp
    for(i=59;i<61;i++){
        aux[i-59]=str1[i];
    }
    double ibexp=strtod(aux, NULL);
    for(i=59;i<61;i++){
        aux[i-59]=' ';
    }

    //numb
    double __attribute__((unused)) numb = str1[62]-48;

    //elnum
    for(i=64;i<68;i++){
        aux[i-64]=str1[i];
    }
    elnum=strtod(aux, NULL);
    for(i=64;i<68;i++){
        aux[i-64]=' ';
    }

    //parse second line:

    //carnumb
    carnumb=(int) str2[0] -48;

    //inclo
    for(i=7;i<16;i++){
        aux[i-7]=str2[i];
    }
    s->inclo=strtod(aux, NULL);
    for(i=7;i<16;i++){
        aux[i-7]=' ';
    }

    //nodeo
    for(i=16;i<25;i++){
        aux[i-16]=str2[i];
    }
    s->nodeo=strtod(aux, NULL);
    for(i=16;i<25;i++){
        aux[i-16]=' ';
    }

    //ecco
    for(i=25;i<33;i++){
        aux[i-25]=str2[i];
    }
    s->ecco=strtod(aux, NULL);
    for(i=25;i<33;i++){
        aux[i-25]=' ';
    }

    //argpo
    for(i=33;i<42;i++){
        aux[i-33]=str2[i];
    }
    s->argpo=strtod(aux, NULL);
    for(i=33;i<42;i++){
        aux[i-33]=' ';
    }

    //mo
    for(i=42;i<51;i++){
        aux[i-42]=str2[i];
    }
    s->mo=strtod(aux, NULL);
    for(i=42;i<51;i++){
        aux[i-42]=' ';
    }

    //no
    for(i=51;i<63;i++){
        aux[i-51]=str2[i];
    }
    s->no=strtod(aux, NULL);
    for(i=51;i<63;i++){
        aux[i-51]=' ';
    }

    //revenum
    for(i=63;i<68;i++){
        aux[i-63]=str2[i];
    }
    revnum=strtod(aux, NULL);
    for(i=63;i<68;i++){
        aux[i-63]=' ';
    }

    //   find no, ndot, nddot
    s->no=s->no/xpdotp;
    s->nddot=s->nddot*pow(10, nexp);
    s->bstar=s->bstar*pow(10, ibexp);


    //      convert to SGP4 units
    getgravc(whichconst);
    s->a=pow( (s->no * tumin) , -2.0/3.0);      //[we]
    s->ndot=s->ndot/(xpdotp*1440.0);            //[rad/min^2]
    s->nddot=s->nddot/(xpdotp*1440.0*1440.0);   //[rad/min^3]

    //      find standard orbital elements
    s->inclo=s->inclo*deg2rad;
    s->nodeo=s->nodeo*deg2rad;
    s->argpo=s->argpo*deg2rad;
    s->mo=s->mo*deg2rad;

    s->alta=s->a*(1+s->ecco)-1.0;
    s->altp=s->a*(1-s->ecco)-1.0;


    // ------------- temp fix for years from 1957-2056 ----------------
    // ------ correct fix will occur when year is 4-digit in 2le ------
    if(s->epochyr<57){
        year=s->epochyr + 2000;
    }else{
        year=s->epochyr + 1900;
    }

    double mon,day, hr,min, sec;
    days2mdh(year, s->epochdays, &mon, &day, &hr,&min, &sec);

    //jday
    double jjday;//it can't be named as the function :(
    jday(year, mon, day, hr, min, sec, &jjday);
    s->jdsatepoch=jjday;

    //----initializi the orbit at sgp4epocho---------
    double sgp4epcoh= jjday-2433281.5;//days since 0 Jan 1950
    sgp4init(whichconst, s, s->bstar, s->ecco, sgp4epcoh, s->argpo, s->inclo, s->mo, s->no, s->nodeo);

}


//input:    year   1900 ...2100
//          days   0.0 ...366.0

//outputs:  mon     1 ...12
//          day     1... 28,29,30,31
//          hr      0 ....23
//          minute  0...59
//          sec     0...59
void days2mdh(int year, double days, double *mon, double *day, double *hr, double *minute, double *sec){
    int months[12];
    int i;
    for(i=1;i<13;i++){
        months[i-1]=31;
        if(i==2){
            months[i-1]=28;
        }
        if(i==4 || i==6 || i==9 || i==11){
            months[i-1]=30;
        }
    }

    int dayoryr=floor(days);
    if((year-1900)%4==0){
        months[1]=29;
    }

    i=0;
    int inttemp=0;
    while((dayoryr>(inttemp + months[i])) && (i<11)){
        inttemp+=months[i];
        i++;
    }

    *mon=i+1;
    *day=dayoryr-inttemp;

    //      find hours minutes and seconds
    double temp=(days-dayoryr)*24;
    if(temp>=temp){
        *hr=floor(temp);
    }else{
        *hr=-floor(-temp);
    }
    temp=(temp-*hr)*60.0;
    if(temp>=temp){
        *minute=floor(temp);
    }else{
        *minute=-floor(-temp);
    }
    *sec=(temp-*minute)*60.0;
}

/*
update the global values
input: whichconst
*/
void getgravc(int whichconst){
    switch(whichconst){
        case 721:
            mu     = 398600.79964;       // in km3 / s2
           radiusearthkm = 6378.135;     // km
           xke    = 0.0743669161;
           tumin  = 1.0 / xke;
           j2     =   0.001082616;
           j3     =  -0.00000253881;
           j4     =  -0.00000165597;
           j3oj2  =  j3 / j2;
           break;
        case 72:
           mu     = 398600.8;            // in km3 / s2
           radiusearthkm = 6378.135;     // km
           xke    = 60.0 / sqrt(radiusearthkm*radiusearthkm*radiusearthkm/mu);
           tumin  = 1.0 / xke;
           j2     =   0.001082616;
           j3     =  -0.00000253881;
           j4     =  -0.00000165597;
           j3oj2  =  j3 / j2; 
           break;
        case 84:
           mu     = 398600.5;            // in km3 / s2
           radiusearthkm = 6378.137;     // km
           xke    = 60.0 / sqrt(radiusearthkm*radiusearthkm*radiusearthkm/mu);
           tumin  = 1.0 / xke;
           j2     =   0.00108262998905;
           j3     =  -0.00000253215306;
           j4     =  -0.00000161098761;
           j3oj2  =  j3 / j2;
           break; 
    }



}


//input:    year   1900 ...2100
//          mon     1 ...12
//          day     1... 28,29,30,31
//          hr      0 ....23
//          minute  0...59
//          sec     0...59

//output:   jday
void jday(int yr, double mon, double day, double hr, double min, double sec, double *jday){
    *jday=367.0*yr - floor( ( 7*(yr+floor( (mon+9)/12.0) ) ) *0.25) 
            + floor(275*mon/9.0) +day+1721013.5 + ( (sec/60.0 + min)/60.0 +hr )/24.0;
}



/*
%   inputs        :
%     ecco        - eccentricity                           0.0 - 1.0
%     epoch       - epoch time in days from jan 0, 1950. 0 hr
%     inclo       - inclination of satellite
%     no          - mean motion of satellite

%   outputs       :
%     ainv        - 1.0 / a
%     ao          - semi major axis
%     con41       -
%     con42       - 1.0 - 5.0 cos(i)
%     cosio       - cosine of inclination
%     cosio2      - cosio squared
%     einv        - 1.0 / e
%     eccsq       - eccentricity squared
%     method      - flag for deep space                    'd', 'n'
%     omeosq      - 1.0 - ecco * ecco
%     posq        - semi-parameter squared
%     rp          - radius of perigee
%     rteosq      - square root of (1.0 - ecco*ecco)
%     sinio       - sine of inclination
%     gsto        - gst at time of observation               rad
%     no          - mean motion of satellite
*/
void init(double ecco, double epoch, double inclo,double no, 
            double *ainv,double *ao,double *con41,double *con42,double *cosio,double *cosio2,
            double *einv,double *eccsq,char *method,double *omeosq, double *posq,double *rp,
            double *rteosq, double *sinio, satrec *s){
    
    double x2o3   = 2.0 / 3.0;

    /* ------------- calculate auxillary epoch quantities ---------- */
    *eccsq  = ecco * ecco;
    *omeosq = 1.0 - *eccsq;
    *rteosq = sqrt(*omeosq);
    *cosio  = cos(inclo);
    *cosio2 = *cosio * *cosio;

    /* ------------------ un-kozai the mean motion ----------------- */    
    double ak    = pow((xke / no),x2o3);
    double d1    = 0.75 * j2 * (3.0 * (*cosio2) - 1.0) / (*rteosq * *omeosq);
    double del   = d1 / (ak * ak);
    double adel  = ak * (1.0 - del * del - del *
       (1.0 / 3.0 + 134.0 * del * del / 81.0));
    del   = d1/(adel * adel);
    s->no    = no / (1.0 + del);

    *ao    = pow((xke / no),x2o3);
    *sinio = sin(inclo);
    double po    = (*ao) * (*omeosq);
    *con42 = 1.0 - 5.0 * *cosio2;
    *con41 = -*con42-*cosio2-*cosio2;
    *ainv  = 1.0 / *ao;
    *einv  = 1.0 / ecco;
    *posq  = po * po;
    *rp    = *ao * (1.0 - ecco);
    *method = 'n';

    //sgp4fix modern approach to finding sidereal time
    char opsmode='a';
    if(opsmode != 'a'){
        s->gsto=gstime(epoch + 2433281.5);
    }else{
        double ts70  = epoch - 7305.0;
        double ids70 = floor(ts70 + 1.0e-8);
        double tfrac = ts70 - ids70;
        //find greenwich location at epoch
        double c1    = 1.72027916940703639e-2;
        double thgr70= 1.7321343856509374;
        double fk5r  = 5.07551419432269442e-15;
        double twopi = 6.283185307179586;
        double c1p2p = c1 + twopi;
        int div=floor((thgr70 + c1*ids70 + c1p2p*tfrac + ts70*ts70*fk5r)/twopi);
        s->gsto=thgr70 + c1*ids70 + c1p2p*tfrac + ts70*ts70*fk5r-div*twopi;
        //s->gsto  = rem( thgr70 + c1*ids70 + c1p2p*tfrac + ts70*ts70*fk5r, twopi);

    }
    s->con41=*con41;
}

/*
this function finds the greenwich sidereal time (iau-82).

*/
double gstime(double jdut1){
    double twopi=2*M_PI;
    double deg2rad=M_PI/180.0;

    double tut1= ( jdut1 - 2451545.0 ) / 36525.0;

    double temp = - 6.2e-6 * tut1 * tut1 * tut1 + 0.093104 * tut1 * tut1  
    + (876600.0 * 3600.0 + 8640184.812866) * tut1 + 67310.54841;

    // 360/86400 = 1/240, to deg, to rad
    int div=floor((temp*deg2rad/240.0 )/twopi);
    temp = temp*deg2rad/240.0-div*twopi;
    

    if(temp<0.0){
        temp=temp+twopi;
    }
    return temp;
}

/*
%   inputs        :
%     satrec    - initialised structure from sgp4init() call.
%     tsince    - time eince epoch (minutes)

%   outputs       :
%     r           - position vector                     km
%     v           - velocity                            km/sec
*/
void sgp4(satrec *s, double tsince, double *r, double *v){
    double twopi=M_PI*2.0;
    double x2o3 = 2.0/3.0;

    // sgp4fix divisor for divide by zero check on inclination
    //the old check used 1.0 + cos(pi-1.0e-9), but then compared it to
    // 1.5 e-12, so the threshold was changed to 1.5e-12 for consistancy
    double __attribute__((unused)) temp4    =   1.5e-12;
    double vkmpersec     = radiusearthkm * xke/60.0;

    /* --------------------- clear sgp4 error flag ----------------- */
    s->t     = tsince;
    s->error = 0.0;

    /* ------- update for secular gravity and atmospheric drag ----- */
    double xmdf, argpdf, nodedf, argpm, mm, t2, nodem, tempa, tempe, templ;
    xmdf    = s->mo + s->mdot * s->t;
    argpdf  = s->argpo + s->argpdot * s->t;
    nodedf  = s->nodeo + s->nodedot * s->t;
    argpm   = argpdf;
    mm      = xmdf;
    t2      = s->t * s->t;
    nodem   = nodedf + s->nodecf * t2;
    tempa   = 1.0 - s->cc1 * s->t;
    tempe   = s->bstar * s->cc4 * s->t;
    templ   = s->t2cof * t2;//hi algun error numeric amb el matlab, els que provenen d'abans

    double delomg, delm, temp, t3, t4;
    if(s->isimp!=1){
        delomg = s->omgcof * s->t;
        delm   = s->xmcof *( pow((1.0 + s->eta * cos(xmdf)),3) - s->delmo);
        temp   = delomg + delm;
        mm     = xmdf + temp;
        argpm  = argpdf - temp;
        t3     = t2 * s->t;
        t4     = t3 * s->t;
        tempa  = tempa - s->d2 * t2 - s->d3 * t3 - s->d4 * t4;  
        tempe  = tempe + s->bstar * s->cc5 * (sin(mm) - s->sinmao);
        templ  = templ + s->t3cof * t3 + t4 * (s->t4cof + s->t * s->t5cof);
    }

    double nm, em, inclm;
    nm    = s->no;
    em    = s->ecco;
    inclm = s->inclo;

    if(s->method=='d'){
        double __attribute__((unused)) tc;
        tc=s->t;
        //dspace... line 153 Matlab
    }

    if(nm<=0.0){
        s->error=2;
    }
    double am;
    am = pow((xke / nm),x2o3) * tempa * tempa;
    nm = xke / pow(am,1.5);
    em = em - tempe;

    // fix tolerance for error recognition
    if((em>=1.0) || (em<-0.001) || (am<0.95)){
        s->error=1;
    }

    //sgp4fix change test condition for eccentricity
    if (em < 1.0e-6){
       em  = 1.0e-6;
    }
    double xlm, emsq;
    mm     = mm + s->no * templ;
    xlm    = mm + argpm + nodem;
    emsq   = em * em;
    temp   = 1.0 - emsq;
    //nodem  = rem(nodem, twopi);
    int div;
    div=floor(nodem/twopi);
    nodem=nodem - div*twopi;
    //argpm  = rem(argpm, twopi);
    div=floor(argpm/twopi);
    argpm=argpm - div*twopi;
    //xlm    = rem(xlm, twopi);
    div=floor(xlm/twopi);
    xlm=xlm - div*twopi;
    //mm     = rem(xlm - argpm - nodem, twopi);
    div=floor((xlm - argpm - nodem)/twopi);
    mm=(xlm - argpm - nodem) - div * twopi;//-2pi

    /* ----------------- compute extra mean quantities ------------- */
    double sinim, cosim;
    sinim = sin(inclm);
    cosim = cos(inclm);

    /* -------------------- add lunar-solar periodics -------------- */
    double ep, xincp,argpp, nodep, mp, sinip, cosip;
    ep     = em;
    xincp  = inclm;
    argpp  = argpm;
    nodep  = nodem;
    mp     = mm;
    sinip  = sinim;
    cosip  = cosim;

    if(s->method=='d'){
        //dpper
    }
    /* -------------------- long period periodics ------------------ */
    if(s->method=='d'){
    }
    double axnl, aynl, xl;
    axnl = ep * cos(argpp);
    temp = 1.0 / (am * (1.0 - ep * ep));
    aynl = ep* sin(argpp) + temp * s->aycof;
    xl   = mp + argpp + nodep + temp * s->xlcof * axnl;

    /* --------------------- solve kepler's equation --------------- */
    double u, eo1, tem5, ktr, abstem5;
    //u    = rem(xl - nodep, twopi);
    div=floor((xl - nodep) / twopi);
    u=(xl-nodep) - twopi*div;
    eo1  = u;
    tem5 = 9999.9;
    ktr = 1;
    abstem5=tem5;

    //   sgp4fix for kepler iteration
    //   the following iteration needs better limits on corrections
    double sineo1, coseo1;
    while( (abstem5>=1.0e-12) && (ktr<=10)){
        sineo1 = sin(eo1);
        coseo1 = cos(eo1);
        tem5   = 1.0 - coseo1 * axnl - sineo1 * aynl;
        tem5   = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
        //abs(tem5)
        if(tem5>=0.0){
            abstem5=tem5;
        }else{
            abstem5=-tem5;
        }
        if(abstem5>=0.95){
            if(tem5>0.0){
                tem5=0.95;
            }else{
                tem5=-0.95;
            }
        }
        eo1 = eo1 + tem5;
        ktr = ktr + 1;

    }

    /* ------------- short period preliminary quantities ----------- */
    double ecose, esine, el2, pl;
    ecose = axnl*coseo1 + aynl*sineo1;
    esine = axnl*sineo1 - aynl*coseo1;
    el2   = axnl*axnl + aynl*aynl;
    pl    = am*(1.0-el2);

    
    if(pl<0.0){
        s->error=4;
        r[0]=r[1]=r[2]=0;
        v[0]=v[1]=v[2]=0;
        return;
    }else{
        double rl, rdotl, rvdotl, betal, sinu, cosu, su, sin2u, cos2u, temp1, temp2;
        rl     = am * (1.0 - ecose);
        rdotl  = sqrt(am) * esine/rl;
        rvdotl = sqrt(pl) / rl;
        betal  = sqrt(1.0 - el2);
        temp   = esine / (1.0 + betal);
        sinu   = am / rl * (sineo1 - aynl - axnl * temp);
        cosu   = am / rl * (coseo1 - axnl + aynl * temp);
        su     = atan2(sinu, cosu);
        sin2u  = (cosu + cosu) * sinu;
        cos2u  = 1.0 - 2.0 * sinu * sinu;
        temp   = 1.0 / pl;
        temp1  = 0.5 * j2 * temp;
        temp2  = temp1 * temp; 
    
        if(s->method=='d'){
            double cosisq;
            cosisq = cosip * cosip;
            s->con41  = 3.0*cosisq - 1.0;
            s->x1mth2 = 1.0 - cosisq;
            s->x7thm1 = 7.0*cosisq - 1.0;
        }
        double mrt, xnode,xinc,mvt,rvdot;
        mrt   = rl * (1.0 - 1.5 * temp2 * betal * s->con41) +0.5 * temp1 * s->x1mth2 * cos2u;
        su    = su - 0.25 * temp2 * s->x7thm1 * sin2u;
        xnode = nodep + 1.5 * temp2 * cosip * sin2u;
        xinc  = xincp + 1.5 * temp2 * cosip * sinip * cos2u;
        mvt   = rdotl - nm * temp1 * s->x1mth2 * sin2u / xke;
        rvdot = rvdotl + nm * temp1 * (s->x1mth2 * cos2u + 1.5 * s->con41) / xke;

        /* --------------------- orientation vectors ------------------- */
        double sinsu, cossu,snod, cnod, sini,cosi,xmx, xmy, ux, uy, uz, vx, vy, vz;
        sinsu =  sin(su);
        cossu =  cos(su);
        snod  =  sin(xnode);
        cnod  =  cos(xnode);
        sini  =  sin(xinc);
        cosi  =  cos(xinc);
        xmx   = -snod * cosi;
        xmy   =  cnod * cosi;
        ux    =  xmx * sinsu + cnod * cossu;
        uy    =  xmy * sinsu + snod * cossu;
        uz    =  sini * sinsu;
        vx    =  xmx * cossu - cnod * sinsu;
        vy    =  xmy * cossu - snod * sinsu;
        vz    =  sini * cossu;

        /* --------- position and velocity (in km and km/sec) ---------- */
        r[0] = (mrt * ux)* radiusearthkm;
        r[1] = (mrt * uy)* radiusearthkm;
        r[2] = (mrt * uz)* radiusearthkm;
        v[0] = (mvt * ux + rvdot * vx) * vkmpersec;
        v[1] = (mvt * uy + rvdot * vy) * vkmpersec;
        v[2] = (mvt * uz + rvdot * vz) * vkmpersec;

        if(mrt<1.0){
            s->error=6;
        }
    }
}
