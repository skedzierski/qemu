#ifndef SK_FFT
#define SK_FFT

typedef enum {
    ID,
    CTRL,
    LEN,
    DMA_IN,
    DMA_OUT,
    STATUS
} SwisFFTRegs;

#define FFT_CTRL_INVERSE_MASK 0x2
#define FFT_CTRL_TRIGGER_MASK 0x1


#endif