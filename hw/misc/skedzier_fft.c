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
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define TYPE_SWIS_DEVICE "swis_fft"
typedef struct SwisFFTState SwisFFTState;
DECLARE_INSTANCE_CHECKER(SwisFFTState, SWIS_FFT,
                         TYPE_SWIS_DEVICE)


#define FFT_REGS_NUM 6
#define DMA_SIZE 4096
struct SwisFFTState {
    SysBusDevice pdev;

    MemoryRegion iomem;
    uint32_t regs[FFT_REGS_NUM];
    qemu_irq irq;

    struct dma_state {
        dma_addr_t src;
        dma_addr_t dst;
        dma_addr_t cnt;
        dma_addr_t cmd;
    } dma;
    char dma_buf[DMA_SIZE];
    uint64_t dma_mask;
};

#define TYPE_SWIS_FFT "swis_fft"

static void swis_fft_on_reset(void *opaque);
static void swis_fft_reset(void* opaque);

static void swis_fft_init (Object *obj)
{
    SysBusDevice * sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    SwisFFTState *s = SWIS_FFT(dev);
    /* TODO: RST# value should be 0. */
    memory_region_init_io(&s->iomem,OBJECT(s),NULL,s,
                          "sysbus-skedzier_fft-iomem", 0x100); //@@sizeof(s->regs.u32));
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd,&s->irq);
    qemu_register_reset (swis_fft_on_reset, s);
    swis_fft_reset(s);
}

static void swis_fft_reset(void* opaque)
{

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

static void swis_fft_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    //SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    //k->init = sysbus_wztim1_init;
    //k->exit = sysbus_wztim1_uninit;
    dc->desc = "SYSBUS demo TIMER";
    dc->reset = sysbus_swis_fft_reset;
    // dc->vmsd = &vmstate_wztim1;
}

static const TypeInfo sysbus_swis_fft_info = {
    .name          = TYPE_SWIS_FFT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SwisFFTState),
    .instance_init = swis_fft_init,
    .class_init    = swis_fft_class_init,
};

static void sysbus_swis_fft_register_types(void)
{
    type_register_static(&sysbus_swis_fft_info);
}

type_init(sysbus_swis_fft_register_types)
