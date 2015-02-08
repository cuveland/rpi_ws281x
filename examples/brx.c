// brx.c - Blinkenlights udp protocol receiver for rpi_ws281x.
//
// Copyright (c) 2015 Jan de Cuveland <cmail@cuveland.de>

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>

#include <glib-unix.h>
#include <blib/blib.h>

#include "../ws2811.h"


#define GPIO_PIN 18
#define DMA 5

#define WIDTH 18
#define HEIGHT 8

#define PORT 2323


typedef struct {
    ws2811_t leds;
    int width;
    int height;
} led_matrix_t;

typedef struct {
    led_matrix_t led_matrix;
    BReceiver* receiver;
    GMainLoop* loop;
} app_data_t;


// return BPacket pixel at given coordinates as ws281x g-r-b color
static inline ws2811_led_t b_packet_get_pixel_color(BPacket* packet,
                                                    int x, int y)
{
    mcu_frame_header_t* h = &packet->header.mcu_frame_h;
    uint8_t* d = packet->data;

    ws2811_led_t colorval = 0; // default: black

    if (x < h->width && y < h->height) {
        if (h->channels == 1) {
            uint32_t grayval = d[y * h->width + x] * 255 / h->maxval;
            if (grayval > 255) {
                grayval = 255;
            }
            colorval = (grayval << 16) | (grayval << 8) | grayval;
        } else if (h->channels == 3) {
            uint32_t r = d[(y * h->width + x) * 3] * 255 / h->maxval;
            if (r > 255) {
                r = 255;
            }
            uint32_t g = d[(y * h->width + x) * 3 + 1] * 255 / h->maxval;
            if (g > 255) {
                g = 255;
            }
            uint32_t b = d[(y * h->width + x) * 3 + 2] * 255 / h->maxval;
            if (b > 255) {
                b = 255;
            }
            colorval = (g << 16) | (r << 8) | b;
        }
    }

    return colorval;
}


// return pointer to ws281x pixel buffer at given coordinates
// (reflects led strip routing)
static inline ws2811_led_t* led_pixel(led_matrix_t* matrix, int x, int y)
{    
    return &matrix->leds.channel[0].leds[y * matrix->width + x];
}


static void led_clear(led_matrix_t* led_matrix)
{
    for (int x = 0; x < led_matrix->width; x++) {
        for (int y = 0; y < led_matrix->height; y++) {
            *led_pixel(led_matrix, x, y) = 0;
        }
    }
    if (ws2811_render(&led_matrix->leds)) {
        fprintf(stderr, "ws2811_render failed\n");
    }
}


static gboolean on_receive(BReceiver* rec, BPacket* packet,
                           gpointer callback_data)
{
    (void)rec;
    
    app_data_t* app_data = (app_data_t*)callback_data;

#ifdef DEBUG
    printf("width %02d, height %02d, channels %02d, maxval %03d\n",
           packet->header.mcu_frame_h.width,
           packet->header.mcu_frame_h.height,
           packet->header.mcu_frame_h.channels,
           packet->header.mcu_frame_h.maxval);
#endif

    // render matrix
    for (int x = 0; x < app_data->led_matrix.width; x++) {
        for (int y = 0; y < app_data->led_matrix.height; y++) {
            *led_pixel(&app_data->led_matrix, x, y) =
                b_packet_get_pixel_color(packet, x, y);
        }
    }

    if (ws2811_render(&app_data->led_matrix.leds)) {
        fprintf(stderr, "ws2811_render failed\n");
        if (app_data->loop && g_main_loop_is_running(app_data->loop)) {
            g_main_loop_quit(app_data->loop);
        }
    }

    return TRUE; // returning FALSE would stop reception of packets
}


static gboolean on_signal(gpointer user_data)
{
    if (user_data == NULL) {
        return FALSE;
    }

    app_data_t* app_data = (app_data_t*)user_data;
    if (app_data->loop && g_main_loop_is_running(app_data->loop)) {
        g_main_loop_quit(app_data->loop);
    }
    return FALSE;
}


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    app_data_t* app_data;
    if ((app_data = calloc(1, sizeof(app_data_t))) == NULL) {
        perror("calloc");
        return EXIT_FAILURE;        
    }

    app_data->led_matrix.width = WIDTH;
    app_data->led_matrix.height = HEIGHT;
    app_data->led_matrix.leds.freq = WS2811_TARGET_FREQ;
    app_data->led_matrix.leds.dmanum = DMA;
    app_data->led_matrix.leds.channel[0].gpionum = GPIO_PIN;
    app_data->led_matrix.leds.channel[0].count = WIDTH * HEIGHT;
    app_data->led_matrix.leds.channel[0].brightness = 255;
    
    if (ws2811_init(&app_data->led_matrix.leds)) {
        fprintf(stderr, "ws2811_init failed\n");
        free(app_data);
        return EXIT_FAILURE;
    }

    b_init();

    if ((app_data->receiver = b_receiver_new(on_receive, app_data)) == NULL) {
        fprintf(stderr, "b_receiver_new failed\n");
        ws2811_fini(&app_data->led_matrix.leds);
        free(app_data);
        return EXIT_FAILURE;
    }

    b_receiver_listen(app_data->receiver, PORT);

    printf("waiting for packets on port %d, press CTRL-C to stop\n", PORT);
    app_data->loop = g_main_loop_new(NULL, FALSE);

    g_unix_signal_add(SIGINT, on_signal, app_data);
    g_unix_signal_add(SIGTERM, on_signal, app_data);
    
    g_main_loop_run(app_data->loop);
    printf("\n");
    g_main_loop_unref(app_data->loop);
    app_data->loop = NULL;

    g_object_unref(app_data->receiver);

    led_clear(&app_data->led_matrix);
    ws2811_fini(&app_data->led_matrix.leds);

    free(app_data);
    return EXIT_SUCCESS;
}
