/**
 * @file searchSatScr.cpp
 * @author Jordi Gauchía (jgauchia@jgauchia.com)
 * @brief  LVGL - GPS satellite search screen with debug log
 * @version 0.2.4
 * @date 2025-12
 */

#include "searchSatScr.hpp"

extern Gps gps;                              /**< Global GPS instance */

static unsigned long millisActual = 0;        /**< Stores the current timestamp in milliseconds */
static bool skipSearch = false;               /**< Flag to indicate if satellite search should be skipped */
bool isSearchingSat = true;                   /**< Flag to indicate if satellite search is in progress */
extern uint8_t activeTile;                    /**< Index of the currently active tile */
lv_timer_t *mainTimer;                        /**< Main Screen Timer */
static lv_obj_t *gpsDebugLog = nullptr;       /**< GPS debug log text area */

static uint8_t lastSatInView = 0;             /**< Last satellite-in-view count logged */
static uint8_t lastSatInUse = 0;              /**< Last satellite-in-use count logged */
static uint8_t lastFixMode = 255;             /**< Last fix mode logged */
static bool fixAcquiredPending = false;       /**< State machine: waiting after fix acquired */

/**
 * @brief Add a line to the GPS debug log text area (LVGL thread only)
 *
 * @param msg The message to append
 */
static void gpsDebugAddLine(const char* msg)
{
    if (!gpsDebugLog)
        return;
    const char* current = lv_textarea_get_text(gpsDebugLog);
    if (current && strlen(current) > 0)
        lv_textarea_add_text(gpsDebugLog, "\n");
    lv_textarea_add_text(gpsDebugLog, msg);
    lv_textarea_set_cursor_pos(gpsDebugLog, LV_TEXTAREA_CURSOR_LAST);
}

/**
 * @brief Drain pending gpsLog() messages from the ring buffer into the textarea.
 *        Must be called from the LVGL thread (timer callback).
 */
static void drainGpsLogBuffer()
{
    uint16_t rp = gpsLogReadPos;
    uint16_t wp = gpsLogWritePos;
    if (rp >= wp)
        return;

    // Process line by line from the buffer
    while (rp < wp)
    {
        // Find end of this line
        uint16_t lineStart = rp;
        while (rp < wp && gpsLogBuffer[rp] != '\n')
            rp++;

        if (rp <= wp)
        {
            // Temporarily null-terminate for display
            char saved = gpsLogBuffer[rp];
            gpsLogBuffer[rp] = '\0';
            gpsDebugAddLine(gpsLogBuffer + lineStart);
            gpsLogBuffer[rp] = saved;
            if (rp < wp)
                rp++; // skip the newline
        }
    }
    gpsLogReadPos = rp;
}

/**
 * @brief Button events
 *
 * @details Handles button events for the search screen.
 *
 * @param event LVGL event pointer.
 */
void buttonEvent(lv_event_t *event)
{
    char *option = (char *)lv_event_get_user_data(event);
    if (strcmp(option,"skip") == 0)
        skipSearch = true;
    if (strcmp(option,"settings") == 0)
        lv_screen_load(settingsScreen);
    lv_timer_resume(mainTimer);
}

/**
 * @brief Search valid GPS signal
 *
 * @details Checks for a valid GPS fix or a skip command.
 *          Drains the GPS log buffer and polls GPS status changes.
 *
 * @param searchTimer LVGL timer pointer associated with the satellite search.
 */
void searchGPS(lv_timer_t *searchTimer)
{
    // Drain any pending log messages from gpsLog() (thread-safe)
    drainGpsLogBuffer();

    // State machine: waiting 500ms after fix acquired before switching screen
    if (fixAcquiredPending)
    {
        if (millis() >= millisActual + 500)
        {
            lv_timer_del(searchTimer);
            lv_timer_resume(mainTimer);
            isSearchingSat = false;
            loadMainScreen();
        }
        return;
    }

    // Poll satellite-in-view count changes
    uint8_t curSatInView = gps.gpsData.satInView;
    if (curSatInView != lastSatInView)
    {
        lastSatInView = curSatInView;
        char buf[32];
        snprintf(buf, sizeof(buf), "Sats in view: %d", lastSatInView);
        gpsDebugAddLine(buf);
    }

    // Poll satellite-in-use count changes
    uint8_t curSatInUse = gps.gpsData.satellites;
    if (curSatInUse != lastSatInUse)
    {
        lastSatInUse = curSatInUse;
        char buf[32];
        snprintf(buf, sizeof(buf), "Sats in use: %d", lastSatInUse);
        gpsDebugAddLine(buf);
    }

    // Poll fix mode changes
    uint8_t curFixMode = gps.gpsData.fixMode;
    if (curFixMode != lastFixMode)
    {
        lastFixMode = curFixMode;
        const char* fixStr;
        switch (lastFixMode)
        {
            case 0:  fixStr = "NONE"; break;
            case 1:  fixStr = "2D";   break;
            case 2:  fixStr = "3D";   break;
            case 3:  fixStr = "DGPS"; break;
            default: fixStr = "?";    break;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "Fix: %s", fixStr);
        gpsDebugAddLine(buf);
    }

    if (isGpsFixed)
    {
        gpsDebugAddLine("GPS Fix acquired!");
        fixAcquiredPending = true;
        millisActual = millis();
        return;
    }

    if (skipSearch)
    {
        lv_timer_del(searchTimer);
        isSearchingSat = false;
        zoom = defaultZoom;
        activeTile = 3;
        lv_tileview_set_tile_by_index(tilesScreen, 3, 0, LV_ANIM_OFF);
        loadMainScreen();
    }
}

/**
 * @brief Create Satellite Search Screen with GPS debug log
 *
 * @details Creates the satellite search screen with a scrollable debug text area
 */
void createSearchSatScr()
{
    // Reset state for clean re-entry
    skipSearch = false;
    lastSatInView = 0;
    lastSatInUse = 0;
    lastFixMode = 255;
    fixAcquiredPending = false;

    searchTimer = lv_timer_create(searchGPS, 100, NULL);
    lv_timer_ready(searchTimer);
    lv_timer_pause(mainTimer);

    searchSatScreen = lv_obj_create(NULL);

    // Title label
    lv_obj_t *label = lv_label_create(searchSatScreen);
    lv_obj_set_style_text_font(label, fontMedium, 0);
    lv_label_set_text(label, textSearch);
    lv_obj_set_align(label, LV_ALIGN_TOP_MID);
    lv_obj_set_y(label, 5);

    // Debug log text area (terminal style: green on black)
    gpsDebugLog = lv_textarea_create(searchSatScreen);
    lv_textarea_set_text(gpsDebugLog, "");
    lv_textarea_set_max_length(gpsDebugLog, 2048);
    lv_obj_set_size(gpsDebugLog, TFT_WIDTH - 20, (int)(TFT_HEIGHT * 0.45));
    lv_obj_align(gpsDebugLog, LV_ALIGN_TOP_MID, 0, 25 * scale);
    lv_textarea_set_cursor_click_pos(gpsDebugLog, false);
    lv_obj_clear_flag(gpsDebugLog, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_text_color(gpsDebugLog, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_color(gpsDebugLog, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gpsDebugLog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(gpsDebugLog, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(gpsDebugLog, fontSmall, 0);
    // Hide the blinking cursor
    lv_obj_set_style_opa(gpsDebugLog, LV_OPA_0, LV_PART_CURSOR);

    // Flush any buffered init logs into the text area
    drainGpsLogBuffer();

    // Spinner (smaller, below text area)
    lv_obj_t *spinner = lv_spinner_create(searchSatScreen);
    lv_obj_set_size(spinner, (int)(80 * scale), (int)(80 * scale));
    lv_spinner_set_anim_params(spinner, 2000, 200);
    lv_obj_align(spinner, LV_ALIGN_BOTTOM_MID, 0, (int)(-85 * scaleBut));

    // Satellite icon centered on spinner
    lv_obj_t *satImg = lv_img_create(searchSatScreen);
    lv_img_set_src(satImg, satIconFile);
    lv_obj_align_to(satImg, spinner, LV_ALIGN_CENTER, 0, 0);

    // Button Bar
    lv_obj_t *btnBar = lv_obj_create(searchSatScreen);
    lv_obj_set_size(btnBar, TFT_WIDTH, (int)(68 * scaleBut));
    lv_obj_set_pos(btnBar, 0, (int)(TFT_HEIGHT - 80 * scaleBut));
    lv_obj_set_flex_flow(btnBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btnBar, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btnBar, LV_OBJ_FLAG_SCROLLABLE);
    static lv_style_t styleBar;
    lv_style_init(&styleBar);
    lv_style_set_bg_opa(&styleBar, LV_OPA_0);
    lv_style_set_border_opa(&styleBar, LV_OPA_0);
    lv_obj_add_style(btnBar, &styleBar, LV_PART_MAIN);

    lv_obj_t *imgBtn;

    // Settings Button
    imgBtn = lv_img_create(btnBar);
    lv_img_set_src(imgBtn, confIconFile);
    lv_obj_add_flag(imgBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_zoom(imgBtn,buttonScale);
    lv_obj_update_layout(imgBtn);
    lv_obj_set_style_size(imgBtn,48 * scaleBut, 48 * scaleBut, 0);
    lv_obj_add_event_cb(imgBtn, buttonEvent, LV_EVENT_PRESSED, (char*)"settings");

    // Skip Button
    imgBtn = lv_img_create(btnBar);
    lv_img_set_src(imgBtn, skipIconFile);
    lv_obj_add_flag(imgBtn, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_zoom(imgBtn,buttonScale);
    lv_obj_update_layout(imgBtn);
    lv_obj_set_style_size(imgBtn,48 * scaleBut, 48 * scaleBut, 0);
    lv_obj_add_event_cb(imgBtn, buttonEvent, LV_EVENT_PRESSED, (char*)"skip");
}
