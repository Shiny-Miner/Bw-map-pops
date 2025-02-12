#include "global.h"
#include "strings.h"
#include "constants/songs.h"
#include "constants/weather.h"
#include "constants/rgb.h"
#include "util.h"
#include "event_object_movement.h"
#include "field_weather.h"
#include "main.h"
#include "menu.h"
#include "palette.h"
#include "random.h"
#include "script.h"
#include "start_menu.h"
#include "sound.h"
#include "sprite.h"
#include "task.h"
#include "trig.h"
#include "gpu_regs.h"
#include "field_camera.h"
#include "rtc.h"
#include "string_util.h"
#include "text.h"
#include "map_name_popup.h"
#include "config/overworld.h"

#if OW_POPUP_GENERATION == GEN_5
static EWRAM_DATA u8 sSecondaryPopupWindowId = 0;
#endif

static EWRAM_DATA u8 sStartMenuWindowId = 0;
static EWRAM_DATA u8 sMapNamePopupWindowId = 0;
static u16 AddWindowParameterized(u8, u8, u8, u8, u8, u8, u16);

const u8 gText_AM[] = _("AM");
const u8 gText_PM[] = _("PM");
const u8 gText_Space[] = _(" ");

static const struct WindowTemplate sStandardTextBox_WindowTemplates[] =
{
    {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 15,
        .width = 27,
        .height = 4,
        .paletteNum = 15,
        .baseBlock = 0x194
    },
    DUMMY_WIN_TEMPLATE
};

bool8 IsWeatherAlphaBlend(void)
{
    return (gWeatherPtr->currWeather == WEATHER_FOG_HORIZONTAL) || 
           (gWeatherPtr->currWeather == WEATHER_FOG_DIAGONAL)   ||
           (gWeatherPtr->currWeather == WEATHER_UNDERWATER_BUBBLES) ||
           (gWeatherPtr->currWeather == WEATHER_UNDERWATER);
};
void FormatDecimalTimeWithoutSeconds(u8 *txtPtr, s8 hour, s8 minute, bool8 is24Hour)
{
    if (is24Hour)
    {
        txtPtr = ConvertIntToDecimalStringN(txtPtr, hour, STR_CONV_MODE_LEADING_ZEROS, 2);
        *txtPtr++ = CHAR_COLON;
        txtPtr = ConvertIntToDecimalStringN(txtPtr, minute, STR_CONV_MODE_LEADING_ZEROS, 2);
    }
    else
    {
        if (hour == 0)
            txtPtr = ConvertIntToDecimalStringN(txtPtr, 12, STR_CONV_MODE_LEADING_ZEROS, 2);
        else if (hour < 13)
            txtPtr = ConvertIntToDecimalStringN(txtPtr, hour, STR_CONV_MODE_LEADING_ZEROS, 2);
        else
            txtPtr = ConvertIntToDecimalStringN(txtPtr, hour - 12, STR_CONV_MODE_LEADING_ZEROS, 2);
        *txtPtr++ = CHAR_COLON;
        txtPtr = ConvertIntToDecimalStringN(txtPtr, minute, STR_CONV_MODE_LEADING_ZEROS, 2);
        txtPtr = StringAppend(txtPtr, gText_Space);
        if (hour < 12)
            txtPtr = StringAppend(txtPtr, gText_AM);
        else
            txtPtr = StringAppend(txtPtr, gText_PM);
    }
    *txtPtr++ = EOS;
    *txtPtr = EOS;
};

void InitStandardTextBoxWindows(void)
{
    InitWindows(sStandardTextBox_WindowTemplates);
    sStartMenuWindowId = WINDOW_NONE;
    sMapNamePopupWindowId = WINDOW_NONE;
#if OW_POPUP_GENERATION == GEN_5
    sSecondaryPopupWindowId = WINDOW_NONE;
#endif
}

u8 AddMapNamePopUpWindow(void)
{
    if (sMapNamePopupWindowId == WINDOW_NONE)
    {
    #if OW_POPUP_GENERATION == GEN_5
        sMapNamePopupWindowId = AddWindowParameterized(0, 0, 0, 30, 3, 14, 0x107);
    #else
        sMapNamePopupWindowId = AddWindowParameterized(0, 1, 1, 10, 3, 14, 0x107);
    #endif
    }
    return sMapNamePopupWindowId;
}

// BW map pop-ups
#if OW_POPUP_GENERATION == GEN_5
u8 AddSecondaryPopUpWindow(void)
{
    if (sSecondaryPopupWindowId == WINDOW_NONE)
        sSecondaryPopupWindowId = AddWindowParameterized(0, 0, 17, 30, 3, 14, 0x161);
    return sSecondaryPopupWindowId;
}
u8 GetSecondaryPopUpWindowId(void)
{
    return sSecondaryPopupWindowId;
}
void RemoveSecondaryPopUpWindow(void)
{
    if (sSecondaryPopupWindowId != WINDOW_NONE)
    {
        RemoveWindow(sSecondaryPopupWindowId);
        sSecondaryPopupWindowId = WINDOW_NONE;
    }
}
void HBlankCB_DoublePopupWindow(void)
{
    u16 offset = gTasks[gPopupTaskId].data[2];
    u16 scanline = REG_VCOUNT;
    if (scanline < 80 || scanline > 160)
    {
        REG_BG0VOFS = offset;
        if(OW_POPUP_BW_ALPHA_BLEND && !IsWeatherAlphaBlend())
            REG_BLDALPHA = BLDALPHA_BLEND(15, 5);
    }
    else
    {
        REG_BG0VOFS = 512 - offset;
    }
}
#endif
