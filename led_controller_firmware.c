/* Week 2: UART LED controller
 * Simulates a UART-controlled LED and 1 ms timer tick. Commands: MODE
 * OFF|SOLID|BLINK, PERIOD <100..5000>, STATUS, HELP. Simulator: @WAIT <ms>,
 * @QUIT. Build with GCC: gcc -std=c99 -Wall -Wextra -pedantic
 * led_controller_firmware.c -o led_sim
 * Run: led_sim.exe --self-test | --demo | --interactive
 * Self-test covers commands, blink timing, limits, and UART buffer overflow.
 * PC simulation only; configure timer, UART, and GPIO for a real board.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_SIZE 64u
#define LINE_SIZE 96u
#define MIN_PERIOD 100u
#define MAX_PERIOD 5000u
#define MAX_WAIT 60000u

typedef enum { OFF, SOLID, BLINK } mode_t;
static volatile uint32_t ms, rx_overflow;
static volatile uint8_t rx[RX_SIZE], head, tail;
static mode_t mode = OFF;
static uint32_t period = 1000u, last_toggle;
static uint8_t led;
static char line[LINE_SIZE];
static size_t used;

/* Replace this print with the target board's LED GPIO write. */
static void led_write(uint8_t on)
{
    if (led != on) {
        led = on;
        printf("LED: %s at %lu ms\n", on ? "ON" : "OFF", (unsigned long)ms);
    }
}

/* Simulated 1 ms timer and UART receive interrupts. */
static void timer_isr(void) { ++ms; }
static void uart_rx_isr(uint8_t b)
{
    uint8_t next = (uint8_t)((head + 1u) % RX_SIZE);
    if (next == tail) { ++rx_overflow; return; }
    rx[head] = b;
    head = next;
}
static int uart_get(uint8_t *b)
{
    if (tail == head) return 0;
    *b = rx[tail];
    tail = (uint8_t)((tail + 1u) % RX_SIZE);
    return 1;
}

static const char *mode_text(void)
{
    return mode == OFF ? "OFF" : mode == SOLID ? "SOLID" : "BLINK";
}
static void set_mode(mode_t m)
{
    mode = m;
    last_toggle = ms;
    led_write(m != OFF);
    printf("OK mode=%s\n", mode_text());
}
static void command(char *s)
{
    char *end;
    unsigned long n;
    while (*s == ' ' || *s == '\t') ++s;
    for (char *p=s; *p; ++p) *p = (char)toupper((unsigned char)*p);
    if (!strcmp(s,"HELP")) {
        puts("MODE OFF|SOLID|BLINK, PERIOD <100..5000>, STATUS");
    } else if (!strcmp(s,"STATUS")) {
        printf("STATUS mode=%s period=%lu ms led=%s uptime=%lu ms\n", mode_text(),
               (unsigned long)period, led ? "ON" : "OFF", (unsigned long)ms);
    } else if (!strncmp(s,"MODE ",5)) {
        s += 5;
        if (!strcmp(s,"OFF")) set_mode(OFF);
        else if (!strcmp(s,"SOLID")) set_mode(SOLID);
        else if (!strcmp(s,"BLINK")) set_mode(BLINK);
        else puts("ERR mode must be OFF, SOLID, or BLINK");
    } else if (!strncmp(s,"PERIOD ",7)) {
        n = strtoul(s+7,&end,10);
        while (*end == ' ' || *end == '\t') ++end;
        if (end == s+7 || *end || n < MIN_PERIOD || n > MAX_PERIOD)
            puts("ERR period must be 100 to 5000 ms");
        else { period=(uint32_t)n; last_toggle=ms; printf("OK period=%lu ms\n",n); }
    } else if (*s) puts("ERR unknown command; send HELP");
}
static void poll(void)
{
    uint8_t b;
    while (uart_get(&b)) {
        if (b=='\r' || b=='\n') {
            if (used) { line[used]='\0'; command(line); used=0; }
        } else if (used < LINE_SIZE-1u) line[used++]=(char)b;
        else { used=0; puts("ERR command line too long"); }
    }
    if (mode == OFF) led_write(0);
    else if (mode == SOLID) led_write(1);
    else {
        uint32_t half=period/2u, elapsed=ms-last_toggle;
        if (elapsed >= half) {
            uint32_t steps=elapsed/half;
            last_toggle += steps*half;
            if (steps & 1u) led_write((uint8_t)!led);
        }
    }
}
static void send_uart(const char *s)
{
    while (*s) uart_rx_isr((uint8_t)*s++);
    uart_rx_isr('\r'); uart_rx_isr('\n'); poll();
}
static int wait_ms(uint32_t n)
{
    if (n > MAX_WAIT) { puts("ERR wait limited to 60000 ms"); return 0; }
    while (n--) timer_isr();
    poll();
    return 1;
}
static void reset(void)
{
    ms=rx_overflow=head=tail=0; mode=OFF; period=1000u; last_toggle=0;
    led=0; used=0;
}
static int self_test(void)
{
    int fail=0;
    reset(); send_uart("MODE BLINK");
    if (mode!=BLINK || !led) ++fail;
    wait_ms(500); if (led) ++fail;
    wait_ms(500); if (!led) ++fail;
    send_uart("PERIOD 200"); if (period!=200) ++fail;
    wait_ms(100); if (led) ++fail;
    send_uart("PERIOD 10"); if (period!=200) ++fail;
    send_uart("MODE SOLID"); if (!led || mode!=SOLID) ++fail;
    send_uart("MODE OFF"); if (led || mode!=OFF) ++fail;
    reset();
    for (unsigned i=0;i<RX_SIZE+8u;i++) uart_rx_isr('X');
    if (!rx_overflow) ++fail;
    if (!fail) puts("SELF-TEST: PASS");
    else printf("SELF-TEST: FAIL (%d checks)\n",fail);
    return fail != 0;
}
static void demo(void)
{
    puts("--- LED controller demo ---");
    send_uart("HELP"); send_uart("MODE BLINK"); wait_ms(1200);
    send_uart("PERIOD 200"); wait_ms(450); send_uart("STATUS");
    send_uart("MODE SOLID"); send_uart("MODE OFF"); send_uart("STATUS");
    puts("--- end demo ---");
}
static void interactive(void)
{
    char input[LINE_SIZE];
    puts("Enter UART commands, @WAIT <ms>, or @QUIT.");
    while (fgets(input,sizeof input,stdin)) {
        size_t n=strlen(input);
        while (n && (input[n-1]=='\n' || input[n-1]=='\r')) input[--n]='\0';
        if (!strcmp(input,"@QUIT") || !strcmp(input,"@quit")) break;
        if (!strncmp(input,"@WAIT ",6)) {
            char *end; unsigned long v=strtoul(input+6,&end,10);
            if (end==input+6 || *end || v>MAX_WAIT) puts("ERR invalid wait");
            else wait_ms((uint32_t)v);
        } else send_uart(input);
    }
}
int main(int argc,char **argv)
{
    reset();
    if (argc>1 && !strcmp(argv[1],"--self-test")) return self_test();
    if (argc>1 && !strcmp(argv[1],"--demo")) { demo(); return 0; }
    if (argc>1 && !strcmp(argv[1],"--interactive")) { interactive(); return 0; }
    printf("Usage: %s --self-test | --demo | --interactive\n",argv[0]);
    return 2;
}
