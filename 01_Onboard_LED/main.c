#include <stdint.h>

/* RCC */
#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018UL)

/* GPIOB */
#define GPIOB_CRL   (*(volatile uint32_t *)0x40010C00UL)
#define GPIOB_ODR   (*(volatile uint32_t *)0x40010C0CUL)

/* SysTick */
#define SYST_CSR    (*(volatile uint32_t *)0xE000E010UL)
#define SYST_RVR    (*(volatile uint32_t *)0xE000E014UL)
#define SYST_CVR    (*(volatile uint32_t *)0xE000E018UL)

#define SYSCLK_HZ 8000000UL

volatile uint32_t msTicks = 0;

void SysTick_Handler(void)
{
    msTicks++;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = msTicks;

    while ((msTicks - start) < ms);
}

void GPIO_Init(void)
{
    /* Enable GPIOB clock */
    RCC_APB2ENR |= (1 << 3);

    /* PB2 = output push-pull, 2 MHz */
    GPIOB_CRL &= ~(0xF << 8);
    GPIOB_CRL |=  (0x2 << 8);
}

void SysTick_Init(void)
{
    SYST_RVR = (SYSCLK_HZ / 1000) - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x07;
}

int main(void)
{
    GPIO_Init();
    SysTick_Init();

    while (1)
    {
        GPIOB_ODR ^= (1 << 2);
        delay_ms(500);
    }
}

