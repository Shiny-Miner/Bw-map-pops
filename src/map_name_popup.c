// BW-style map name pop-up for Pokemon FireRed (ROM hack build).
//
// Replaces the ROM's own ShowMapNamePopup / DismissMapNamePopup /
// IsMapNamePopupTaskActive. See mainBPRE.asm for the hooks and BPRE.ld for
// the ROM symbols / fixed RAM addresses used here.
#include "global.h"
#include "config/overworld.h"
#include "bg.h"
#include "event_data.h"
#include "field_weather.h"
#include "gpu_regs.h"
#include "main.h"
#include "map_name_popup.h"
#include "menu.h"
#include "new_menu_helpers.h"
#include "palette.h"
#include "region_map.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "window.h"
#include "characters.h"
#include "constants/region_map_sections.h"
#include "constants/weather.h"

#if OW_POPUP_GENERATION != GEN_5
#error "Only OW_POPUP_GENERATION == GEN_5 is implemented. For the vanilla FR popup, do not build/apply this patch."
#endif

#if OW_POPUP_BW_TIME_MODE != OW_POPUP_BW_TIME_NONE
#error "FireRed has no RTC code, so the BW pop-up clock is not available. Set OW_POPUP_BW_TIME_MODE to OW_POPUP_BW_TIME_NONE."
#endif

// Gen5 assets (built by the Makefile from graphics/png_to_4bpp and graphics/pal_to_gbapal)
static const u8 sMapPopUpTilesPrimary_BW[] = INCBIN_U8("graphics/png_to_4bpp/bw_primary.4bpp");
static const u8 sMapPopUpTilesSecondary_BW[] = INCBIN_U8("graphics/png_to_4bpp/bw_secondary.4bpp");
static const u16 sMapPopUpTilesPalette_BW_Black[16] = INCBIN_U16("graphics/pal_to_gbapal/black.gbapal");
static const u16 sMapPopUpTilesPalette_BW_White[16] = INCBIN_U16("graphics/pal_to_gbapal/white.gbapal");

// Text color indices inside the pop-up palettes: {background, foreground, shadow}
static const u8 sBwTextColors[3] = {0, 2, 3};

enum MapPopUp_Themes_BW
{
    MAPPOPUP_THEME_BW_DEFAULT,
};

// States and data defines for Task_MapNamePopUpWindow
enum {
    STATE_SLIDE_IN,
    STATE_WAIT,
    STATE_SLIDE_OUT,
    STATE_UNUSED,
    STATE_ERASE,
    STATE_END,
    STATE_PRINT, // For some reason the first state is numerically last.
};

#define POPUP_OFFSCREEN_Y  24
#define POPUP_SLIDE_SPEED  2

#define tState         data[0]
#define tOnscreenTimer data[1]
#define tYOffset       data[2] // read by HBlankCB_DoublePopupWindow
#define tIncomingPopUp data[3]
#define tPrintTimer    data[4]
#define tWindowId      data[5]
#define tWindow2Id     data[6]

// gPopupTaskId (menu.h) is a free EWRAM byte whose address is defined in BPRE.ld;
// normal EWRAM_DATA/.bss variables can't be used in this build.

static void Task_MapNamePopUpWindow(u8 taskId);
static void ShowMapNamePopUpWindow(u8 taskId, bool32 palIntoFadedBuffer);
static void HideMapNamePopUpWindow(u8 taskId);

bool8 IsWeatherAlphaBlend(void)
{
    return (gWeatherPtr->currWeather == WEATHER_FOG_HORIZONTAL)
        || (gWeatherPtr->currWeather == WEATHER_FOG_DIAGONAL)
        || (gWeatherPtr->currWeather == WEATHER_UNDERWATER_BUBBLES)
        || (gWeatherPtr->currWeather == WEATHER_UNDERWATER);
}

static u8 AddPopUpWindow(u8 tilemapTop, u16 baseBlock)
{
    struct WindowTemplate template =
    {
        .bg = 0,
        .tilemapLeft = 0,
        .tilemapTop = tilemapTop,
        .width = 30,
        .height = 3,
        .paletteNum = 14,
        .baseBlock = baseBlock,
    };
    return AddWindow(&template);
}

static void HBlankCB_DoublePopupWindow(void)
{
    u16 offset = gTasks[gPopupTaskId].tYOffset;
    u16 scanline = REG_VCOUNT;

    if (scanline < 80 || scanline > 160)
    {
        REG_BG0VOFS = offset;
        if (OW_POPUP_BW_ALPHA_BLEND && !IsWeatherAlphaBlend())
            REG_BLDALPHA = BLDALPHA_BLEND(15, 5);
    }
    else
    {
        REG_BG0VOFS = 512 - offset;
    }
}

// Same logic as the ROM's floor suffix ("B1F", "2F" ...).
static u8 *AppendFloorNum(u8 *dest, s8 floorNum)
{
    if (floorNum == 0 || floorNum == 127)
        return dest;
    *dest++ = CHAR_SPACE;
    if (floorNum < 0)
    {
        *dest++ = CHAR_B;
        floorNum = -floorNum;
    }
    dest = ConvertIntToDecimalStringN(dest, floorNum, STR_CONV_MODE_LEFT_ALIGN, 2);
    *dest++ = CHAR_F;
    *dest = EOS;
    return dest;
}

void ShowMapNamePopup(bool32 palIntoFadedBuffer)
{
    u8 taskId;

    if (FlagGet(FLAG_DONT_SHOW_MAP_NAME_POPUP) == TRUE)
        return;

    if (!FuncIsActiveTask(Task_MapNamePopUpWindow))
    {
        // New pop up window
        taskId = CreateTask(Task_MapNamePopUpWindow, 100);
        gPopupTaskId = taskId;

        if (OW_POPUP_BW_ALPHA_BLEND && !IsWeatherAlphaBlend())
            SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG0 | BLDCNT_TGT2_ALL | BLDCNT_EFFECT_BLEND);

        gTasks[taskId].tState = STATE_PRINT;
        gTasks[taskId].tOnscreenTimer = 0;
        gTasks[taskId].tYOffset = POPUP_OFFSCREEN_Y;
        gTasks[taskId].tIncomingPopUp = FALSE;
        gTasks[taskId].tPrintTimer = 0;
        gTasks[taskId].tWindowId = WINDOW_NONE;
        gTasks[taskId].tWindow2Id = WINDOW_NONE;
    }
    else
    {
        // There's already a pop up window running.
        // Hurry the old pop up offscreen so the new one can appear.
        taskId = gPopupTaskId;
        if (gTasks[taskId].tState != STATE_SLIDE_OUT)
            gTasks[taskId].tState = STATE_SLIDE_OUT;
        gTasks[taskId].tIncomingPopUp = TRUE;
    }
    // Faded-buffer flag is only needed when the window is built; remember it.
    gTasks[taskId].data[7] = palIntoFadedBuffer;
}

void DismissMapNamePopup(void)
{
    u8 taskId = FindTaskIdByFunc(Task_MapNamePopUpWindow);

    if (taskId == TASK_NONE)
        return;
    if (gTasks[taskId].tState == STATE_PRINT)
        HideMapNamePopUpWindow(taskId);       // nothing on screen yet
    else if (gTasks[taskId].tState < STATE_SLIDE_OUT)
        gTasks[taskId].tState = STATE_SLIDE_OUT;
    gTasks[taskId].tIncomingPopUp = FALSE;
}

bool8 IsMapNamePopupTaskActive(void)
{
    return FuncIsActiveTask(Task_MapNamePopUpWindow);
}

static void Task_MapNamePopUpWindow(u8 taskId)
{
    struct Task *task = &gTasks[taskId];

    switch (task->tState)
    {
    case STATE_PRINT:
        // Wait, then create and print the pop up window
        if (++task->tPrintTimer > 30)
        {
            task->tState = STATE_SLIDE_IN;
            task->tPrintTimer = 0;
            ShowMapNamePopUpWindow(taskId, task->data[7]);
            EnableInterrupts(INTR_FLAG_HBLANK);
            SetHBlankCallback(HBlankCB_DoublePopupWindow);
        }
        break;
    case STATE_SLIDE_IN:
        task->tYOffset -= POPUP_SLIDE_SPEED;
        if (task->tYOffset <= 0)
        {
            task->tYOffset = 0;
            task->tState = STATE_WAIT;
            task->tOnscreenTimer = 0;
        }
        break;
    case STATE_WAIT:
        if (++task->tOnscreenTimer > 120)
        {
            task->tOnscreenTimer = 0;
            task->tState = STATE_SLIDE_OUT;
        }
        break;
    case STATE_SLIDE_OUT:
        task->tYOffset += POPUP_SLIDE_SPEED;
        if (task->tYOffset >= POPUP_OFFSCREEN_Y)
        {
            task->tYOffset = POPUP_OFFSCREEN_Y;
            if (task->tIncomingPopUp)
            {
                // A new pop up window is incoming, return to the first state to show it.
                task->tState = STATE_PRINT;
                task->tPrintTimer = 0;
                task->tIncomingPopUp = FALSE;
            }
            else
            {
                task->tState = STATE_ERASE;
            }
        }
        break;
    case STATE_ERASE:
        if (task->tWindowId != WINDOW_NONE)
            ClearStdWindowAndFrame(task->tWindowId, TRUE);
        if (task->tWindow2Id != WINDOW_NONE)
            ClearStdWindowAndFrame(task->tWindow2Id, TRUE);
        task->tState = STATE_END;
        break;
    case STATE_END:
        HideMapNamePopUpWindow(taskId);
        break;
    }
}

static void HideMapNamePopUpWindow(u8 taskId)
{
    struct Task *task = &gTasks[taskId];

    if (task->tWindowId != WINDOW_NONE)
    {
        ClearStdWindowAndFrame(task->tWindowId, TRUE);
        RemoveWindow(task->tWindowId);
        task->tWindowId = WINDOW_NONE;
    }
    if (task->tWindow2Id != WINDOW_NONE)
    {
        ClearStdWindowAndFrame(task->tWindow2Id, TRUE);
        RemoveWindow(task->tWindow2Id);
        task->tWindow2Id = WINDOW_NONE;
    }

    DisableInterrupts(INTR_FLAG_HBLANK);
    SetHBlankCallback(NULL);

    if (OW_POPUP_BW_ALPHA_BLEND && !IsWeatherAlphaBlend())
    {
        SetGpuReg(REG_OFFSET_WININ, WININ_WIN0_BG_ALL | WININ_WIN0_OBJ | WININ_WIN1_BG_ALL | WININ_WIN1_OBJ);
        SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT2_BG1 | BLDCNT_TGT2_BG2 | BLDCNT_TGT2_BG3 | BLDCNT_TGT2_OBJ | BLDCNT_EFFECT_BLEND);
        SetGpuReg(REG_OFFSET_BLDALPHA, BLDALPHA_BLEND(8, 10));
    }

    SetGpuReg(REG_OFFSET_BG0VOFS, 0);
    DestroyTask(taskId);
}

static void ShowMapNamePopUpWindow(u8 taskId, bool32 palIntoFadedBuffer)
{
    struct Task *task = &gTasks[taskId];
    u8 mapName[32];
    const u16 *palette;
    u8 windowId, window2Id;

    if (OW_POPUP_BW_ALPHA_BLEND && !IsWeatherAlphaBlend())
        SetGpuRegBits(REG_OFFSET_WININ, WININ_WIN0_CLR);

    // Reuse the windows if a previous pop up is being replaced.
    if (task->tWindowId == WINDOW_NONE)
        task->tWindowId = AddPopUpWindow(0, 0x107);
    if (task->tWindow2Id == WINDOW_NONE)
        task->tWindow2Id = AddPopUpWindow(17, 0x161);
    windowId = task->tWindowId;
    window2Id = task->tWindow2Id;

    // Background / frame (single BW theme for every map section)
    switch (MAPPOPUP_THEME_BW_DEFAULT)
    {
    default:
        palette = (OW_POPUP_BW_COLOR == OW_POPUP_BW_COLOR_WHITE) ? sMapPopUpTilesPalette_BW_White
                                                                  : sMapPopUpTilesPalette_BW_Black;
        break;
    }
    if (palIntoFadedBuffer)
        CpuCopy16(palette, &gPlttBufferFaded[BG_PLTT_ID(14)], PLTT_SIZE_4BPP);
    else
        LoadPalette(palette, BG_PLTT_ID(14), PLTT_SIZE_4BPP);

    CopyToWindowPixelBuffer(windowId, sMapPopUpTilesPrimary_BW, sizeof(sMapPopUpTilesPrimary_BW), 0);
    CopyToWindowPixelBuffer(window2Id, sMapPopUpTilesSecondary_BW, sizeof(sMapPopUpTilesSecondary_BW), 0);
    PutWindowTilemap(windowId);
    PutWindowTilemap(window2Id);

    // Map name
    AppendFloorNum(GetMapName(mapName, gMapHeader.regionMapSectionId, 0), gMapHeader.floorNum);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 8, 2, sBwTextColors, TEXT_SKIP_DRAW, mapName);

    CopyWindowToVram(windowId, COPYWIN_FULL);
    CopyWindowToVram(window2Id, COPYWIN_FULL);
}
