#include <stdio.h>
#include <stdlib.h>
#include<math.h>

//#include "adcs.c"
#include "adcs.h"
#include "satutl.h"
#include "help_adcs.h"

void cross(double* A, double* B, double* res) {

    res[0] = (A[1]) * (B[2])- (A[2]) * (B[1]);
    res[1] = (A[2]) * (B[0])- (A[0]) * (B[2]);
    res[2] = (A[0]) * (B[1])- (A[1]) * (B[0]);

}

/*******************************************
 *    calcuar la norm de un vector "A"     *
 * ----------------------------------------*
 *     Retorna el valor de la norma        *
 ******************************************/
double norm(double A[3]) {

    double vect_norm = 0;
    vect_norm = sqrt((A[0] * A[0] + A[1] * A[1] + A[2] * A[2]));
    return vect_norm;

}


double dot(double *A, double *B){
    int i;
    double res=0;
    for ( i = 0; i < 3; i++)
    {
        res+= A[i]*B[i];
    }
    
    return res;

}

/*double **matrix_sum(double matrix1[][3], double matrix2[][3]) {
    int i, j;
    double **matrix3;
    matrix3 = malloc(sizeof (double*) * 3);

    for (i = 0; i < 3; i++) {
        matrix3[i] = malloc(sizeof (double*) * 3);
    }

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            matrix3[i][j] = matrix1[i][j] + matrix2[i][j];
        }
    }
    return matrix3;
}
*/
double **get_data_file(char const *fitx, int N_col) {
    FILE *archivo;

    //int N_col = 507;
    double B[N_col][3];
    double **matrix3;
    int i, j;

    matrix3 = malloc(sizeof (double*) * 3 * N_col);

    for (i = 0; i < N_col; i++) {
        matrix3[i] = malloc(sizeof (double*) * 3);
    }


    archivo = fopen(fitx, "r");

    if (archivo == NULL) {
        exit(1);
    } else {
        //printf("El contenido del archivo de prueba es \n");
        i = 0;
        j = 0;
        while (feof(archivo) == 0) {
            //fscanf(archivo, "%lf", &B[i][j]); /****COMMENTED BECAUSE IT WAS NOT RECOGNISED****/
            //matrix3[i][j]=B[i][j];

            i++;
            if (i == N_col) {
                i = 0;
                j++;
            }

        }

    }
    fclose(archivo);
    //
    for (i = 0; i < N_col; i++) {
        //printf("En la iteració %d\n", i);
        for (j = 0; j < 3; j++) {
            //printf("%e ", B[i][j]);
            matrix3[i][j] = B[i][j];
        }
        //printf("\n");
    }

    //matrix3[5][1]=0.3;
    return matrix3;
}


//Obtain the quaterion to pas from vector_1 to vector_2 
void U2Q(double* vector_1, double* vector_2, double* quat) {

    int i;
    int vector_minusOne[3] = {0, 0, 1};

    //double quat[4];
    double crossproduct[3];
    double crossproduct_minusOne[3];
    double result = 0.0;
    double result2 = 0.0;
    /*
    for (int i = 0; i < 3; i++) {
        result += vector_1[i] * vector_2[i];
    }*/

    result=dot(vector_1, vector_2);
    //printf("\n%f\n\n", result);

    if (result == -1) {//modificar-ho tal i com ho fa el matlab
        double s2 = sqrt(2);
        /*
        crossproduct_minusOne[0] = (vector_minusOne[1] * vector_2[2]) - (vector_minusOne[2] * vector_2[1]);
        crossproduct_minusOne[1] = (vector_minusOne[2] * vector_2[0]) - (vector_minusOne[0] * vector_2[2]);
        crossproduct_minusOne[2] = (vector_minusOne[0] * vector_2[1]) - (vector_minusOne[1] * vector_2[0]);
        
        
        */

        cross(&vector_minusOne, vector_2, &crossproduct_minusOne);
        for (i = 0; i < 3; i++) {
            if (crossproduct_minusOne[i] < 0) {
                crossproduct_minusOne[i] = crossproduct_minusOne[i]*(-1);
            }
            printf("\n%f\n", crossproduct_minusOne[i]);
        }

        quat[0] = 0;
        quat[1] = crossproduct_minusOne[0] / s2;
        quat[2] = crossproduct_minusOne[1] / s2;
        quat[3] = crossproduct_minusOne[2] / s2;
        
    } else {
        /*
        crossproduct[0] = (vector_1[1] * vector_2[2]) - (vector_1[2] * vector_2[1]);
        crossproduct[1] = (vector_1[2] * vector_2[0]) - (vector_1[0] * vector_2[2]);
        crossproduct[2] = (vector_1[0] * vector_2[1]) - (vector_1[1] * vector_2[0]);
        */
        cross(vector_1, vector_2, &crossproduct);
        double s = sqrt(2 * (1 + result));

        quat[0] = 0.5 * s;
        quat[1] = crossproduct[0] / s;
        quat[2] = crossproduct[1] / s;
        quat[3] = crossproduct[2] / s;
    }



    /*
    printf("Resulting Quaternion:\n");
    for(i=0;i<4;i++){
        printf("%f",quat[i]);
        printf("\n");
    }*/

}

void mat_2_quat(double a[3][3], double q[4]) {
   /* quat[0] = sqrt((1 + rot_matrix[0][0] + rot_matrix[1][1] + rot_matrix[2][2])) / 2;
    quat[1] = (rot_matrix[2][1] - rot_matrix[1][2]) / (4 * quat[0]);
    quat[2] = (rot_matrix[0][2] - rot_matrix[2][0]) / (4 * quat[0]);
    quat[3] = (rot_matrix[1][0] - rot_matrix[0][1]) / (4 * quat[0]);*/

    double trace = a[0][0] + a[1][1] + a[2][2]; // I removed + 1.0f; see discussion with Ethan
    if( trace > 0 ) {// I changed M_EPSILON to 0
        double s = 0.5f / sqrtf(trace+ 1.0f);
        q[0] = 0.25f / s;
        q[1] = ( a[2][1] - a[1][2] ) * s;
        q[2] = ( a[0][2] - a[2][0] ) * s;
        q[3] = ( a[1][0] - a[0][1] ) * s;
    } else {
    if ( a[0][0] > a[1][1] && a[0][0] > a[2][2] ) {
        double s = 2.0f * sqrtf( 1.0f + a[0][0] - a[1][1] - a[2][2]);
        q[0] = (a[2][1] - a[1][2] ) / s;
        q[1] = 0.25f * s;
        q[2] = (a[0][1] + a[1][0] ) / s;
        q[3] = (a[0][2] + a[2][0] ) / s;
    } else if (a[1][1] > a[2][2]) {
        double s = 2.0f * sqrtf( 1.0f + a[1][1] - a[0][0] - a[2][2]);
        q[0] = (a[0][2] - a[2][0] ) / s;
        q[1] = (a[0][1] + a[1][0] ) / s;
        q[2] = 0.25f * s;
        q[3] = (a[1][2] + a[2][1] ) / s;
    } else {
        double s = 2.0f * sqrtf( 1.0f + a[2][2] - a[0][0] - a[1][1] );
        q[0] = (a[1][0] - a[0][1] ) / s;
        q[1] = (a[0][2] + a[2][0] ) / s;
        q[2] = (a[1][2] + a[2][1] ) / s;
        q[3] = 0.25f * s;
        }
    }
}

//transpose of a Quaternion, pas A->B to B-> A 
void QPose(double *q){
    double aux;

    int i;
    for ( i = 0; i < 3; i++)
    {
        aux=q[i+1];
        q[i+1]=-aux;
    }
}

//Multiply two quaternions.
//Q2 transforms from A to B and Q1 transforms from B to C
//so Q3 transforms from A to C.
void QMult(double *qB, double *qA, double *qResult){
    qResult[0]=qA[0]*qB[0] - qA[1]*qB[1] - qA[2]*qB[2] - qA[3]*qB[3];
    qResult[1]=qA[1]*qB[0] + qA[0]*qB[1] - qA[3]*qB[2] + qA[2]*qB[3];
    qResult[2]=qA[2]*qB[0] + qA[3]*qB[1] + qA[0]*qB[2] - qA[1]*qB[3];
    qResult[3]=qA[3]*qB[0] - qA[2]*qB[1] + qA[1]*qB[2] + qA[0]*qB[3];
}



//Convert a quaternion to an angle and a unit vector
//q és el queaternion com a input. angle i unit_vector són els outputs
void Q2AU(double *q, double *angle, double *unit_vector){
    double norm;

    norm=sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    //printf("%f\n\n", norm);

    angle[0]=2*atan2(-norm, q[0]);
    if(angle[0]< -PI){
        angle[0]=angle[0] + 2 * PI;
    }else if( angle[0] > PI){
        angle[0]=angle[0]- 2 * PI;
    }
    
    if(norm<epss){
        unit_vector[0]=1;
        unit_vector[1]=0;
        unit_vector[2]=0;
    }
    else{
        unit_vector[0]=q[1]/norm;
        unit_vector[1]=q[2]/norm;
        unit_vector[2]=q[3]/norm;
    }
}


//Convert an angle and a unit vector to a quaternion 
//inputs: angle and u_vec. Outputs: q
void AU2Q(double *angle, double *u_vec, double *q){
    double c=cos(*angle/2);
    double s=sin(*angle/2);

    q[0]=c;
    q[1]=-s*u_vec[0];
    q[2]=-s*u_vec[1];
    q[3]=-s*u_vec[2];

}

//inputs: accel, accel_sat, d
//outputs: a, b, l, accel
void Windup(double *accel, double *accel_sat, p_struct *d, double *a, double *b, double *l){
    if(*accel > *accel_sat){
       *(a +0 +2*0) = d->a[0][0] - d->l[0] * d->c[0];//pos [0][0]
       *(a +1 +2*0) = d->a[0][1] - d->l[0] * d->c[1];//pos [0][1]
       *(a +0 +2*1) = d->a[1][0] - d->l[1] * d->c[0];//pos [1][0]
       *(a +1 +2*1) = d->a[1][1] - d->l[1] * d->c[1];//pos [1][1]

        b[0]=d->b[0] - d->l[0] * d->d;
        b[1]=d->b[1] - d->l[1] * d->d; 

        l[0]=d->l[0];
        l[1]=d->l[1];

    }else{
        *(a +0 +2*0) = d->a[0][0];//pos [0][0]
        *(a +1 +2*0) = d->a[0][1];//pos [0][1]
        *(a +0 +2*1) = d->a[1][0];//pos [1][0]
        *(a +1 +2*1) = d->a[1][1];//pos [1][1]

        b[0]=d->b[0];
        b[1]=d->b[1]; 

        l[0]=d->l[0];
        l[1]=d->l[1];
    }

    if(*accel > *accel_sat){
        *accel=*accel_sat;
    }else if(*accel < - *accel_sat){
        *accel= - *accel_sat;
    }
    
}

void PID3Axis(double *q_eci_Body, p_struct *d, double *torque){

   int i;
   double q_target[4];
   double delta_q[4];
   U2Q(&d->eci_vecor, &d->body_vector,&q_target);
    
    if(d->q_target_last[0]==0 && d->q_target_last[1]==0 && d->q_target_last[2]==0 && d->q_target_last[3]==0 ){
        for(i=0; i<4;i++){
            d->q_target_last[i]=q_eci_Body[i];
        }
        //per anar be no haruia d'entrar en aquest if 
        printf("proba_m");
    }

    printf("q_target_val:\n");
    for(i=0;i<4;i++){
        printf("%f   ", q_target[i]);
    }
    printf("\n\n");


    //Trobar delta_Q
    QPose(&d->q_target_last);
    QMult(&d->q_target_last, &q_target, &delta_q);
    QPose(&d->q_target_last);

    printf("delta_q val:\n");
    for(i=0;i<4;i++){
        printf("%f   ", delta_q[i]);
    }
    printf("\n\n");

    double angle;
    double u_vec[3];

    Q2AU(&delta_q, &angle, &u_vec);
    
    if(abs(angle) > d->max_angle){
        printf("\tdins if\n");
        double aux;
        if(angle<0){
            aux=-d->max_angle;
            AU2Q(&aux, &u_vec, &delta_q);
        }else{
            aux=d->max_angle;
            AU2Q(&aux, &u_vec, &delta_q);
        }
        QMult(&d->q_target_last, &delta_q, &q_target);

        printf("delta_q val dins if:\n");
        for(i=0;i<4;i++){
            printf("%f   ", delta_q[i]);
        }
        printf("\n\n");

    }

    printf("q_target val despres del if:\n");
    for(i=0;i<4;i++){
        printf("%f   ", q_target[i]);
    }
    printf("\n\n");
    //s'ha de fer el un if(unwrap(anlge)) que no se que polles es
    //!!!!!!!!!!
    //!!!!!!!!!!
    //linia 166-173 MATLAB PID3Axis.m
    printf("angle val:\n%f\nu_vec val:\n", angle);
    for(i=0;i<3;i++){
        printf("%f   ",u_vec[i]);
    }
    printf("\n\n");

    //d.q_target_last=q_target
    for ( i = 0; i < 4; i++)
    {
        d->q_target_last[i]=q_target[i];
    }

    //trobar el q_target_body
    double q_target_body[4];
    QPose(q_eci_Body);
    QMult(q_eci_Body, &q_target, &q_target_body);
    QPose(q_eci_Body);
    QPose(&q_target_body);
    if(q_target_body[0]<0){
        for ( i = 0; i < 4; i++)
        {
            q_target_body[i]=-q_target_body[i];
        }
    }

    
    printf("q_target_body val:\n");
    for(i=0;i<4;i++){
        printf("%f   ", q_target_body[i]);
    }
    printf("\n\n");
    
    
    double angle_error[3];
    for ( i = 0; i < 3; i++)
    {
        angle_error[i]=-2 * q_target_body[i+1];
    }

    double accel[3];
    double a[2][2];
    double b[2];
    double l[2];
    
    //eix x (pos 0 vectors)
    accel[0]=d->c[0] * d->x_roll[0]  +  d->c[1] * d->x_roll[1]  +  d->d * angle_error[0];
    Windup(&accel[0],&d->accel_sat[0], &d, &a, &b, &l);
    //actualitzar el x_roll
    d->x_roll[0]=a[0][0]*d->x_roll[0] + a[0][1]*d->x_roll[1] + b[0]*angle_error[0] + l[0]*accel[0];
    d->x_roll[1]=a[1][0]*d->x_roll[0] + a[1][1]*d->x_roll[1] + b[1]*angle_error[0] + l[1]*accel[0];

    //eix y (pos 1 dels vectors)
    accel[1]=d->c[0] * d->x_pitch[0]  +  d->c[1] * d->x_pitch[1]  +  d->d * angle_error[1];
    Windup(&accel[1],&d->accel_sat[1], &d, &a, &b, &l);
    //actualitzar el x_pitch
    d->x_pitch[0]=a[0][0]*d->x_pitch[0] + a[0][1]*d->x_pitch[1] + b[0]*angle_error[0] + l[0]*accel[0];
    d->x_pitch[1]=a[1][0]*d->x_pitch[0] + a[1][1]*d->x_pitch[1] + b[1]*angle_error[0] + l[1]*accel[0];

    //eix z (pos 0 dels vectors)
    accel[2]=d->c[0] * d->x_yaw[0]  +  d->c[1] * d->x_yaw[1]  +  d->d * angle_error[2];
    Windup(&accel[2],&d->accel_sat[2], &d, &a, &b, &l);
    //actualitzar el x_yaw
    d->x_yaw[0]=a[0][0]*d->x_yaw[0] + a[0][1]*d->x_yaw[1] + b[0]*angle_error[0] + l[0]*accel[0];
    d->x_yaw[0]=a[0][0]*d->x_yaw[0] + a[0][1]*d->x_yaw[1] + b[0]*angle_error[0] + l[0]*accel[0];
    
    for ( i = 0; i < 3; i++)
    {
        torque[i]=-d->inertia[i] * accel[i];
    }

   
}

double * normalize(double arr1[4]){
    static double quat[4];
    quat[0] = (arr1[0])/sqrt(arr1[0]+arr1[1]+arr1[2]+arr1[3]);
    quat[1] = (arr1[1])/sqrt(arr1[0]+arr1[1]+arr1[2]+arr1[3]);
    quat[2] = (arr1[2])/sqrt(arr1[0]+arr1[1]+arr1[2]+arr1[3]);
    quat[3] = (arr1[3])/sqrt(arr1[0]+arr1[1]+arr1[2]+arr1[3]);
    return quat;
}


void quat_2_mat(double *rot, double arr1[]){
    //definim quaternio com: qw + i qx + j qy + k qz
    *(rot + 0*3 + 0)=1 - 2*(arr1[2]*arr1[2] + arr1[3]*arr1[3]); //1 - 2*qy2 - 2*qz2
    *(rot + 0*3 + 1)=2*(arr1[1]*arr1[2] - arr1[0]*arr1[3]); //2*qx*qy - 2*qz*qw
    *(rot + 0*3 + 2)=2*(arr1[1]*arr1[3] + arr1[2]*arr1[0]); //2*qx*qz + 2*qy*qw
    *(rot + 1*3 + 0)=2*(arr1[1]*arr1[2] + arr1[3]*arr1[0]); //2*qx*qy + 2*qz*qw
    *(rot + 1*3 + 1)=1 - 2*(arr1[1]*arr1[1] + arr1[3]*arr1[3]); //1 - 2*qx2 - 2*qz2
    *(rot + 1*3 + 2)=2*(arr1[2]*arr1[3] - arr1[1]*arr1[0]); //2*qy*qz - 2*qx*qw
    *(rot + 2*3 + 0)=2*(arr1[1]*arr1[3] - arr1[2]*arr1[0]); //2*qx*qz - 2*qy*qw
    *(rot + 2*3 + 1)=2*(arr1[2]*arr1[3] + arr1[1]*arr1[0]); //2*qy*qz + 2*qx*qw
    *(rot + 2*3 + 2)=1 - 2*(arr1[1]*arr1[1] + arr1[2]*arr1[2]); //1 - 2*qx2 - 2*qy2
}

void rot_2_quat(double rot_matrix[3][3], double *q){
    //definim quaternio com: qw + i qx + j qy + k qz
    q[0] = sqrt((1 + rot_matrix[0][0] + rot_matrix[1][1] + rot_matrix[2][2])) / 2;
    q[1] = (rot_matrix[2][1] - rot_matrix[1][2]) / (4*(q[0]));
    q[2] = (rot_matrix[0][2] - rot_matrix[2][0]) / (4*(q[0]));
    q[3] = (rot_matrix[1][0] - rot_matrix[0][1]) / (4*(q[0]));
}






