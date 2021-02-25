/**
 * $Id$
 *
 * @brief Red Pitaya Spectrum Analyzer FPGA Interface.
 *
 * @Author Jure Menart <juremenart@gmail.com>
 *
 * (c) Red Pitaya  http://www.redpitaya.com
 *
 * This part of code is written in C programming language.
 * Please visit http://en.wikipedia.org/wiki/C_(programming_language)
 * for more details on the language used herein.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "rp_cross.h"
#include "spec_fpga.h"
#include "spec_dsp.h"
#include "calib.h"
#include "common.h"

/* internals */
/* The FPGA register structure */
spectr_fpga_reg_mem_t *g_spectr_fpga_reg_mem = NULL;
uint32_t           *g_spectr_fpga_cha_mem = NULL;
uint32_t           *g_spectr_fpga_chb_mem = NULL;

/* The memory file descriptor used to mmap() the FPGA space */
int             g_spectr_fpga_mem_fd = -1;

unsigned short      g_signal_fgpa_length = ADC_BUFFER_SIZE;

/* constants */
/* ADC format = s.13 */
const int c_spectr_fpga_adc_bits = ADC_BITS;
/* c_osc_fpga_max_v */
float g_spectr_fpga_adc_max_v;
const float c_spectr_fpga_adc_max_v_revC= +1.079;
const float c_spectr_fpga_adc_max_v_revD= +1.027;
/* Sampling frequency = 125Mspmpls (non-decimated) */
float spectr_get_fpga_smpl_freq() { return ADC_SAMPLE_RATE; }
/* Sampling period (non-decimated) - 8 [ns] */
const float c_spectr_fpga_smpl_period = (1. / ADC_SAMPLE_RATE);



double __rp_rand()
{
    return ((double)rand() / (double)RAND_MAX);
}

int __spectr_fpga_cleanup_mem(void)
{
    if(g_spectr_fpga_reg_mem) {
        if(munmap(g_spectr_fpga_reg_mem, SPECTR_FPGA_BASE_SIZE) < 0) {
            fprintf(stderr, "munmap() failed: %s\n", strerror(errno));
            return -1;
        }
        g_spectr_fpga_reg_mem = NULL;
        if(g_spectr_fpga_cha_mem)
            g_spectr_fpga_cha_mem = NULL;
        if(g_spectr_fpga_chb_mem)
            g_spectr_fpga_chb_mem = NULL;
    }
    if(g_spectr_fpga_mem_fd >= 0) {
        close(g_spectr_fpga_mem_fd);
        g_spectr_fpga_mem_fd = -1;
    }

    return 0;
}

int spectr_fpga_init(void)
{
    /* update hw specific parmateres, NOTE, this is now hardcoded */
    g_spectr_fpga_adc_max_v = c_spectr_fpga_adc_max_v_revD;

    /* If maybe needed, cleanup the FD & memory pointer */
    if(__spectr_fpga_cleanup_mem() < 0)
        return -1;

    g_spectr_fpga_mem_fd = open("/dev/uio/api", O_RDWR | O_SYNC);
    if(g_spectr_fpga_mem_fd < 0) {
        fprintf(stderr, "ERROR: failed open of UIO device: %s\n", strerror(errno));
        return -1;
    }

    g_spectr_fpga_reg_mem = mmap(NULL, SPECTR_FPGA_BASE_SIZE, PROT_READ | PROT_WRITE,
                          MAP_SHARED, g_spectr_fpga_mem_fd, (SPECTR_FPGA_BASE_ADDR >> 20) * sysconf(_SC_PAGESIZE));
    if((void *)g_spectr_fpga_reg_mem == MAP_FAILED) {
        fprintf(stderr, "mmap() failed: %s\n", strerror(errno));
        __spectr_fpga_cleanup_mem();
        return -1;
    }
    g_spectr_fpga_cha_mem = (uint32_t *)g_spectr_fpga_reg_mem +
        (SPECTR_FPGA_CHA_OFFSET / sizeof(uint32_t));
    g_spectr_fpga_chb_mem = (uint32_t *)g_spectr_fpga_reg_mem +
        (SPECTR_FPGA_CHB_OFFSET / sizeof(uint32_t));

    return 0;
}

int spectr_fpga_exit(void)
{
    __spectr_fpga_cleanup_mem();

    return 0;
}

int spectr_fpga_update_params(int trig_imm, int trig_source, int trig_edge,
                           float trig_delay, float trig_level, float decimation,
                           int enable_avg_at_dec)
{
    /* TODO: Locking of memory map */
    int fpga_trig_source = spectr_fpga_cnv_trig_source(trig_imm, trig_source,
                                                    trig_edge);
    int fpga_dec_factor = decimation;
    int fpga_delay;
    int fpga_trig_thr = spectr_fpga_cnv_v_to_cnt(trig_level);


    // uint32_t gain_hi_cha_filt_aa = 0;
    // uint32_t gain_hi_cha_filt_bb = 0;
    // uint32_t gain_hi_cha_filt_pp = 0;
    // uint32_t gain_hi_cha_filt_kk = 0xFFFFFF;

    // uint32_t gain_hi_chb_filt_aa = 0;
    // uint32_t gain_hi_chb_filt_bb = 0;
    // uint32_t gain_hi_chb_filt_pp = 0;
    // uint32_t gain_hi_chb_filt_kk = 0xFFFFFF;


    /* Equalization filter coefficients */
    uint32_t gain_hi_cha_filt_aa = 0x7D93;
    uint32_t gain_hi_cha_filt_bb = 0x437C7;
    uint32_t gain_hi_cha_filt_pp = 0x0;
    uint32_t gain_hi_cha_filt_kk = 0xd9999a;

    uint32_t gain_hi_chb_filt_aa = 0x7D93;
    uint32_t gain_hi_chb_filt_bb = 0x437C7;
    uint32_t gain_hi_chb_filt_pp = 0x0;
    uint32_t gain_hi_chb_filt_kk = 0xd9999a;

#if defined Z10 || defined Z20_125
    rp_pinState_t stateCh1 = RP_HIGH;
    rp_pinState_t stateCh2 = RP_HIGH;
    if (!rp_IsApiInit()){ 
        calib_Init();
    }else{
        rp_AcqGetGain(RP_CH_1,&stateCh1);
        rp_AcqGetGain(RP_CH_2,&stateCh2);
    }

    gain_hi_cha_filt_aa = calib_GetFilterCoff(RP_CH_1,stateCh1,AA);
    gain_hi_cha_filt_bb = calib_GetFilterCoff(RP_CH_1,stateCh1,BB);
    gain_hi_cha_filt_pp = calib_GetFilterCoff(RP_CH_1,stateCh1,PP);
    gain_hi_cha_filt_kk = calib_GetFilterCoff(RP_CH_1,stateCh1,KK);

    gain_hi_chb_filt_aa = calib_GetFilterCoff(RP_CH_2,stateCh2,AA);
    gain_hi_chb_filt_bb = calib_GetFilterCoff(RP_CH_2,stateCh2,BB);
    gain_hi_chb_filt_pp = calib_GetFilterCoff(RP_CH_2,stateCh2,PP);
    gain_hi_chb_filt_kk = calib_GetFilterCoff(RP_CH_2,stateCh2,KK);
    if (!rp_IsApiInit()) calib_Release(); 
#endif

    if((fpga_trig_source < 0) || (fpga_dec_factor < 0)) {
        fprintf(stderr, "spectr_fpga_update_params() failed\n");
        return -1;
    }

    fpga_delay = rp_get_fpga_signal_length();

    /* Trig source is written after ARM */
    /*    g_spectr_fpga_reg_mem->trig_source   = fpga_trig_source;*/
    if(trig_source == 0)
        g_spectr_fpga_reg_mem->cha_thr   = fpga_trig_thr;
    else
        g_spectr_fpga_reg_mem->chb_thr   = fpga_trig_thr;

    g_spectr_fpga_reg_mem->data_dec      = fpga_dec_factor;
    g_spectr_fpga_reg_mem->trigger_delay = (uint32_t)fpga_delay;

    g_spectr_fpga_reg_mem->other = enable_avg_at_dec;

    g_spectr_fpga_reg_mem->cha_filt_aa = gain_hi_cha_filt_aa;
    g_spectr_fpga_reg_mem->cha_filt_bb = gain_hi_cha_filt_bb;
    g_spectr_fpga_reg_mem->cha_filt_pp = gain_hi_cha_filt_pp;
    g_spectr_fpga_reg_mem->cha_filt_kk = gain_hi_cha_filt_kk;

    g_spectr_fpga_reg_mem->chb_filt_aa = gain_hi_chb_filt_aa;
    g_spectr_fpga_reg_mem->chb_filt_bb = gain_hi_chb_filt_bb;
    g_spectr_fpga_reg_mem->chb_filt_pp = gain_hi_chb_filt_pp;
    g_spectr_fpga_reg_mem->chb_filt_kk = gain_hi_chb_filt_kk;

    return 0;
}

int spectr_fpga_reset(void)
{
    g_spectr_fpga_reg_mem->conf |= SPECTR_FPGA_CONF_RST_BIT;
    return 0;
}

int spectr_fpga_arm_trigger(void)
{
    g_spectr_fpga_reg_mem->conf |= SPECTR_FPGA_CONF_ARM_BIT;

    return 0;
}

int spectr_fpga_set_trigger(uint32_t trig_source)
{
    g_spectr_fpga_reg_mem->trig_source = trig_source;
    return 0;
}

int spectr_fpga_set_trigger_delay(uint32_t trig_delay)
{
    g_spectr_fpga_reg_mem->trigger_delay = trig_delay;
    return 0;
}

int spectr_fpga_triggered(void)
{
    return ((g_spectr_fpga_reg_mem->trig_source & SPECTR_FPGA_TRIG_SRC_MASK)==0);
}

int spectr_fpga_buffer_fill(void){
    return ((g_spectr_fpga_reg_mem->conf & SPECTR_FPGA_BUFFER_FILL));
}

int spectr_fpga_get_sig_ptr(int **cha_signal, int **chb_signal)
{
    *cha_signal = (int *)g_spectr_fpga_cha_mem;
    *chb_signal = (int *)g_spectr_fpga_chb_mem;
    return 0;
}

int spectr_fpga_get_signal(double **cha_signal, double **chb_signal)
{
    //int cur_ptr_trig;
    int wr_ptr_trig;
    int in_idx, out_idx;
    double *cha_o = *cha_signal;
    double *chb_o = *chb_signal;

    if(!cha_o || !chb_o) {
        fprintf(stderr, "spectr_fpga_get_signal() not initialized\n");
        return -1;
    }

    double offset[2] = {0,0};
    double gain[2] = {1,1};
#if defined Z10 || defined Z20_125
        rp_pinState_t stateCh1 = RP_HIGH;
        rp_pinState_t stateCh2 = RP_HIGH;

        if (!rp_IsApiInit()){ 
            calib_Init();
        }else{
            rp_AcqGetGain(RP_CH_1,&stateCh1);
            rp_AcqGetGain(RP_CH_2,&stateCh2);
        }

        offset[0] = calib_getOffset(RP_CH_1,stateCh1);
        offset[1] = calib_getOffset(RP_CH_2,stateCh2);
        gain[0] = (double)calib_GetFrontEndScale(RP_CH_1,stateCh1) / (double)rp_cmn_CalibFullScaleFromVoltage(stateCh1 == RP_LOW ? 20 : 1) * (stateCh1 == RP_LOW ? 1 : 20);
        gain[1] = (double)calib_GetFrontEndScale(RP_CH_2,stateCh2) / (double)rp_cmn_CalibFullScaleFromVoltage(stateCh2 == RP_LOW ? 20 : 1) * (stateCh2 == RP_LOW ? 1 : 20);
        if (!rp_IsApiInit()) calib_Release();
#endif

#if defined Z20_250_12
        rp_pinState_t stateCh1 = RP_HIGH;
        rp_pinState_t stateCh2 = RP_HIGH;
        rp_acq_ac_dc_mode_t ac_dc_Ch1 = RP_DC;
        rp_acq_ac_dc_mode_t ac_dc_Ch2 = RP_DC;
        float ch1VMax = 1;
        float ch2VMax = 1;

        if (!rp_IsApiInit()){ 
            calib_Init();
        }else{
            rp_AcqGetGain(RP_CH_1,&stateCh1);
            rp_AcqGetGain(RP_CH_2,&stateCh2);
            rp_AcqGetGainV(RP_CH_1,&ch1VMax);
            rp_AcqGetGainV(RP_CH_2,&ch2VMax);
            rp_AcqGetAC_DC(RP_CH_1,&ac_dc_Ch1);
            rp_AcqGetAC_DC(RP_CH_2,&ac_dc_Ch2);
        }

        offset[0] = calib_getOffset(RP_CH_1,stateCh1,ac_dc_Ch1);
        offset[1] = calib_getOffset(RP_CH_2,stateCh2,ac_dc_Ch2);
        gain[0] = (double)calib_GetFrontEndScale(RP_CH_1,stateCh1,ac_dc_Ch1) / (double)rp_cmn_CalibFullScaleFromVoltage(stateCh1 == RP_LOW ? 20 : 1) * (stateCh1 == RP_LOW ? 1 : 20);
        gain[1] = (double)calib_GetFrontEndScale(RP_CH_2,stateCh2,ac_dc_Ch2) / (double)rp_cmn_CalibFullScaleFromVoltage(stateCh2 == RP_LOW ? 20 : 1) * (stateCh2 == RP_LOW ? 1 : 20);
        if (!rp_IsApiInit()) calib_Release(); 
#endif

    spectr_fpga_get_wr_ptr(NULL, &wr_ptr_trig);
    for(in_idx = wr_ptr_trig + 1, out_idx = 0;
        out_idx < rp_get_fpga_signal_length(); in_idx++, out_idx++) {
        if(in_idx >= ADC_BUFFER_SIZE)
            in_idx = in_idx % ADC_BUFFER_SIZE;

        cha_o[out_idx] = g_spectr_fpga_cha_mem[in_idx];
        chb_o[out_idx] = g_spectr_fpga_chb_mem[in_idx];

        // convert to signed
        if(cha_o[out_idx] > (double)(1 << (ADC_BITS-1)))
            cha_o[out_idx] -= (double)(1 << ADC_BITS);
        if(chb_o[out_idx] > (double)(1 << (ADC_BITS-1)))
            chb_o[out_idx] -= (double)(1 << ADC_BITS);

        cha_o[out_idx] = ((cha_o[out_idx] - offset[0]) * gain[0]) / (double)((int)(1<<(c_spectr_fpga_adc_bits-1)));
        chb_o[out_idx] = ((chb_o[out_idx] - offset[1]) * gain[1]) / (double)((int)(1<<(c_spectr_fpga_adc_bits-1)));
    }

    return 0;
}

int spectr_fpga_get_wr_ptr(int *wr_ptr_curr, int *wr_ptr_trig)
{
    if(wr_ptr_curr)
        *wr_ptr_curr = g_spectr_fpga_reg_mem->wr_ptr_cur;
    if(wr_ptr_trig)
        *wr_ptr_trig = g_spectr_fpga_reg_mem->wr_ptr_trigger;

    return 0;
}
int spectr_fpga_cnv_trig_source(int trig_imm, int trig_source, int trig_edge)
{
    int fpga_trig_source = 0;

    /* Trigger immediately */
    if(trig_imm)
        return 1;

    switch(trig_source) {
    case 0: /* ChA*/
        if(trig_edge == 0)
            fpga_trig_source = 2;
        else
            fpga_trig_source = 3;
        break;
    case 1: /* ChB*/
        if(trig_edge == 0)
            fpga_trig_source = 4;
        else
            fpga_trig_source = 5;
        break;
    case 2: /* External */
        if(trig_edge == 0)
            fpga_trig_source = 6;
        else
            fpga_trig_source = 7;

        break;
    default:
        /* Error */
        return -1;
    }

    return fpga_trig_source;
}


int spectr_fpga_cnv_time_to_smpls(float time, int dec_factor)
{
    /* Calculate sampling period (including decimation) */
    float smpl_p = (c_spectr_fpga_smpl_period * dec_factor);
    int fpga_smpls = (int)round(time / smpl_p);

    return fpga_smpls;
}

int spectr_fpga_cnv_v_to_cnt(float voltage)
{
    int adc_cnts = 0;

    if((voltage > g_spectr_fpga_adc_max_v) || (voltage < -g_spectr_fpga_adc_max_v))
        return -1;

    adc_cnts = (int)round(voltage * (float)((int)(1<<c_spectr_fpga_adc_bits)) /
                          (2*g_spectr_fpga_adc_max_v));

    /* Clip highest value (+14 is calculated in int32_t to 0x2000, but we have
     * only 14 bits
     */
    if((voltage > 0) && (adc_cnts & (1<<(c_spectr_fpga_adc_bits-1))))
        adc_cnts = (1<<(c_spectr_fpga_adc_bits-1))-1;
    else
        adc_cnts = adc_cnts & ((1<<(c_spectr_fpga_adc_bits))-1);

    return adc_cnts;
}

float spectr_fpga_cnv_cnt_to_v(int cnts)
{
    int m;

    if(cnts & (1<<(c_spectr_fpga_adc_bits-1))) {
        /* negative number */
        m = -1 *((cnts ^ ((1<<c_spectr_fpga_adc_bits)-1)) + 1);
    } else {
        m = cnts;
        /* positive number */
    }
    return (m * g_spectr_fpga_adc_max_v /
            (float)(1<<(c_spectr_fpga_adc_bits-1)));

}

int rp_set_fpga_signal_length(unsigned short len){
    if (len < 256 || len > ADC_BUFFER_SIZE) return -1;
    
    unsigned char res = 0;
    unsigned short n = len; 
    while (n) {
        res += n&1;
        n >>= 1;
    }
    if (res != 1) return -1;

    g_signal_fgpa_length = len; 
    return 0;
}

unsigned short rp_get_fpga_signal_length(){
    return g_signal_fgpa_length;
}

unsigned short rp_get_fpga_signal_max_length(){
    return ADC_BUFFER_SIZE;
}
