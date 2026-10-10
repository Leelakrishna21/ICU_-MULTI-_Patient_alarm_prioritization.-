/*
 * ICU Alarm System (simple version)
 * QNX Neutrino 7.x / 8.0  -  Raspberry Pi 4 Model B
 *
 * Flow: Sensor -> Duplicate check (4 s) -> POSIX MQ (P1/P2/P3)
 *       -> Aging (P2 > 6 s becomes P1) -> Dispatcher (SCHED_FIFO 25)
 *       -> LEDs + buzzer + console
 *
 * Run as root. If mq_open fails, run:  mqueue &
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

/* ---------- Pins (BCM numbers) ---------- */
#define DHT11_PIN   4    /* header pin 7  */
#define RED_PIN     17   /* header pin 11 */
#define YELLOW_PIN  22   /* header pin 15 */
#define GREEN_PIN   27   /* header pin 13 */
#define BUZZER_PIN  23   /* header pin 16 */

#define LED_ACTIVE_LOW 0 /* set to 1 if your LEDs turn ON when GPIO is LOW */

/* ---------- Settings ---------- */
#define DUP_WINDOW_MS 4000
#define AGING_MS      6000
#define HOLD_MS       2000          /* how long each alarm is shown */
#define MQ_NAME       "/icu_alarm_mq"

#define P1 3   /* Critical */
#define P2 2   /* Warning  */
#define P3 1   /* Advisory */

/* ---------- BCM2711 GPIO registers ---------- */
#define GPIO_BASE 0xFE200000UL
#define GPSET0    7     /* 0x1C / 4 */
#define GPCLR0    10    /* 0x28 / 4 */
#define GPLEV0    13    /* 0x34 / 4 */
#define PUPPDN0   57    /* 0xE4 / 4 */

static volatile uint32_t *gpio;
static uint64_t cps;                  /* CPU clock cycles per second */
static mqd_t mq_tx, mq_rx;
static pthread_mutex_t dup_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int      sensor;        /* 0 = DHT11, 1 = Bed 1, 2 = Bed 2 */
    int      type;          /* 0 = temp, 1 = humidity, 2 = HR, 3 = SpO2 */
    int      prio;
    float    value;
    uint64_t time_ms;
    char     text[32];
} alarm_t;

static const char *SENSOR[] = { "DHT11", "BED-1", "BED-2" };
static const char *PNAME[]  = { "NORMAL", "P3-ADVISORY", "P2-WARNING", "P1-CRITICAL" };

/* ================= Helpers ================= */
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void msleep(int ms) { usleep(ms * 1000); }

/* ================= GPIO ================= */
static void pin_mode(int pin, int output)
{
    int reg = pin / 10, shift = (pin % 10) * 3;
    uint32_t v = gpio[reg];
    v &= ~(7u << shift);
    if (output) v |= (1u << shift);
    gpio[reg] = v;
}

static void pin_write(int pin, int high)
{
    if (high) gpio[GPSET0] = 1u << pin;
    else      gpio[GPCLR0] = 1u << pin;
}

static int pin_read(int pin) { return (gpio[GPLEV0] >> pin) & 1; }

static void led(int pin, int on) { pin_write(pin, on ^ LED_ACTIVE_LOW); }

/* Only ONE LED on at a time: switch all off first, then one on */
static void show(int prio)
{
    led(RED_PIN, 0);
    led(YELLOW_PIN, 0);
    led(GREEN_PIN, 0);
    if (prio == P1)      led(RED_PIN, 1);
    else if (prio == P2) led(YELLOW_PIN, 1);
    else                 led(GREEN_PIN, 1);
}

static int gpio_init(void)
{
    ThreadCtl(_NTO_TCTL_IO, 0);
    void *p = mmap_device_memory(NULL, 0x100, PROT_READ | PROT_WRITE | PROT_NOCACHE, 0, GPIO_BASE);
    if (p == MAP_FAILED) { perror("mmap GPIO (run as root)"); return -1; }
    gpio = (volatile uint32_t *)p;

    int outs[] = { RED_PIN, YELLOW_PIN, GREEN_PIN };
    for (int i = 0; i < 3; i++) {
        led(outs[i], 0);          /* set OFF before making it an output */
        pin_mode(outs[i], 1);
    }
    pin_write(BUZZER_PIN, 0);
    pin_mode(BUZZER_PIN, 1);

    /* DHT11: input with pull-up (2 bits per pin, 01 = pull-up) */
    int shift = (DHT11_PIN % 16) * 2;
    gpio[PUPPDN0] = (gpio[PUPPDN0] & ~(3u << shift)) | (1u << shift);
    pin_mode(DHT11_PIN, 0);
    return 0;
}

/* ================= Step 2 + 3: duplicate check, then send ================= */
static void raise_alarm(int sensor, int type, int prio, float value, const char *text)
{
    static uint64_t last_time[3][4];
    static int      last_prio[3][4];
    uint64_t t = now_ms();

    pthread_mutex_lock(&dup_lock);
    int dup = last_time[sensor][type] != 0 &&
              t - last_time[sensor][type] < DUP_WINDOW_MS &&
              prio <= last_prio[sensor][type];
    if (!dup) {
        last_time[sensor][type] = t;
        last_prio[sensor][type] = prio;
    }
    pthread_mutex_unlock(&dup_lock);

    if (dup) {
        printf("  [suppressed] %s %s\n", SENSOR[sensor], text);
        return;
    }

    alarm_t a = { sensor, type, prio, value, t, "" };
    snprintf(a.text, sizeof a.text, "%s", text);
    mq_send(mq_tx, (char *)&a, sizeof a, prio);   /* mq priority = alarm priority */
    printf("RAISE  %-5s %-11s %s (%.1f)\n", SENSOR[sensor], PNAME[prio], text, value);
}

/* ================= DHT11 ================= */
/* How long (us) the pin stays at 'level'; -1 on timeout */
static int pulse(int level, int timeout_us)
{
    uint64_t start = ClockCycles();
    uint64_t limit = (uint64_t)timeout_us * cps / 1000000;
    while (pin_read(DHT11_PIN) == level)
        if (ClockCycles() - start > limit) return -1;
    return (int)((ClockCycles() - start) * 1000000 / cps);
}

static int dht11_read(int *temp, int *hum)
{
    uint8_t d[5] = { 0 };

    pin_write(DHT11_PIN, 0);       /* start signal: LOW for 20 ms */
    pin_mode(DHT11_PIN, 1);
    msleep(20);
    pin_mode(DHT11_PIN, 0);        /* release line, sensor replies */

    if (pulse(1, 200) < 0) return -1;
    if (pulse(0, 120) < 0) return -1;
    if (pulse(1, 120) < 0) return -1;

    for (int i = 0; i < 40; i++) {
        if (pulse(0, 100) < 0) return -1;
        int high = pulse(1, 120);
        if (high < 0) return -1;
        d[i / 8] <<= 1;
        if (high > 48) d[i / 8] |= 1;   /* ~27 us = 0, ~70 us = 1 */
    }
    if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) return -2;

    *hum  = d[0];
    *temp = d[2];
    return 0;
}

static void *dht11_thread(void *arg)
{
    sleep(1);
    while (1) {
        int t, h;
        if (dht11_read(&t, &h) == 0) {
            printf("DHT11  temp %d C  humidity %d %%\n", t, h);
            if (t >= 40)      raise_alarm(0, 0, P1, t, "Temperature critical");
            else if (t >= 36) raise_alarm(0, 0, P2, t, "Temperature high");
            if (h >= 90)      raise_alarm(0, 1, P2, h, "Humidity high");
            else if (h >= 80) raise_alarm(0, 1, P3, h, "Humidity elevated");
        } else {
            printf("DHT11  read failed (check wiring)\n");
        }
        sleep(2);
    }
    return arg;
}

/* ================= Simulated beds ================= */
static void *bed_thread(void *arg)
{
    int bed = (int)(intptr_t)arg;           /* 1 or 2 */
    unsigned seed = time(NULL) + bed;
    float hr = 75, spo2 = 98;

    while (1) {
        /* random walk, with an occasional bad spike */
        hr   += (rand_r(&seed) % 7) - 3;
        spo2 += ((rand_r(&seed) % 3) - 1) * 0.5f;
        if (rand_r(&seed) % 20 == 0) hr   = 145;   /* tachycardia */
        if (rand_r(&seed) % 25 == 0) spo2 = 84;    /* desaturation */
        if (hr < 60) hr = 60;
        if (hr > 160) hr = 160;
        if (spo2 > 100) spo2 = 100;

        if (hr >= 140)      raise_alarm(bed, 2, P1, hr, "Heart rate critical");
        else if (hr >= 120) raise_alarm(bed, 2, P2, hr, "Heart rate high");
        else if (hr >= 100) raise_alarm(bed, 2, P3, hr, "Heart rate elevated");

        if (spo2 <= 85)      raise_alarm(bed, 3, P1, spo2, "SpO2 critical");
        else if (spo2 <= 90) raise_alarm(bed, 3, P2, spo2, "SpO2 low");

        /* drift back toward normal after a spike */
        if (hr > 80)   hr -= 10;
        if (spo2 < 96) spo2 += 2;
        sleep(1);
    }
    return NULL;
}

/* ================= Step 4 + 5: aging + dispatcher ================= */
static void *dispatcher_thread(void *arg)
{
    alarm_t list[32];
    int count = 0;

    show(0);   /* green */

    while (1) {
        /* 1. Move all new alarms from the queue into our list */
        alarm_t a;
        while (mq_receive(mq_rx, (char *)&a, sizeof a, NULL) > 0)
            if (count < 32) list[count++] = a;

        uint64_t now = now_ms();

        /* 2. Starvation aging: P2 waiting > 6 s becomes P1 */
        for (int i = 0; i < count; i++) {
            if (list[i].prio == P2 && now - list[i].time_ms > AGING_MS) {
                list[i].prio = P1;
                printf("ESCALATE %s %s : P2 -> P1\n", SENSOR[list[i].sensor], list[i].text);
            }
        }

        /* 3. Nothing to do: green LED, check again soon */
        if (count == 0) {
            show(0);
            msleep(50);
            continue;
        }

        /* 4. Pick highest priority (oldest first if equal) */
        int best = 0;
        for (int i = 1; i < count; i++)
            if (list[i].prio > list[best].prio ||
                (list[i].prio == list[best].prio && list[i].time_ms < list[best].time_ms))
                best = i;

        alarm_t cur = list[best];
        list[best] = list[--count];

        printf(">>> ALERT %-11s %s %s (%.1f), waited %llu ms\n",
               PNAME[cur.prio], SENSOR[cur.sensor], cur.text, cur.value,
               (unsigned long long)(now - cur.time_ms));

        /* 5. Outputs: LED, plus pulsing buzzer for P1 */
        show(cur.prio);
        for (int t = 0; t < HOLD_MS; t += 250) {
            if (cur.prio == P1) pin_write(BUZZER_PIN, (t / 250) % 2 == 0);
            msleep(250);
        }
        pin_write(BUZZER_PIN, 0);
    }
    return arg;
}

/* ================= main ================= */
static void start(void *(*fn)(void *), void *arg, int prio)
{
    pthread_t t;
    pthread_attr_t attr;
    struct sched_param sp = { .sched_priority = prio };

    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_setschedparam(&attr, &sp);
    pthread_create(&t, &attr, fn, arg);
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    if (gpio_init() != 0) return 1;

    /* POSIX message queue */
    struct mq_attr attr = { 0 };
    attr.mq_maxmsg  = 32;
    attr.mq_msgsize = sizeof(alarm_t);
    mq_unlink(MQ_NAME);
    mq_rx = mq_open(MQ_NAME, O_CREAT | O_RDONLY | O_NONBLOCK, 0660, &attr);
    mq_tx = mq_open(MQ_NAME, O_WRONLY | O_NONBLOCK);
    if (mq_rx == (mqd_t)-1 || mq_tx == (mqd_t)-1) {
        perror("mq_open (run 'mqueue &' first)");
        return 1;
    }

    /* LED test: red, yellow, green one at a time */
    printf("LED test: RED\n");    show(P1); sleep(1);
    printf("LED test: YELLOW\n"); show(P2); sleep(1);
    printf("LED test: GREEN\n");  show(0);  sleep(1);

    start(dispatcher_thread, NULL, 25);           /* Step 5: SCHED_FIFO 25 */
    start(dht11_thread,      NULL, 30);
    start(bed_thread, (void *)(intptr_t)1, 15);
    start(bed_thread, (void *)(intptr_t)2, 15);

    printf("ICU alarm system running. Ctrl+C to stop.\n");
    while (1) sleep(10);
    return 0;
}
