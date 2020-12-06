#include <cstdlib>
#include <cstring>
#include <errno.h>
#include "sys/stat.h"

#define HAVE_STRINGS_H
#define HAVE_STDINT_H
#define MAP_BUTTON(id, name) S9xMapButton((id), S9xGetCommandT((name)), false)

#include "snes9x.h"
#include "conffile.h"
#include "controls.h"
#include "display.h"
#include "memmap.h"
#include "apu.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
	#include "rg_system.h"
}

#define APP_ID 60

#define AUDIO_SAMPLE_RATE   (32000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50 + 1)

// static uint32_t audioBuffer[AUDIO_BUFFER_LENGTH];

static odroid_video_frame_t update1;
static odroid_video_frame_t update2;
static odroid_video_frame_t *currentUpdate = &update1;

static rg_app_desc_t *app;


extern void snes_task(void*);

extern "C" void app_main()
{
	// I tried reducing DMA, Reserved memory, and whatnot but it just won't allocate a continuous 128K segment :(
	// Internal ram is a bit faster than spiram + it makes room for loading bigger ROMS
	Memory.RAM1 = (uint8 *) rg_alloc(0x10000, MEM_FAST);
    Memory.RAM2 = (uint8 *) rg_alloc(0x10000, MEM_FAST);

	//heap_caps_malloc_extmem_enable(32768);

    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(NULL, NULL, NULL);

	update1.width = update2.width = SNES_WIDTH;
    update1.height = update2.height = SNES_HEIGHT;
    update1.stride = update2.stride = SNES_WIDTH * 2;
    update1.pixel_size = update2.pixel_size = 2;
    update1.pixel_clear = update2.pixel_clear = -1;

    update1.buffer = rg_alloc(update1.stride * update1.height, MEM_SLOW);
    // update2.buffer = rg_alloc(update2.stride * update2.height, MEM_FAST);

    app = odroid_system_get_app();

	const char *rom_file = app->romPath;

	xTaskCreatePinnedToCore(&snes_task, "snes_task", 1024 * 32, (void *)rom_file, 10, NULL, 1);
	vTaskDelete(NULL);
}


uint32_t timer = 0;
uint32_t loops = 0;

void S9xMessage (int type, int number, const char *message)
{
	printf("%s\n", message);
}

void S9xParseArg (char **argv, int &i, int argc)
{

}

void S9xParsePortConfig (ConfigFile &conf, int pass)
{

}

const char * S9xGetDirectory (enum s9x_getdirtype dirtype)
{
	return "";
}

const char * S9xGetFilename (const char *ex, enum s9x_getdirtype dirtype)
{
	return odroid_system_get_path(ODROID_PATH_SAVE_SRAM, app->romPath);
}

bool8 S9xContinueUpdate (int width, int height)
{
	return (TRUE);
}

void S9xToggleSoundChannel (int c)
{

}

void S9xAutoSaveSRAM (void)
{

}

static uint8_t skip = 0;
void S9xSyncSpeed (void)
{
	IPPU.RenderThisFrame = !IPPU.RenderThisFrame;
	//IPPU.RenderThisFrame = skip == 0;
	//if (skip++ == 10) skip = 0;
	//IPPU.RenderThisFrame = true;

	loops++;

	uint32_t time = (esp_timer_get_time() / 1000);

	if ((time - timer) >= 1000) {
		printf("Sync time avg %.2fms\n", (float)(time - timer) / loops);
		timer = time;
		loops = 0;
	}

	odroid_gamepad_state_t joystick;
	odroid_input_read_gamepad(&joystick);
	for (int i = 0; i < ODROID_INPUT_MAX; i++) {
		S9xReportButton(i, joystick.values[i]);
	}
}

bool S9xPollButton (uint32 id, bool *pressed)
{
	return false;
}

bool S9xPollAxis (uint32 id, int16 *value)
{
	return false;
}

bool S9xPollPointer (uint32 id, int16 *x, int16 *y)
{
	return false;
}

void S9xSetPalette (void)
{

}

void S9xTextMode (void)
{

}

void S9xGraphicsMode (void)
{

}

void S9xHandlePortCommand (s9xcommand_t cmd, int16 data1, int16 data2)
{

}

void S9xExit (void)
{
	exit(0);
}

void S9xInitDisplay (int argc, char **argv)
{
	// Setup SNES buffers
	GFX.Pitch = SNES_WIDTH * 2;
	GFX.Screen = (uint16*)currentUpdate->buffer;
	//GFX.Screen = (uint16 *) heap_caps_calloc(GFX.Pitch * (SNES_HEIGHT_EXTENDED + 4), 1, MALLOC_CAP_SPIRAM);
	S9xGraphicsInit();
}

void S9xDeinitDisplay (void)
{

}

bool8 S9xInitUpdate (void)
{

	return (TRUE);
}

bool8 S9xDeinitUpdate (int width, int height)
{
	odroid_display_queue_update(currentUpdate, NULL);

	return (TRUE);
}

void snes_ppu_task(void *arg)
{
	while(1) {
		if (IPPU.RefreshNow) {
			IPPU.RefreshNow = false;
			S9xUpdateScreen();
		}
	}
}

void snes_task(void *arg) // IRAM_ATTR
{
	memset(&Settings, 0, sizeof(Settings));
	Settings.SupportHiRes = FALSE;
	Settings.Transparency = TRUE;
	Settings.AutoDisplayMessages = TRUE;
	Settings.InitialInfoStringTimeout = 120;
	Settings.HDMATimingHack = 100;
	Settings.BlockInvalidVRAMAccessMaster = TRUE;
	Settings.StopEmulation = TRUE;
	Settings.SkipFrames = AUTO_FRAMERATE;
	Settings.TurboSkipFrames = 15;
	Settings.CartAName[0] = 0;
	Settings.CartBName[0] = 0;

	S9xLoadConfigFiles(NULL, 0);

	if (!Memory.Init() || !S9xInitAPU())
	{
		fprintf(stderr, "Snes9x: Memory allocation failure - not enough RAM/virtual memory available.\nExiting...\n");
		Memory.Deinit();
		S9xDeinitAPU();
		exit(1);
	}

	bool8 loaded = FALSE;

	odroid_system_spi_lock_acquire(SPI_LOCK_SDCARD);
	loaded = Memory.LoadROM((const char*)arg);
	odroid_system_spi_lock_release(SPI_LOCK_SDCARD);

	if (!loaded)
	{
		RG_PANIC("Error opening the ROM file.\n");
		exit(1);
	} else {
		printf("ROM loaded!\n");
	}

	CPU.Flags = 0;
	Settings.StopEmulation = FALSE;
	Settings.Paused = FALSE;
	Settings.SoundSync = FALSE;

	S9xInitSound(0);
	S9xReportControllers();
	S9xSetController(0, CTL_JOYPAD, 0, 0, 0, 0);
	S9xSetController(1, CTL_NONE, 1, 0, 0, 0);
	S9xInitDisplay(NULL, 0);

    MAP_BUTTON(ODROID_INPUT_A, "Joypad1 A");
    MAP_BUTTON(ODROID_INPUT_B, "Joypad1 B");
    MAP_BUTTON(ODROID_INPUT_START, "Joypad1 X");
    MAP_BUTTON(ODROID_INPUT_SELECT, "Joypad1 Y");
    MAP_BUTTON(ODROID_INPUT_MENU, "Joypad1 Select");
    MAP_BUTTON(ODROID_INPUT_VOLUME, "Joypad1 Start");
    //MAP_BUTTON(BTN_L, "Joypad1 L");
    //MAP_BUTTON(BTN_R, "Joypad1 R");
    MAP_BUTTON(ODROID_INPUT_LEFT, "Joypad1 Left");
    MAP_BUTTON(ODROID_INPUT_RIGHT, "Joypad1 Right");
    MAP_BUTTON(ODROID_INPUT_UP, "Joypad1 Up");
    MAP_BUTTON(ODROID_INPUT_DOWN, "Joypad1 Down");

	printf("Starting loop!\n");

	sleep(1);

	while (1)
	{
		S9xMainLoop();
	}
}

void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
	*drive = 0;

	const char	*slash = strrchr(path, SLASH_CHAR),
				*dot   = strrchr(path, '.');

	if (dot && slash && dot < slash)
		dot = NULL;

	if (!slash)
	{
		*dir = 0;

		strcpy(fname, path);

		if (dot)
		{
			fname[dot - path] = 0;
			strcpy(ext, dot + 1);
		}
		else
			*ext = 0;
	}
	else
	{
		strcpy(dir, path);
		dir[slash - path] = 0;

		strcpy(fname, slash + 1);

		if (dot)
		{
			fname[dot - slash - 1] = 0;
			strcpy(ext, dot + 1);
		}
		else
			*ext = 0;
	}
}

void _makepath(char *path, const char *, const char *dir, const char *fname, const char *ext)
{
	if (dir && *dir)
	{
		strcpy(path, dir);
		strcat(path, SLASH_STR);
	}
	else
		*path = 0;

	strcat(path, fname);

	if (ext && *ext)
	{
		strcat(path, ".");
		strcat(path, ext);
	}
}
