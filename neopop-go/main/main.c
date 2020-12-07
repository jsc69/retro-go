#include <errno.h>
#include <sys/stat.h>
#include <rg_system.h>

#define APP_ID 70

#define AUDIO_SAMPLE_RATE   (44100)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50 + 1)

#define FB_WIDTH 160
#define FB_HEIGHT 152

static int16_t audioBuffer[AUDIO_BUFFER_LENGTH * 2];

static odroid_video_frame_t update1;
static odroid_video_frame_t update2;
static odroid_video_frame_t *currentUpdate = &update1;

static rg_app_desc_t *app;

/*---------------------------------------------------------------------------
 * NEOPOP : Emulator as in Dreamland
 *
 * Copyright (c) 2001-2002 by neopop_uk
 *---------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version. See also the license.txt file for
 *    additional informations.
 *---------------------------------------------------------------------------
 */
#include <libretro.h>
#include <string.h>

#include "git.h"
#include "state.h"
#include "state_helpers.h"
#include "mednafen-types.h"

#include "ngp/neopop.h"
#include "ngp/TLCS-900h/TLCS900h_interpret.h"
#include "ngp/TLCS-900h/TLCS900h_registers.h"
#include "ngp/Z80_interface.h"
#include "ngp/interrupt.h"
#include "ngp/mem.h"
#include "ngp/rom.h"
#include "ngp/gfx.h"
#include "ngp/sound.h"
#include "ngp/dma.h"
#include "ngp/bios.h"
#include "ngp/flash.h"
#include "ngp/system.h"

/* ==================================================== */

static MDFN_Surface surf;
static ngpgfx_t NGPGfx_obj;
extern uint8_t CPUExRAM[16384];
static uint8_t *chee;
static int32_t z80_runtime;
extern int32_t ngpc_soundTS;

ngpgfx_t *NGPGfx = &NGPGfx_obj;

void neopop_reset(void)
{
   ngpgfx_power(NGPGfx);
   Z80_reset();
   reset_int();
   reset_timers();

   reset_memory();
   BIOSHLE_Reset();
   reset_registers();    /* TLCS900H registers */
   reset_dma();
}

void app_main()
{
    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(NULL, NULL, NULL);

    update1.width = update2.width = FB_WIDTH;
    update1.height = update2.height = FB_HEIGHT;
    update1.stride = update2.stride = FB_WIDTH * 2;
    update1.pixel_size = update2.pixel_size = 2;
    update1.pixel_clear = update2.pixel_clear = -1;

    update1.buffer = rg_alloc(update1.stride * update1.height, MEM_SLOW);
    // update2.buffer = rg_alloc(update2.stride * update2.height, MEM_FAST);

    app = odroid_system_get_app();

    const char *rom_file = app->romPath;

    // Init
    ngpc_rom.data = rg_alloc(0x200000, MEM_SLOW);
    int actual_size = odroid_sdcard_read_file(rom_file, ngpc_rom.data, 0x200000);
    if (actual_size <= 0)
    {
        RG_PANIC("ROM file loading failed!");
    }
    ngpc_rom.length = actual_size;

    rom_loaded();

   //  MDFNMP_Init(1024, 1024 * 1024 * 16 / 1024);

    NGPGfx->layer_enable = 1 | 2 | 4;

    MDFNNGPCSOUND_Init();

   //  MDFNMP_AddRAM(16384, 0x4000, CPUExRAM);

    SetFRM(); /* Set up fast read memory mapping */

    bios_install();

    z80_runtime = 0;

    neopop_reset();

    surf.width  = currentUpdate->width;
    surf.height = currentUpdate->height;
    surf.pitch  = currentUpdate->stride;
    surf.depth  = currentUpdate->pixel_size * 8;
    surf.pixels = currentUpdate->buffer;

    ngpgfx_set_pixel_format(NGPGfx, 16);
    MDFNNGPC_SetSoundRate(AUDIO_SAMPLE_RATE);

    // Run
    while (true)
    {
        // for (j = 0; j < (RETRO_DEVICE_ID_JOYPAD_R3+1); j++)
        //     if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, j))
        //         ret |= (1 << j);

      //   update_input();
      bool MeowMeow        = false;

      storeB(0x6F82, *chee);

      do
      {
         int32_t timetime = (uint8)TLCS900h_interpret();
         MeowMeow |= updateTimers(&surf, timetime);
         z80_runtime += timetime;

         while(z80_runtime > 0)
         {
            int z80rantime = Z80_RunOP();

            if (z80rantime < 0) /* Z80 inactive, so take up all run time! */
            {
               z80_runtime = 0;
               break;
            }

            z80_runtime -= z80rantime << 1;

         }
      }while(!MeowMeow);

      int SoundBufSize = MDFNNGPCSOUND_Flush(&audioBuffer, AUDIO_BUFFER_LENGTH * 2);

      //   SoundBufSize      = spec.SoundBufSize - spec.SoundBufSizeALMS;
      //   spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;

      //   video_cb(surf->pixels, width, height, FB_WIDTH * 2);

      //   for (total = 0; total < spec.SoundBufSize; )
      //       total += audio_batch_cb(sound_buf + total*2, spec.SoundBufSize - total);
    }
}
