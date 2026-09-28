/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/cFiles/file.h to edit this template
 */

/* 
 * File:   adcs.h
 * Author: Albert Fabregas
 *
 * Created on 19 de octubre de 2022, 10:00
 */

#ifndef ADCS_H
#define ADCS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PI 3.14159265358979323846
    
void decimal_to_binary(int n, char *res);
    
double gainConstant(void);//és una constant, no depoent de cap valor, es podria definir com a constant
                              //a més, el resultat sempre és (3.77*10^-7)

void detumble();
    
void detumbling_sim(int N_col);
    
   
    
    
    
#ifdef __cplusplus
}
#endif

#endif /* ADCS_H */

