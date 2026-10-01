#include "bsp_dwt.h"
#include <math.h>

static uint32_t cpu_hz;
static uint32_t last_cycle;
static uint64_t total_cycles;

/* A single atomic sample prevents a torn high/low word at CYCCNT rollover.
 * Must be sampled at least once per 2^32 cycles (~59.6 s at 72 MHz).
 * RobotSystemCpp samples every millisecond. Re-init after changing HCLK. */
static uint64_t SampleCycles(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t now = DWT->CYCCNT;
    total_cycles += (uint32_t)(now - last_cycle);
    last_cycle = now;
    uint64_t result = total_cycles;
    __set_PRIMASK(primask);
    return result;
}

void BspDwt_Init(uint32_t cpu_freq_mhz)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    cpu_hz = cpu_freq_mhz ? cpu_freq_mhz * 1000000U : HAL_RCC_GetHCLKFreq();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    last_cycle = 0;
    total_cycles = 0;
    __set_PRIMASK(primask);
}

void BspDwt_CntUpdate(void)
{
    if (cpu_hz)
    {
        (void)SampleCycles();
    }
}

void BspDwt_SysTimeUpdate(void)
{
    BspDwt_CntUpdate();
}

double BspDwt_GetDeltaTime64(uint32_t *cnt_last)
{
    if (!cnt_last || !cpu_hz)
    {
        return 0;
    }
    uint32_t now = DWT->CYCCNT;
    uint32_t elapsed = now - *cnt_last;
    *cnt_last = now;
    BspDwt_CntUpdate();
    return (double)elapsed / cpu_hz;
}

float BspDwt_GetDeltaTime(uint32_t *cnt_last)
{
    return (float)BspDwt_GetDeltaTime64(cnt_last);
}

uint64_t BspDwt_GetTimeline_USec(void)
{
    if (!cpu_hz)
    {
        return 0;
    }
    uint64_t cycles = SampleCycles();
    return (cycles / cpu_hz) * 1000000ULL + ((cycles % cpu_hz) * 1000000ULL) / cpu_hz;
}

float BspDwt_GetTimeline_Sec(void)
{
    return (float)BspDwt_GetTimeline_USec() * 0.000001f;
}

float BspDwt_GetTimeline_MSec(void)
{
    return (float)BspDwt_GetTimeline_USec() * 0.001f;
}

void BspDwt_DelayUs(uint32_t us)
{
    if (!cpu_hz)
    {
        return;
    }
    /* Chunking also avoids overflow when converting microseconds to cycles. */
    while (us)
    {
        uint32_t chunk = us > 1000U ? 1000U : us;
        uint32_t ticks = (uint32_t)(((uint64_t)chunk * cpu_hz) / 1000000U);
        uint32_t start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < ticks)
        {
            __NOP();
        }
        BspDwt_CntUpdate();
        us -= chunk;
    }
}

void BspDwt_Delay(float seconds)
{
    if (!isfinite(seconds) || seconds <= 0 || seconds > 4294.0f)
    {
        return;
    }
    BspDwt_DelayUs((uint32_t)(seconds * 1000000.0f));
}
