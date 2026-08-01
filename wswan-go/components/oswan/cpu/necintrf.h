#ifndef NECINTRF_H_
#define NECINTRF_H_

#include <stdint.h>

enum {
    NEC_IP=1, NEC_AW, NEC_CW, NEC_DW, NEC_BW, NEC_SP, NEC_BP, NEC_IX, NEC_IY,
    NEC_FLAGS, NEC_ES, NEC_CS, NEC_SS, NEC_DS,
    NEC_VECTOR, NEC_PENDING, NEC_NMI_STATE, NEC_IRQ_STATE };

/* Public variables */
extern int32_t nec_ICount;

void nec_set_reg(int32_t, uint32_t);
int32_t nec_execute(int32_t cycles);
uint32_t nec_get_reg(uint32_t regnum);
void nec_reset(void *param);
void nec_int(uint32_t wektor);
void nec_interrupt(uint32_t int_num);
void nec_set_irq_line(int32_t irqline, int32_t state);

#endif
