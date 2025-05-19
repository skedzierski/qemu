/*
 * QEMU FFT device
 *
 * Copyright (c) 2025 Szymon Kedzierski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/misc/skedzier_fft.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#include <kiss_fft.h>

#define FFT_DEBUG
#ifdef FFT_DEBUG
  #define PRINT_DEBUG(fmt, ...) \
          fprintf(stderr, "swis_fft: %s:%d:%s(): " fmt "\n", \
                  __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
  #define PRINT_DEBUG(fmt, ...) // no-op
#endif

#define TYPE_SWIS_DEVICE "swis_fft"
typedef struct SwisFFTState SwisFFTState;
DECLARE_INSTANCE_CHECKER(SwisFFTState, SWIS_FFT,
                         TYPE_SWIS_DEVICE)


#define FFT_REGS_NUM 6
struct SwisFFTState {
    SysBusDevice pdev;

    MemoryRegion iomem;
    uint32_t regs[FFT_REGS_NUM];
    qemu_irq irq;
    bool inverse_fft;
    kiss_fft_cfg fft_cfg;
    bool fft_configured;
    kiss_fft_cpx* fout;
    kiss_fft_cpx* fin;
};

#define TYPE_SWIS_FFT "swis_fft"

static void swis_fft_on_reset(void *opaque);
static void swis_fft_reset(void* opaque);
static uint64_t swis_fft_read(void *opaque, hwaddr addr, unsigned size);
static void swis_fft_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps swis_fft_iomem_ops = {
    .read = swis_fft_read,
    .write = swis_fft_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4, //Always 32-bit access!!
        .max_access_size = 4,
    },
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4
    }
};

static void swis_fft_init (Object *obj)
{
    PRINT_DEBUG("fft initialized");
    SysBusDevice * sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    SwisFFTState *s = SWIS_FFT(dev);
    /* TODO: RST# value should be 0. */
    memory_region_init_io(&s->iomem,OBJECT(s),&swis_fft_iomem_ops,s,
                          "sysbus-skedzier_fft-iomem", 0x100); //@@sizeof(s->regs.u32));
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd,&s->irq);
    qemu_register_reset (swis_fft_on_reset, s);
    swis_fft_reset(s);
}

static void swis_fft_finalize(Object *obj)
{
    PRINT_DEBUG("fft finalized!");
    SysBusDevice * sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    SwisFFTState *s = SWIS_FFT(dev);
    free(s->fout);
    free(s->fin);
    free(s->fft_cfg);
}

static void swis_fft_reset(void* opaque)
{
    PRINT_DEBUG("reset!");
    SwisFFTState* s = opaque;
    memset(s, 0, sizeof(SwisFFTState));
}

static void swis_fft_on_reset(void *opaque)
{
    SwisFFTState *s = opaque;
    swis_fft_reset(s);
}

static void sysbus_swis_fft_reset(DeviceState *dev)
{
    SwisFFTState *d = SWIS_FFT(dev);
    swis_fft_reset(d);
}

static uint64_t swis_fft_read(void *opaque, hwaddr addr, unsigned size)
{
    SwisFFTState* s = opaque;
    if(s->regs[(addr/4) & 0xff] == STATUS)
    {
        qemu_irq_lower(s->irq);
        s->regs[STATUS] = 0x0;  
    }
    return s->regs[(addr/4) & 0x0ff];
}

static void swis_fft_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    addr = (addr/4) & 0x0ff;
    SwisFFTState* s = opaque;
    if(addr > ID && addr < STATUS)
        s->regs[addr] = val;
    
    if(addr == LEN)
    {
        s->regs[addr] = val;
        s->fft_configured = false;
        if(s->fin == NULL)
            s->fin = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*s->regs[LEN]);
        else
            s->fin = realloc(s->fin, sizeof(kiss_fft_cpx)*s->regs[LEN]);
        
        if(s->fout == NULL)
            s->fout = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*s->regs[LEN]);
        else
            s->fout = realloc(s->fout, sizeof(kiss_fft_cpx)*s->regs[LEN]);
    }
    
    if(addr == CTRL)
    {
        s->fft_configured = s->fft_configured && (s->inverse_fft == (val & FFT_CTRL_INVERSE_MASK));
        
        s->inverse_fft = val & FFT_CTRL_INVERSE_MASK;
        
        if(val & FFT_CTRL_TRIGGER_MASK)
        {

            if(s->regs[LEN] <= 0)
                return;
            
            if(s->fft_cfg != NULL && !s->fft_configured)
            {
                free(s->fft_cfg);
                s->fft_cfg = NULL;
            }

            if(!s->fft_configured)
            {
                PRINT_DEBUG("configured!");
                s->fft_cfg = kiss_fft_alloc(s->regs[LEN], s->inverse_fft, NULL, NULL);
                s->fft_configured = true;
            }
            kiss_fft_scalar* buf = malloc(2*sizeof(kiss_fft_scalar)*s->regs[LEN]);
            cpu_physical_memory_read(s->regs[DMA_IN], buf, 2*sizeof(kiss_fft_scalar)*s->regs[LEN]);
            PRINT_DEBUG("read %ld bytes with DMA from address 0x%x", 2*sizeof(kiss_fft_scalar)*s->regs[LEN], s->regs[DMA_IN]);
            
            
            for(int i = 0; i < 2*s->regs[LEN]; i += 2)
            {
                s->fin[i/2].r = buf[i];
                s->fin[i/2].i = buf[i+1]; 
            }

            kiss_fft(s->fft_cfg, s->fin, s->fout);
            
            for(int i = 0; i < 2*s->regs[LEN]; i += 2)
            {
                buf[i] = s->fout[i/2].r;
                buf[i+1] = s->fout[i/2].i;
            }
            
            cpu_physical_memory_write(s->regs[DMA_OUT], buf, 2*sizeof(kiss_fft_scalar)*s->regs[LEN]);
            PRINT_DEBUG("read %ld bytes with DMA from address 0x%x", 2*sizeof(kiss_fft_scalar)*s->regs[LEN], s->regs[DMA_OUT]);
            qemu_irq_raise(s->irq);
            s->regs[STATUS] = 0x1;
            free(buf);

        }
    }
}

static void swis_fft_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    //SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    //k->init = sysbus_wztim1_init;
    //k->exit = sysbus_wztim1_uninit;
    dc->desc = "SYSBUS FFT acc";
    dc->reset = sysbus_swis_fft_reset;
    // dc->vmsd = &vmstate_wztim1;
}

static const TypeInfo sysbus_swis_fft_info = {
    .name          = TYPE_SWIS_FFT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SwisFFTState),
    .instance_init = swis_fft_init,
    .instance_finalize = swis_fft_finalize,
    .class_init    = swis_fft_class_init,
};

static void sysbus_swis_fft_register_types(void)
{
    type_register_static(&sysbus_swis_fft_info);
}

type_init(sysbus_swis_fft_register_types)
