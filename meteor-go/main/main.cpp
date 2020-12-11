extern "C" {
#include <rg_system.h>
}

#include "ameteor.hpp"
#include "ameteor/cartmem.hpp"
#include <sstream>
#include <stdio.h>
#include <cstring>
#include <assert.h>

#define APP_ID 70

#define AUDIO_SAMPLE_RATE   (44100)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50 + 1)

#define GBA_WIDTH 240
#define GBA_HEIGHT 160

static int16_t audioBuffer[AUDIO_BUFFER_LENGTH * 2];

static odroid_video_frame_t update1;
static odroid_video_frame_t update2;
static odroid_video_frame_t *currentUpdate = &update1;

static rg_app_desc_t *app;




class Audio
{
public:
    void InitAMeteor()
    {
        AMeteor::_sound.GetSpeaker().SetFrameSlot(syg::mem_fun(*this, &Audio::PlayFrames));
    }

    void PlayFrames(const int16_t* data)
    {
        // pretro_sample(data[0], data[1]);
    }
};

class Video
{
public:
    void InitAMeteor()
    {
        AMeteor::_lcd.GetScreen().GetRenderer().SetFrameSlot(
                syg::mem_fun(*this, &Video::ShowFrame));
    }

    void ShowFrame (const uint16_t* frame)
    {
        for (unsigned i = 0; i < sizeof(GBA_WIDTH * GBA_HEIGHT) / sizeof(uint16_t); i++)
        {
            uint16_t col = frame[i];
            uint16_t b = (col >> 10) & 0x1f;
            uint16_t g = (col >>  5) & 0x1f;
            g = (g << 1) | (g >> 4);
            uint16_t r = (col >>  0) & 0x1f;
            ((uint16_t*)(update1.buffer))[i] = (r << 11) | (g << 5) | (b << 0);
        }
        AMeteor::Stop();
        odroid_display_queue_update(&update1, NULL);
        printf("FRAME!\n");
        // AMeteor::Stop();
        // pretro_refresh(conv_buf, 240, 160, 240 * 2);
    }
};

class Input
{
	public:
    void InitAMeteor()
    {
        AMeteor::_lcd.sig_vblank.connect(syg::mem_fun(*this, &Input::CheckEvents));
    }

    void CheckEvents()
    {
    }
};

extern "C" void app_main()
{
    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(NULL, NULL, NULL);

    app = odroid_system_get_app();

    update1.width = update2.width = GBA_WIDTH;
    update1.height = update2.height = GBA_HEIGHT;
    update1.stride = update2.stride = GBA_WIDTH * 2;
    update1.pixel_size = update2.pixel_size = 2;
    update1.pixel_clear = update2.pixel_clear = -1;

    update1.buffer = rg_alloc(update1.stride * update1.height, MEM_SLOW);
    // update2.buffer = rg_alloc(update2.stride * update2.height, MEM_FAST);

    const char *rom_file = app->romPath;

    Video am_video;
    Audio am_audio;
    Input am_input;
    am_video.InitAMeteor();
    am_audio.InitAMeteor();
    am_input.InitAMeteor();

    odroid_system_spi_lock_acquire(SPI_LOCK_SDCARD);
    if (AMeteor::_memory.LoadRom(rom_file))
        printf("ROM LOADED!\n");
    else
        printf("ROM NOT LOADED!\n");
    odroid_system_spi_lock_release(SPI_LOCK_SDCARD);

    AMeteor::Reset(AMeteor::UNIT_ALL ^ (AMeteor::UNIT_MEMORY_BIOS | AMeteor::UNIT_MEMORY_ROM));

    while (true)
    {
        // pretro_poll();
        AMeteor::Run(10000000); // We emulate until VBlank.
        printf("LOOP!\n");
    }
}
