/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/cFiles/file.h to edit this template
 */

/* 
 * File:   help_adcs.h
 * Author: Maria Rosa Ribo
 *
 * Created on 31 de octubre de 2022, 11:35
 */

#ifndef HELP_ADCS_H
#define HELP_ADCS_H

#ifdef __cplusplus
extern "C" {
#endif

#define ROWS 507    /* if you need a constant, #define one (or more) */
#define COLS 3
#define epss (2.22e-16)

    typedef struct{
        double eci_vecor[3];
        double body_vector[3];
        double x_roll[2];
        double x_pitch[2];
        double x_yaw[2];

        double a[2][2];
        double b[2];
        double c[2];//vector fila
        double l[2];//vector columna
        double d;
        double inertia[3];
        double accel_sat[3];
        double max_angle;

        double q_desired_state[4];
        double q_target_last[4];

    }p_struct;

    struct rot_matrix_2{
        double rot_matrix[3][3];
    };

    struct quat{
        double arr1[4];
    };

    
    void cross(double* A, double* B, double* res);

    double norm(double A[3]);

    double dot(double *A, double *B);
    
    //double **matrix_sum(int matrix1[][3], int matrix2[][3]);

    double **get_data_file(char const *fitx, int N_col);

    void U2Q(double vector_1[3], double vector_2[3], double quat[4]);

    //void quat_2_mat(double rot_matrix[3][3], double quat_1[4]);

    void mat_2_quat(double rot_matrix[3][3], double quat[4]);

    void QPose(double *q);

    void QMult(double *qB, double *qA, double *qResult);

    void Q2AU(double *q, double *angle, double *unit_vector);

    void AU2Q(double *angle, double *u_vec, double *q);

    void Windup(double *accel, double *accel_sat, p_struct *d, double *a, double *b, double *l);

    void PID3Axis(double *q_eci_Body, p_struct *d, double *torque);

    double * normalize(double arr1[4]);

    void quat_2_mat(double *rot, double arr1[]);

    void rot_2_quat(double rot_matrix[3][3], double *q);



#ifdef __cplusplus
}
#endif

#endif /* HELP_ADCS_H */

