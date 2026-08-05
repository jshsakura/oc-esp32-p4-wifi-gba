/* Generated automatically by tools/generate_symbols.py. Do not edit. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <rg_system.h>
#include "private/elf_symbol.h"

/* libgcc internals: no header declares these, so they are declared here
 * with their real signatures rather than as int. */
extern double __adddf3(double, double);
extern double __subdf3(double, double);
extern double __muldf3(double, double);
extern double __divdf3(double, double);
extern double __floatsidf(int);
extern double __floatunsidf(unsigned);
extern int __fixdfsi(double);
extern unsigned __fixunsdfsi(double);
extern float __truncdfsf2(double);
extern double __extendsfdf2(float);
extern int __ltdf2(double, double);
extern int __gtdf2(double, double);
extern int __eqdf2(double, double);
extern int __nedf2(double, double);
extern int __ledf2(double, double);
extern int __gedf2(double, double);

const struct esp_elfsym gb_host_symbols[] = {
    ESP_ELFSYM_EXPORT(__adddf3),
    ESP_ELFSYM_EXPORT(__divdf3),
    ESP_ELFSYM_EXPORT(__fixdfsi),
    ESP_ELFSYM_EXPORT(__floatsidf),
    ESP_ELFSYM_EXPORT(abort),
    ESP_ELFSYM_EXPORT(calloc),
    ESP_ELFSYM_EXPORT(fclose),
    ESP_ELFSYM_EXPORT(feof),
    ESP_ELFSYM_EXPORT(fopen),
    ESP_ELFSYM_EXPORT(fread),
    ESP_ELFSYM_EXPORT(free),
    ESP_ELFSYM_EXPORT(fseek),
    ESP_ELFSYM_EXPORT(fwrite),
    ESP_ELFSYM_EXPORT(malloc),
    ESP_ELFSYM_EXPORT(memcpy),
    ESP_ELFSYM_EXPORT(memset),
    ESP_ELFSYM_EXPORT(rand),
    ESP_ELFSYM_EXPORT(rg_system_log),
    ESP_ELFSYM_END
};
