#include <stdint.h>

// ─── Base Addresses ───────────────────────────────────────────
#define RCC_BASE     0x40021000
#define FLASH_BASE   0x40022000
#define GPIOA_BASE   0x40010800
#define GPIOC_BASE   0x40011000
#define AFIO_BASE    0x40010000
#define EXTI_BASE    0x40010400
#define USART2_BASE  0x40004400

// ─── RCC Registers ────────────────────────────────────────────
#define RCC_CR       (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR     (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_APB2ENR  (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1ENR  (*(volatile uint32_t *)(RCC_BASE + 0x1C))

// ─── Flash Register ───────────────────────────────────────────
#define FLASH_ACR    (*(volatile uint32_t *)(FLASH_BASE + 0x00))

// ─── GPIOA Registers ──────────────────────────────────────────
#define GPIOA_CRL    (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_BSRR   (*(volatile uint32_t *)(GPIOA_BASE + 0x10))

// ─── GPIOC Registers ──────────────────────────────────────────
#define GPIOC_CRH    (*(volatile uint32_t *)(GPIOC_BASE + 0x04))
#define GPIOC_ODR    (*(volatile uint32_t *)(GPIOC_BASE + 0x0C))
#define GPIOC_IDR    (*(volatile uint32_t *)(GPIOC_BASE + 0x08))

// ─── AFIO Registers ───────────────────────────────────────────
#define AFIO_EXTICR4 (*(volatile uint32_t *)(AFIO_BASE + 0x14))

// ─── EXTI Registers ───────────────────────────────────────────
#define EXTI_IMR     (*(volatile uint32_t *)(EXTI_BASE + 0x00))
#define EXTI_FTSR    (*(volatile uint32_t *)(EXTI_BASE + 0x0C))
#define EXTI_PR      (*(volatile uint32_t *)(EXTI_BASE + 0x14))

// ─── SysTick Registers ────────────────────────────────────────
#define SYST_CSR     (*(volatile uint32_t *)0xE000E010)
#define SYST_RVR     (*(volatile uint32_t *)0xE000E014)
#define SYST_CVR     (*(volatile uint32_t *)0xE000E018)

// ─── NVIC ─────────────────────────────────────────────────────
#define NVIC_ISER1   (*(volatile uint32_t *)0xE000E104)

// ─── USART2 Registers ─────────────────────────────────────────
#define USART2_SR    (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_DR    (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_BRR   (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_CR1   (*(volatile uint32_t *)(USART2_BASE + 0x0C))
#define USART2_CR2   (*(volatile uint32_t *)(USART2_BASE + 0x10))

// ─── Pin Definitions ──────────────────────────────────────────
#define LED_PIN      5
#define BTN_PIN      13

// ─── State Machine ────────────────────────────────────────────
typedef enum {
    STATE_LED_OFF,
    STATE_LED_ON,
    STATE_LED_BLINK
} State;

// ─── Global Variables ─────────────────────────────────────────
volatile uint32_t tick_count = 0;
volatile uint8_t  btn_event  = 0;
volatile uint8_t  rec_data   = 0;

// ─── SystemInit ───────────────────────────────────────────────
void SystemInit(void) {
}

// ─── SysTick Handler ──────────────────────────────────────────
void SysTick_Handler(void) {
    tick_count++;
}

// ─── Button Interrupt ─────────────────────────────────────────
void EXTI15_10_IRQHandler(void) {
    if(EXTI_PR & (1 << BTN_PIN)) {
        btn_event = 1;
        EXTI_PR   = (1 << BTN_PIN);
    }
}

// ─── USART2 Interrupt ─────────────────────────────────────────
void USART2_IRQHandler(void) {
    if(USART2_SR & (1 << 5)) {     // RXNE set
        rec_data = USART2_DR;       // reading DR clears RXNE
    }
}

// ─── get_tick ─────────────────────────────────────────────────
uint32_t get_tick(void) {
    return tick_count;
}

// ─── PLL Init ─────────────────────────────────────────────────
void pll_init(void) {
    RCC_CR      |=  (1 << 16);
    while(!(RCC_CR & (1 << 17)));

    FLASH_ACR   &= ~(0x7);
    FLASH_ACR   |=  (0x2);

    RCC_CFGR    &= ~(0xF << 4);
    RCC_CFGR    |=  (0x4 << 8);
    RCC_CFGR    &= ~(0x7 << 11);

    RCC_CFGR    |=  (1 << 16);
    RCC_CFGR    &= ~(1 << 17);
    RCC_CFGR    &= ~(0xF << 18);
    RCC_CFGR    |=  (0x7 << 18);

    RCC_CR      |=  (1 << 24);
    while(!(RCC_CR & (1 << 25)));

    RCC_CFGR    &= ~(0x3);
    RCC_CFGR    |=  (0x2);
    while((RCC_CFGR & (0x3 << 2)) != (0x2 << 2));
}

// ─── SysTick Init ─────────────────────────────────────────────
void systick_init(void) {
    SYST_RVR = 71999;
    SYST_CVR = 0;
    SYST_CSR = 0x7;
}

// ─── GPIO Init ────────────────────────────────────────────────
void gpio_init(void) {
    RCC_APB2ENR |= (1 << 0);   // AFIOEN
    RCC_APB2ENR |= (1 << 2);   // IOPAEN
    RCC_APB2ENR |= (1 << 4);   // IOPCEN

    // PA5 → output push-pull 10MHz (bits 23:20)
    GPIOA_CRL   &= ~(0xF << 20);
    GPIOA_CRL   |=  (0x1 << 20);

    // PC13 → input pull-up (bits 23:20 of CRH)
    GPIOC_CRH   &= ~(0xF << 20);
    GPIOC_CRH   |=  (0x8 << 20);
    GPIOC_ODR   |=  (1 << BTN_PIN);
}

// ─── EXTI Init ────────────────────────────────────────────────
void exti_init(void) {
    AFIO_EXTICR4 &= ~(0xF << 4);
    AFIO_EXTICR4 |=  (0x2 << 4);

    EXTI_IMR  |= (1 << BTN_PIN);
    EXTI_FTSR |= (1 << BTN_PIN);

    NVIC_ISER1 |= (1 << 8);    // IRQ40 → bit 8 of ISER1
}

// ─── USART2 Init ──────────────────────────────────────────────
void usart2_init(void) {
    RCC_APB1ENR |= (1 << 17);  // USART2EN
    RCC_APB2ENR |= (1 << 2);   // IOPAEN

    // PA2 → AF push-pull 10MHz (TX)
    GPIOA_CRL   &= ~(0xF << 8);
    GPIOA_CRL   |=  (0x9 << 8);

    // PA3 → floating input (RX)
    GPIOA_CRL   &= ~(0xF << 12);
    GPIOA_CRL   |=  (0x4 << 12);

    USART2_CR1  |=  (1 << 13); // UE
    USART2_CR1  &= ~(1 << 12); // M = 8 data bits
    USART2_CR2  &= ~(0x3 << 12); // 1 stop bit
    USART2_BRR   =   0x139;    // 115200 baud
    USART2_CR1  |=  (1 << 3);  // TE
    USART2_CR1  |=  (1 << 2);  // RE
    USART2_CR1  |=  (1 << 5);  // RXNEIE

    NVIC_ISER1  |=  (1 << 6);  // IRQ38 → bit 6 of ISER1
}

// ─── USART2 Transmit ──────────────────────────────────────────
void usart2_transmit(uint8_t data) {
    while(!(USART2_SR & (1 << 7)));  // wait TXE
    USART2_DR = data;
}

// ─── USART2 Send String ───────────────────────────────────────
void usart2_send_string(const char *str) {
    while(*str) {
        usart2_transmit((uint8_t)*str++);
    }
}

// ─── Main ─────────────────────────────────────────────────────
int main(void) {

    pll_init();
    systick_init();
    gpio_init();
    exti_init();
    usart2_init();

    usart2_send_string("System started\r\n");

    State    current_state = STATE_LED_OFF;
    uint32_t blink_timer   = 0;
    uint8_t  blink_phase   = 0;

    while(1) {

        // ── Button event ──────────────────────────────────────
        uint8_t event = 0;
        if(btn_event) {
            uint32_t press_time = get_tick();
            while(!((GPIOC_IDR >> BTN_PIN) & 1)) {
                if(get_tick() - press_time > 20) {
                    event = 1;
                    break;
                }
            }
            btn_event = 0;
        }

        // ── Echo received UART data ───────────────────────────
        if(rec_data != 0 && rec_data != 10) {
            uint8_t local_data = rec_data;  // copy in ONE instruction
            rec_data = 0;                    // clear immediately

            usart2_send_string("Received: ");
            usart2_transmit(local_data);     // safe, interrupt can't touch this
            usart2_send_string("\r\n");
        }

        // ── State Machine ─────────────────────────────────────
        switch(current_state) {

            case STATE_LED_OFF:
                GPIOA_BSRR = (1 << (LED_PIN + 16));
                if(event) {
                    current_state = STATE_LED_ON;
                    usart2_send_string("State: LED ON\r\n");
                }
                break;

            case STATE_LED_ON:
                GPIOA_BSRR = (1 << LED_PIN);
                if(event) {
                    current_state = STATE_LED_BLINK;
                    usart2_send_string("State: LED BLINK\r\n");
                }
                break;

            case STATE_LED_BLINK:
                if((get_tick() - blink_timer) >= 500) {
                    blink_timer = get_tick();
                    if(blink_phase == 0) {
                        GPIOA_BSRR = (1 << LED_PIN);
                        blink_phase = 1;
                    } else {
                        GPIOA_BSRR = (1 << (LED_PIN + 16));
                        blink_phase = 0;
                    }
                }
                if(event) {
                    blink_phase   = 0;
                    current_state = STATE_LED_OFF;
                    usart2_send_string("State: LED OFF\r\n");
                }
                break;
        }
    }
}
