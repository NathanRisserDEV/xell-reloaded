#include <stdint.h>

#include <console/console.h>
#include <time/time.h>

#include "splash.h"
#include "splash_image.h"

#define SPLASH_MAX_PERCENT 80

static unsigned char rgb565_red(uint16_t pixel) {
    unsigned char value = (pixel >> 8) & 0xf8;
    return value | (value >> 5);
}

static unsigned char rgb565_green(uint16_t pixel) {
    unsigned char value = (pixel >> 3) & 0xfc;
    return value | (value >> 6);
}

static unsigned char rgb565_blue(uint16_t pixel) {
    unsigned char value = (pixel << 3) & 0xf8;
    return value | (value >> 5);
}

static void draw_scaled_splash(int origin_x, int origin_y, int target_width, int target_height) {
    int y;

    for (y = 0; y < target_height; ++y) {
        int x;
        int src_y = y * GALAXY_SPLASH_HEIGHT / target_height;

        for (x = 0; x < target_width; ++x) {
            int src_x = x * GALAXY_SPLASH_WIDTH / target_width;
            uint16_t pixel = galaxy_splash_rgb565[src_y * GALAXY_SPLASH_WIDTH + src_x];

            if (pixel == 0)
                continue;

            console_pset(
                origin_x + x,
                origin_y + y,
                rgb565_red(pixel),
                rgb565_green(pixel),
                rgb565_blue(pixel)
            );
        }
    }
}

void galaxy_splash_show(unsigned int seconds) {
    unsigned int columns = 0;
    unsigned int rows = 0;
    int safe_width;
    int safe_height;
    int max_width;
    int max_height;
    int target_width = GALAXY_SPLASH_WIDTH;
    int target_height = GALAXY_SPLASH_HEIGHT;
    int origin_x;
    int origin_y;

    console_get_dimensions(&columns, &rows);

    safe_width = (int)columns * 8;
    safe_height = (int)rows * 16;

    if (safe_width <= 0 || safe_height <= 0)
        return;

    console_clrscr();

    max_width = safe_width * SPLASH_MAX_PERCENT / 100;
    max_height = safe_height * SPLASH_MAX_PERCENT / 100;

    if (max_width <= 0 || max_height <= 0)
        return;

    if (target_width > max_width) {
        target_height = target_height * max_width / target_width;
        target_width = max_width;
    }

    if (target_height > max_height) {
        target_width = target_width * max_height / target_height;
        target_height = max_height;
    }

    if (target_width <= 0 || target_height <= 0)
        return;

    origin_x = (safe_width - target_width) / 2;
    origin_y = (safe_height - target_height) / 2;

    draw_scaled_splash(origin_x, origin_y, target_width, target_height);
    console_putch(13);

    if (seconds > 0)
        delay(seconds);

    console_clrscr();
}
