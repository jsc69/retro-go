#include <rg_system.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include "gui.h"

#define IMAGE_LOGO_WIDTH    (47)
#define IMAGE_LOGO_HEIGHT   (51)
#define IMAGE_BANNER_WIDTH  (272)
#define IMAGE_BANNER_HEIGHT (32)

#define CRC_WIDTH    (104)
#define CRC_X_OFFSET (RG_SCREEN_WIDTH - CRC_WIDTH)
#define CRC_Y_OFFSET (35)

#define LIST_WIDTH       (RG_SCREEN_WIDTH)
#define LIST_HEIGHT      (RG_SCREEN_HEIGHT - LIST_Y_OFFSET)
#define LIST_LINE_COUNT  ((RG_SCREEN_HEIGHT - LIST_Y_OFFSET) / LIST_LINE_HEIGHT)
#define LIST_LINE_HEIGHT (rg_gui_get_font_info().height)
#define LIST_X_OFFSET    (0)
#define LIST_Y_OFFSET    (48 + LIST_LINE_HEIGHT)

#define COVER_MAX_HEIGHT (184)
#define COVER_MAX_WIDTH  (184)

static const theme_t gui_themes[] = {
    {0, C_GRAY, C_WHITE, C_AQUA},
    {0, C_GRAY, C_GREEN, C_AQUA},
    {0, C_WHITE, C_GREEN, C_AQUA},

    {5, C_GRAY, C_WHITE, C_AQUA},
    {5, C_GRAY, C_GREEN, C_AQUA},
    {5, C_WHITE, C_GREEN, C_AQUA},

    {11, C_GRAY, C_WHITE, C_AQUA},
    {11, C_GRAY, C_GREEN, C_AQUA},
    {11, C_WHITE, C_GREEN, C_AQUA},

    {16, C_GRAY, C_WHITE, C_AQUA},
    {16, C_GRAY, C_GREEN, C_AQUA},
    {16, C_WHITE, C_GREEN, C_AQUA},
};
int gui_themes_count = sizeof(gui_themes) / sizeof(theme_t);

static char str_buffer[128];

retro_gui_t gui;

void gui_event(gui_event_t event, tab_t *tab)
{
    if (tab->event_handler)
        (*tab->event_handler)(event, tab);
}

tab_t *gui_add_tab(const char *name, const rg_image_t *logo, const rg_image_t *header, void *arg, void *event_handler)
{
    tab_t *tab = calloc(1, sizeof(tab_t));

    sprintf(tab->name, "%s", name);
    sprintf(tab->status, "Loading...");

    tab->event_handler = event_handler;
    tab->img_header = header;
    tab->img_logo = logo;
    tab->initialized = false;
    tab->is_empty = false;
    tab->arg = arg;

    gui.tabs[gui.tabcount++] = tab;

    printf("gui_add_tab: Tab '%s' added at index %d\n", tab->name, gui.tabcount - 1);

    return tab;
}

void gui_init_tab(tab_t *tab)
{
    if (tab->initialized)
        return;

    tab->initialized = true;
    // tab->status[0] = 0;

    sprintf(str_buffer, "Sel.%.11s", tab->name);
    tab->listbox.cursor = rg_settings_int32_get(str_buffer, 0);

    gui_event(TAB_INIT, tab);

    tab->listbox.cursor = MIN(tab->listbox.cursor, tab->listbox.length -1);
    tab->listbox.cursor = MAX(tab->listbox.cursor, 0);
}

tab_t *gui_get_tab(int index)
{
    return (index >= 0 && index < gui.tabcount) ? gui.tabs[index] : NULL;
}

tab_t *gui_get_current_tab()
{
    return gui_get_tab(gui.selected);
}

tab_t *gui_set_current_tab(int index)
{
    index %= gui.tabcount;

    if (index < 0)
        index += gui.tabcount;

    gui.selected = index;

    return gui_get_tab(gui.selected);
}

void gui_save_current_tab()
{
    tab_t *tab = gui_get_current_tab();

    sprintf(str_buffer, "Sel.%.11s", tab->name);
    rg_settings_int32_set(str_buffer, tab->listbox.cursor);
    rg_settings_int32_set("SelectedTab", gui.selected);
    rg_settings_commit();
}

listbox_item_t *gui_get_selected_item(tab_t *tab)
{
    listbox_t *list = &tab->listbox;

    if (list->cursor >= 0 && list->cursor < list->length)
        return &list->items[list->cursor];

    return NULL;
}

static int list_comparator(const void *p, const void *q)
{
    return strcasecmp(((listbox_item_t*)p)->text, ((listbox_item_t*)q)->text);
}

void gui_sort_list(tab_t *tab, int sort_mode)
{
    if (tab->listbox.length == 0)
        return;

    qsort((void*)tab->listbox.items, tab->listbox.length, sizeof(listbox_item_t), list_comparator);
}

void gui_resize_list(tab_t *tab, int new_size)
{
    int cur_size = tab->listbox.length;

    if (new_size == cur_size)
        return;

    if (new_size == 0)
    {
        free(tab->listbox.items);
        tab->listbox.items = NULL;
    }
    else
    {
        tab->listbox.items = realloc(tab->listbox.items, new_size * sizeof(listbox_item_t));
        for (int i = cur_size; i < new_size; i++)
            memset(&tab->listbox.items[i], 0, sizeof(listbox_item_t));
    }

    tab->listbox.length = new_size;
    tab->listbox.cursor = MIN(tab->listbox.cursor, tab->listbox.length -1);
    tab->listbox.cursor = MAX(tab->listbox.cursor, 0);

    printf("gui_resize_list: Resized list '%s' from %d to %d items\n", tab->name, cur_size, new_size);
}

void gui_scroll_list(tab_t *tab, scroll_mode_t mode)
{
    listbox_t *list = &tab->listbox;

    if (list->length == 0 || list->cursor > list->length) {
        return;
    }

    int cur_cursor = list->cursor;
    int old_cursor = list->cursor;

    if (mode == LINE_UP) {
        cur_cursor--;
    }
    else if (mode == LINE_DOWN) {
        cur_cursor++;
    }
    else if (mode == PAGE_UP) {
        char st = ((char*)list->items[cur_cursor].text)[0];
        int max = LIST_LINE_COUNT - 2;
        while (--cur_cursor > 0 && max-- > 0)
        {
           if (st != ((char*)list->items[cur_cursor].text)[0]) break;
        }
    }
    else if (mode == PAGE_DOWN) {
        char st = ((char*)list->items[cur_cursor].text)[0];
        int max = LIST_LINE_COUNT - 2;
        while (++cur_cursor < list->length-1 && max-- > 0)
        {
           if (st != ((char*)list->items[cur_cursor].text)[0]) break;
        }
    }

    if (cur_cursor < 0) cur_cursor = list->length - 1;
    if (cur_cursor >= list->length) cur_cursor = 0;

    list->cursor = cur_cursor;

    if (cur_cursor != old_cursor)
    {
        gui_draw_notice(" ", C_BLACK);
        gui_draw_list(tab);
        gui_event(TAB_SCROLL, tab);
    }
}

void gui_redraw()
{
    tab_t *tab = gui_get_current_tab();
    gui_draw_header(tab);
    gui_draw_status(tab);
    gui_draw_list(tab);
    gui_event(TAB_REDRAW, tab);
}

void gui_draw_navbar()
{
    for (int i = 0; i < gui.tabcount; i++)
    {
        rg_display_write(i * IMAGE_LOGO_WIDTH, 0, IMAGE_LOGO_WIDTH, IMAGE_LOGO_HEIGHT, 0,
            gui.tabs[i]->img_logo->data);
    }
}

void gui_draw_header(tab_t *tab)
{
    int x_pos = IMAGE_LOGO_WIDTH;
    int y_pos = IMAGE_LOGO_HEIGHT;

    rg_gui_draw_fill_rect(x_pos, 0, RG_SCREEN_WIDTH - x_pos, LIST_Y_OFFSET, C_BLACK);
    rg_gui_draw_fill_rect(0, y_pos, RG_SCREEN_WIDTH, LIST_Y_OFFSET - y_pos, C_BLACK);

    if (tab->img_logo)
        rg_gui_draw_image(0, 0, IMAGE_LOGO_WIDTH, IMAGE_LOGO_HEIGHT, tab->img_logo);

    if (tab->img_header)
        rg_gui_draw_image(x_pos + 1, 0, IMAGE_BANNER_WIDTH, IMAGE_BANNER_HEIGHT, tab->img_header);
}

// void gui_draw_notice(tab_t *tab)
void gui_draw_notice(const char *text, uint16_t color)
{
    rg_gui_draw_text(CRC_X_OFFSET, CRC_Y_OFFSET, CRC_WIDTH, text, color, C_BLACK);
}

void gui_draw_status(tab_t *tab)
{
    rg_gui_draw_battery(RG_SCREEN_WIDTH - 27, 3);
    rg_gui_draw_text(
        IMAGE_LOGO_WIDTH + 11,
        IMAGE_BANNER_HEIGHT + 3,
        128,
        tab->status,
        C_WHITE,
        C_BLACK
    );
}

void gui_draw_list(tab_t *tab)
{
    const theme_t *theme = &gui_themes[gui.theme % gui_themes_count];
    const listbox_t *list = &tab->listbox;

    int columns = LIST_WIDTH / rg_gui_get_font_info().width;
    int line_height = LIST_LINE_HEIGHT;
    int lines = LIST_LINE_COUNT;

    for (int i = 0; i < lines; i++) {
        int entry = list->cursor + i - (lines / 2);

        if (entry >= 0 && entry < list->length) {
            sprintf(str_buffer, "%.*s", columns, list->items[entry].text);
        } else {
            str_buffer[0] = '\0';
        }

        rg_gui_draw_text(
            LIST_X_OFFSET,
            LIST_Y_OFFSET + i * line_height,
            LIST_WIDTH,
            str_buffer,
            (entry == list->cursor) ? theme->list_selected : theme->list_standard,
            (int)(16.f / lines * i) << theme->list_background
        );
    }
}

void gui_draw_preview(retro_emulator_file_t *file)
{
    retro_emulator_t *emu = (retro_emulator_t *)file->emulator;

    if (file->checksum > 0 && file->missing_cover != gui.show_preview)
    {
        uint16_t modes[] = {0x0000, 0x0312, 0x0123, 0x0012, 0x0003};
        uint16_t order = modes[gui.show_preview % 5];
        char path[256], buf_crc[10];
        rg_image_t *img = NULL;

        sprintf(buf_crc, "%08X", file->checksum);

        while (order && !img)
        {
            switch (order & 0xF)
            {
                case 0x1:
                    sprintf(path, "%s/%s/%c/%s.art", RG_BASE_PATH_ROMART, emu->dirname, buf_crc[0], buf_crc);
                    img = rg_gui_load_image_file(path);
                    break;
                case 0x2:
                    sprintf(path, "%s/%s/%c/%s.png", RG_BASE_PATH_ROMART, emu->dirname, buf_crc[0], buf_crc);
                    img = rg_gui_load_image_file(path);
                    break;
                case 0x3:
                    sprintf(path, "%s/%s/%s.%s.png", RG_BASE_PATH_SAVES, emu->dirname, file->name, file->ext);
                    img = rg_gui_load_image_file(path);
                    break;
            }

            order >>= 4;
        }

        if (img)
        {
            int height = MIN(img->height, COVER_MAX_HEIGHT);
            int width = MIN(img->width, COVER_MAX_WIDTH);

            if (img->height > COVER_MAX_HEIGHT || img->width > COVER_MAX_WIDTH)
                gui_draw_notice("Art too large", C_ORANGE);

            rg_gui_draw_image(320 - width, 240 - height, width, height, img);
            rg_gui_free_image(img);
            file->missing_cover = 0;
            return;
        }
    }

    // In case we change show_preview we want missing_cover to be invalidated
    file->missing_cover = gui.show_preview;

    gui_draw_notice(" No art found", C_RED);
}
