/*
 * Click nbfs://nbhost/SystemFileSystem/Templates/Licenses/license-default.txt to change this license
 * Click nbfs://nbhost/SystemFileSystem/Templates/cFiles/file.h to edit this template
 */

/* 
 * File:   ifrg13syn.h
 * Author: Albert Fàbregas
 *
 * Created on 13 de diciembre de 2022, 11:35
 */

#ifndef IGRF13SYN_H
#define IGRF13SYN_H

#ifdef __cplusplus
extern "C" {
#endif



void pri();

void get_gh(double *gh);

void igrf13(double fyears, double alt, double nlat, double elong, double *bb);


#ifdef __cplusplus
}
#endif

#endif /* IGRF13SYN_H */

