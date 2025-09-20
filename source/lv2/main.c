#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <debug.h>
#include <xenos/xenos.h>
#include <console/console.h>
#include <time/time.h>
#include <ppc/timebase.h>
#include <usb/usbmain.h>
#include <sys/iosupport.h>
#include <ppc/register.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xenon_nand/xenon_config.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_soc/xenon_io.h>
#include <xenon_sound/sound.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_smc/xenon_gpio.h>
#include <xb360/xb360.h>
#include <network/network.h>
#include <httpd/httpd.h>
#include <diskio/ata.h>
#include <elf/elf.h>
#include <version.h>
#include <byteswap.h>

#include "asciiart.h"
#include "config.h"
#include "file.h"
#include "tftp/tftp.h"

#include "log.h"

void do_asciiart() {
    char *p = asciiart;
    while (*p)
        console_putch(*p++);
    printf(asciitail);
}

void dumpana() {
    int i;
    for (i = 0; i < 0x100; ++i)
    {
        uint32_t v;
        xenon_smc_ana_read(i, &v);
        printf("0x%08x, ", (unsigned int)v);
        if ((i&0x7)==0x7)
            printf(" // %02x\n", (unsigned int)(i &~0x7));
    }
}

char FUSES[350]; /* this string stores the ascii dump of the fuses */
char CBLDV[17]; // 16 + terminate
char FGLDV[80];
int cbldvcount;
int fgldvcount;

unsigned char stacks[6][0x10000];

/* animation stack for thread */
static unsigned char anim_stack[0x10000];

/* crude millisecond-ish delay */
static void delay_ms(unsigned int ms)
{
    volatile unsigned int i, j;
    for (j = 0; j < ms; ++j) {
        for (i = 0; i < 20000; ++i) {
            __asm__ volatile ("" ::: "memory");
        }
    }
}

/* animation thread */
void animate_madeby_task()
{
    const char *s = "made by squidwidthe1st on discord";
    int len = strlen(s);
    int i;

    printf("\n"); // reserve a line for animation

    for (;;) {
        // type out
        for (i = 1; i <= len; ++i) {
            printf("\r%.*s", i, s);
            fflush(stdout);
            delay_ms(60);
        }

        delay_ms(600);

        // delete
        for (i = len; i >= 0; --i) {
            printf("\r%.*s", i, s);
            printf("%*s", len - i, " ");
            fflush(stdout);
            delay_ms(30);
        }

        delay_ms(300);
    }
}

void reset_timebase_task()
{
    mtspr(284,0); // TBLW
    mtspr(285,0); // TBUW
    mtspr(284,0);
}

void synchronize_timebases()
{
    xenon_thread_startup();
    
    std((void*)0x200611a0,0); // stop timebase
    
    int i;
    for(i=1;i<6;++i){
        xenon_run_thread_task(i,&stacks[i][0xff00],(void *)reset_timebase_task);
        while(xenon_is_thread_task_running(i));
    }
    
    reset_timebase_task(); // don't forget thread 0
            
    std((void*)0x200611a0,0x1ff); // restart timebase
}
    
int main(){
    LogInit();
    int i;

    printf("ANA Dump before Init:\n");
    dumpana();

    synchronize_timebases();
    
    *(volatile uint32_t*)0xea00106c = 0x1000000;
    *(volatile uint32_t*)0xea001064 = 0x10;
    *(volatile uint32_t*)0xea00105c = 0xc000000;

    xenon_smc_start_bootanim();

    setbuf(stdout,NULL);

    xenos_init(VIDEO_MODE_AUTO);

    printf("ANA Dump after Init:\n");
    dumpana();

#ifdef SWIZZY_THEME
    console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_ORANGE);
#elif defined XTUDO_THEME
    console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_PINK);
#elif defined DEFAULT_THEME
    console_set_colors(CONSOLE_COLOR_BLACK, CONSOLE_COLOR_PURPLE);
#else
    console_set_colors(CONSOLE_COLOR_BLACK,CONSOLE_COLOR_GREEN);
#endif
    console_init();

    printf("\nXeLL RELOADED GALAXYRGH - Xenon Linux Loader 2nd Stage " LONGVERSION "\n");
    do_asciiart();
    
    xenon_sound_init();
    xenon_make_it_faster(XENON_SPEED_FULL);

    if (xenon_get_console_type() != REV_CORONA_PHISON)
    {
        printf(" * nand init\n");
        sfcx_init();
        if (sfc.initialized != SFCX_INITIALIZED)
        {
            printf(" ! sfcx initialization failure\n");
            printf(" ! nand related features will not be available\n");
            delay(5);
        }
    }

    xenon_config_init();

#ifndef NO_NETWORKING
    printf(" * network init\n");
    network_init();
    printf(" * starting httpd server...");
    httpd_start();
    printf("success\n");
#endif

    printf(" * usb init\n");
    usb_init();
    usb_do_poll();

    printf(" * sata hdd init\n");
    xenon_ata_init();

#ifndef NO_DVD
    printf(" * sata dvd init\n");
    xenon_atapi_init();
#endif

    mount_all_devices();
    /*int device_list_size = */ findDevices();

    console_clrscr();
    printf(" _________________________________________________\n|                                                 |\n|  XeLL RELOADED - Xenon Linux Loader             |\n|  GalaxyRGH build by squidwidthe1st on Discord   |\n|_________________________________________________|\n"); 
    
#ifndef NO_PRINT_CONFIG
    printf("\n * FUSES to this system - write them down and keep them safe:\n");
    char *fusestr = FUSES;
    char *cbldvstr = CBLDV;
    char *fgldvstr = FGLDV;
    
    for (i = 0; i < 12; ++i){
        u64 line;
        unsigned int hi,lo;
        
        line = xenon_secotp_read_line(i);
        hi=line >> 32;
        lo=line & 0xffffffff;

        fusestr += sprintf(fusestr, "fuseset %02d: %08x%08x\n", i, hi, lo);

        if (i >= 7) {
            fgldvstr += sprintf(fgldvstr, "%08x%08x", hi, lo) + '\0';
        }
        if (i == 2) {
            cbldvstr += sprintf(cbldvstr, "%08x%08x", hi, lo);
        }
    }

    for (i = 0; CBLDV[i] != '\0' ; ++i) {
        if ('f' == CBLDV[i]) {
            cbldvcount = i + 1;
        }
    }

    for (i = 0; FGLDV[i] != '\0'; ++i) {
        if ('f' == FGLDV[i]) {
            ++fgldvcount;
        }
    }
    
    printf(FUSES);
    
    print_cpu_dvd_keys();
    print_serials();
    
    printf(" * CPU PVR: %08x\n", mfspr(287));
    
    if (xenon_get_console_type() == 0) {
        printf(" * Console: Xenon System\n");
    } else if (xenon_get_console_type() == 1) {
        printf(" * Console: Xenon/Zephyr System\n");
    } else if (xenon_get_console_type() == 2) {
        printf(" * Console: Falcon System\n");
    } else if (xenon_get_console_type() == 3) {
        printf(" * Console: Jasper System\n");
    } else if (xenon_get_console_type() == 4) {
        printf(" * Console: Trinity System\n");
        printf(" * Glitch Method: RGH-3\n");
        printf(" * Glitch Chip: NONE-Chipless | 10k Resistor\n");

        // start animated thread instead of static modder line
        xenon_run_thread_task(5, &anim_stack[0xff00], (void *)animate_madeby_task);

        printf(" * Mod Date: 9/20/2025\n");
    } else if (xenon_get_console_type() == 5) {
        printf(" * Console: Corona System\n");
    } else if (xenon_get_console_type() == 6) {
        printf(" * Console: Corona MMC System\n");
    } else if (xenon_get_console_type() == 7) {
        printf(" * Console: Winchester - how did you get here??? really how?\n");
    } else if (xenon_get_console_type() == -1) {
        printf(" * Console: Unknown ERROR\n");
    }
    
    printf(" * 2BL LDV: %d\n", cbldvcount);
    printf(" * 6BL LDV: %d\n", fgldvcount);
    
    network_print_config();
#endif
    LogDeInit();
    
    mount_all_devices();
    printf("\n * Looking for files on local media and TFTP...\n\n");
    for(;;){
        fileloop();
        tftp_loop();
        console_clrline();
        usb_do_poll();
    }

    return 0;
}
