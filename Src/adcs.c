/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/cFiles/file.c to edit this template
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "help_adcs.h"

#define PI 3.14159265358979323846
#define asind(x) (asin( x ) / PI * 180)
#define sind(x) (sin( x ) / PI * 180)
#define atan2d(x,y) (atan2(x,y) / PI * 180)

void decimal_to_binary(int n, char *res) {
    int c, d, t;
    char *p;

    t = 0;
    p = (char*) malloc(32 + 1);

    for (c = 8; c > 0; c--) {
        //  dividing n with 2^c
        d = n >> c;

        if (d & 1)
            *(p + t) = 1 + '0';
        else
            *(p + t) = 0 + '0';

        t++;
    }
    *(p + t) = '\0';

    res = p;
}



/*******************************************
 *           calcuar el guany              *
 * ----------------------------------------*
 *     Retorna el valor del guany          *
 ******************************************/
double gainConstant(void) {

    double w = 0;
    double T = 5551;
    double m = 0.250;
    double a = 0.05;
    double alfa = 0.645772;
    double k = 0;
    double inercia [3][3] = {
        {(m * a * a) / 6, 0, 0},
        {0, (m * a * a) / 6, 0},
        {0, 0, (m * a * a) / 6}};
    w = (2 * PI) / T;
    k = 2 * w * (1 + sin(alfa)) * inercia[0][0];

    return k; //El guany sempre es 0, ns perque és una funcio si sempre és el mateix

}

//void detumble(I2C_HandleTypeDef *hi2c1) {

void detumble() {

    double k[3] = {25000, 25000, 5000};
    double mag_field[3];
    double mag_field_before[3] = {0, 0, 0};
    double w_threshold = 0.017;
    double dT = 1;
    int i, j;
    double velW[3] = {1, 1, 1};//Per poder simular
    double mom_b_dot[3];
    double torque[3] = {0, 0, 0};
    

    i = 0;
    while ((fabs(velW[0]) > w_threshold || fabs(velW[1]) > w_threshold || fabs(velW[2]) > w_threshold)) {
        //actualitzar els valors -> llegir data
        
        //mag_field = read_mag_field();
        //velW = read_vel_W();

        for (j = 0; j < 3; j++) {
            //bdot
            mom_b_dot[j] = -k[j] * (mag_field[j] - mag_field_before[j]) / dT;
            //actualitzar camp magnetic anterior per poder fer la derivada en el següent punt
            mag_field_before[j] = mag_field[j];

        }
        //calcular torque
        cross(mom_b_dot, mag_field, torque);

        i++;

    }

}

void detumbling_sim(int N_col) {


    double k[3] = {25000, 25000, 5000};
    double mag_field[3];
    double mag_field_before[3] = {0, 0, 0};

    double velW[3] = {1, 1, 1};//Per poder simular
    double w_threshold = 0.017;
    double dT = 1;
    //    double intens[3];
    //    int N[3] = {168, 152, 152};
    //    double S[3] = {0.00018, 0.00022, 0.00022};
    int i, j;

    double mom_b_dot[3];
    double torque[3] = {0, 0, 0};
    char pos[3] = {'x', 'y', 'z'};

    double **B;
    double **W;
    
    B=get_data_file("det_mag_field.txt", N_col);
    W=get_data_file("det_vel_ang.txt", N_col);
    


    i = 0;
    while ((fabs(velW[0]) > w_threshold || fabs(velW[1]) > w_threshold || fabs(velW[2]) > w_threshold) && i < N_col) {
        //actualitzar els valors -> llegir data
        for (j = 0; j < 3; j++) {
            mag_field[j] = B[i][j];
            velW[j] = W[i][j];
        }

        for (j = 0; j < 3; j++) {
            //bdot
            mom_b_dot[j] = -k[j] * (mag_field[j] - mag_field_before[j]) / dT;
            //actualitzar camp magnetic anterior per poder fer la derivada en el següent punt
            mag_field_before[j] = mag_field[j];

        }
        //calcular torque
        cross(mom_b_dot, mag_field, torque);
        

        printf("\nvalor torque en la iteració %d\n", i);
        for (j = 0; j < 3; j++) {
            printf("Element %c : %e\n", pos[j], torque[j]);
        }

        i++;

    }
    int __attribute__((unused)) i_final = i;
    printf("\n\nHa acabat en la iteració %d", i);

}









