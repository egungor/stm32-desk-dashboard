/**
 * @file    link_port.c
 * @brief   Connects the protocol clients to the USB HID interface and the UI.
 *
 * Two clients share the link (ADR-006): link_client.c for the audio
 * commands and dash_client.c for the dashboard. Each received frame is
 * decoded once here and given to the client it belongs to (Dash_Owns);
 * frames the audio client must answer as invalid go to it undecoded.
 * Both clients number their frames from the audio client's counter
 * (Dash_SetSequenceCounter): one sequence counter for the board.
 */
#include "link_port.h"

#include <stdbool.h>
#include <string.h>

#include "dash_client.h"
#include "link_client.h"
#include "log.h"
#include "stm32f7xx_hal.h"
#include "ui.h"
#include "ui_port.h"
#include "usb_link.h"

#define SEND_TIMEOUT_MS  20U
#define MAX_FRAMES_PER_RUN 8U /* bounds the time spent per UI loop */

static LinkClient s_link;
static DashClient s_dash;
static bool s_usb_configured;
static bool s_vbus;
static uint32_t s_config_count;

static bool cb_send_frame(const uint8_t frame[PROTO_FRAME_SIZE], void *ctx)
{
    (void)ctx;
    return UsbLink_Send(frame, SEND_TIMEOUT_MS);
}

/* ------------------------------------------------------------- dashboard */

static void cb_dash_card(uint8_t slot, const char *text, void *ctx)
{
    (void)ctx;
    Log_Line("DASH CARD slot=%u bytes=%u", (unsigned)slot, (unsigned)strlen(text));
    Ui_SetCard(slot, text);
}

static void cb_dash_detail(uint8_t slot, const char *text, void *ctx)
{
    (void)ctx;
    Log_Line("DASH DETAIL slot=%u bytes=%u", (unsigned)slot, (unsigned)strlen(text));
    Ui_ShowDetail(slot, text);
}

static void cb_dash_notice(DashNotice notice, void *ctx)
{
    (void)ctx;
    if (notice == DASH_NOTICE_DETAIL_FAILED)
    {
        Log_Line("DASH DETAIL FAIL");
        Ui_DetailFailed();
    }
    else
    {
        Log_Line("DASH ACTION FAIL");
        Ui_ShowNotice(UI_NOTICE_ACTION_FAILED);
    }
}

static void cb_dash_time(uint32_t local_time, void *ctx)
{
    (void)ctx;
    Ui_SetTime(local_time);
}

static void on_ui_open_detail(uint8_t slot, int8_t arg, void *user)
{
    (void)user;
    Log_Line("UI DETAIL slot=%u arg=%d", (unsigned)slot, (int)arg);
    if (!Dash_OpenDetail(&s_dash, slot, arg, HAL_GetTick()))
    {
        Ui_DetailFailed();
    }
}

static void on_ui_close_detail(void *user)
{
    (void)user;
    Dash_CloseDetail(&s_dash);
}

static void on_ui_detail_action(uint8_t slot, uint8_t action, void *user)
{
    (void)user;
    Log_Line("UI ACTION slot=%u action=%u", (unsigned)slot, (unsigned)action);
    if (!Dash_Action(&s_dash, PROTO_ACTION_CARD, slot, action))
    {
        Ui_ShowNotice(UI_NOTICE_ACTION_FAILED);
    }
}

static void on_ui_media(uint8_t action, void *user)
{
    (void)user;
    Log_Line("UI MEDIA action=%u", (unsigned)action);
    if (!Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, action))
    {
        Ui_ShowNotice(UI_NOTICE_ACTION_FAILED);
    }
}

/* --------------------------------------------------------------- audio */

static void cb_show_waiting(void *ctx)
{
    (void)ctx;
    Ui_ShowWaiting();
}

static void cb_show_devices(const UiDevice *devices, size_t count, void *ctx)
{
    (void)ctx;
    Log_Line("LINK LIST OK n=%u gen=%u", (unsigned)count, (unsigned)s_link.generation);
    Ui_SetDevices(devices, count);
}

static void cb_set_active(uint8_t device_id, void *ctx)
{
    (void)ctx;
    Ui_SetActive(device_id);
}

static void cb_set_pending(uint8_t device_id, void *ctx)
{
    (void)ctx;
    Ui_SetPending(device_id);
}

static void cb_notice(LinkNotice notice, void *ctx)
{
    (void)ctx;
    if (notice == LINK_NOTICE_SELECT_FAILED)
    {
        Log_Line("LINK SELECT FAIL reason=host_error");
        Ui_ShowNotice(UI_NOTICE_SELECT_FAILED);
    }
    else if (notice == LINK_NOTICE_EDIT_FAILED)
    {
        Log_Line("LINK EDIT FAIL reason=host_error");
        Ui_ShowNotice(UI_NOTICE_EDIT_FAILED);
    }
    else
    {
        Log_Line("LINK SELECT FAIL reason=timeout");
        Ui_ShowNotice(UI_NOTICE_HOST_TIMEOUT);
    }
}

/* The protocol's settings record and the UI's have the same fields and
 * values (ui.h); the UI does not depend on protocol.h. */
static void to_ui(const ProtoSettings *p, UiSettings *u)
{
    u->layout = p->layout;
    u->theme = p->theme;
    u->accent = p->accent;
    u->grid = p->grid;
    u->startup_tab = p->startup_tab;
    u->last_tab = p->last_tab;
    u->screen_timeout = p->screen_timeout;
    u->flipped = p->flipped;
    u->language = p->language;
    u->volume_limit_on = p->volume_limit_on;
    u->volume_limit = p->volume_limit;
    u->startup_screen = p->startup_screen;
}

static void to_proto(const UiSettings *u, ProtoSettings *p)
{
    p->layout = u->layout;
    p->theme = u->theme;
    p->accent = u->accent;
    p->grid = u->grid;
    p->startup_tab = u->startup_tab;
    p->last_tab = u->last_tab;
    p->screen_timeout = u->screen_timeout;
    p->flipped = u->flipped;
    p->language = u->language;
    p->volume_limit_on = u->volume_limit_on;
    p->volume_limit = u->volume_limit;
    p->startup_screen = u->startup_screen;
}

/* Settings from the host or a local change: the UI applies its fields,
 * the display port the flip. */
static void cb_apply_settings(const ProtoSettings *s, void *ctx)
{
    UiSettings u;

    (void)ctx;
    Log_Line("LINK SETTINGS layout=%u theme=%u accent=%u grid=%u tab=%u/%u lang=%u flipped=%u limit=%u/%u",
             (unsigned)s->layout, (unsigned)s->theme, (unsigned)s->accent, (unsigned)s->grid,
             (unsigned)s->startup_tab, (unsigned)s->last_tab, (unsigned)s->language, (unsigned)s->flipped,
             (unsigned)s->volume_limit_on, (unsigned)s->volume_limit);
    to_ui(s, &u);
    Ui_ApplySettings(&u);
    UiPort_SetFlipped(s->flipped != 0U);
}

/* The user changed a setting in the UI (e.g. the tab). */
static void on_ui_settings(const UiSettings *settings, void *user)
{
    ProtoSettings p;

    (void)user;
    to_proto(settings, &p);
    Link_SetSettings(&s_link, &p, HAL_GetTick());
}

static void cb_set_volume(uint8_t volume, void *ctx)
{
    (void)ctx;
    Ui_SetVolume(volume);
}

static void cb_set_muted(uint8_t muted, void *ctx)
{
    (void)ctx;
    if (muted != LINK_VOLUME_UNKNOWN)
    {
        Log_Line("LINK MUTE muted=%u", (unsigned)muted);
    }
    Ui_SetMuted(muted);
}

static void on_ui_volume(uint8_t volume, bool released, void *user)
{
    (void)user;
    if (released)
    {
        Log_Line("UI VOLUME value=%u", (unsigned)volume);
    }
    Link_SetVolume(&s_link, volume, HAL_GetTick());
}

static void on_ui_mute(bool muted, void *user)
{
    (void)user;
    Log_Line("UI MUTE %s", muted ? "on" : "off");
    Link_SetMute(&s_link, muted);
}

/* Edit mode: the host stores the edit and sends a new list. */
static bool on_ui_favorite(uint8_t device_id, bool favorite, void *user)
{
    const bool sent = Link_SetFavorite(&s_link, device_id, favorite, HAL_GetTick());

    (void)user;
    Log_Line("UI EDIT favorite id=%u on=%u sent=%u", (unsigned)device_id, favorite ? 1U : 0U, sent ? 1U : 0U);
    return sent;
}

static bool on_ui_alias(uint8_t device_id, const char *alias, void *user)
{
    const bool sent = Link_SetAlias(&s_link, device_id, alias, HAL_GetTick());

    (void)user;
    Log_Line("UI EDIT alias id=%u bytes=%u sent=%u", (unsigned)device_id, (unsigned)strlen(alias),
             sent ? 1U : 0U);
    return sent;
}

static bool on_ui_move(uint8_t device_id, uint8_t new_position, void *user)
{
    const bool sent = Link_MoveDevice(&s_link, device_id, new_position, HAL_GetTick());

    (void)user;
    Log_Line("UI EDIT move id=%u to=%u sent=%u", (unsigned)device_id, (unsigned)new_position, sent ? 1U : 0U);
    return sent;
}

static void on_ui_select(uint8_t device_id, void *user)
{
    (void)user;
    Log_Line("UI SELECT id=%u", (unsigned)device_id);
    Link_Select(&s_link, device_id, HAL_GetTick());
}

void LinkPort_Init(void)
{
    static const LinkCallbacks callbacks = {
        cb_send_frame, cb_show_waiting, cb_show_devices, cb_set_active, cb_set_pending, cb_notice,
        cb_set_volume, cb_set_muted, NULL, cb_apply_settings,
    };

    Ui_Init(on_ui_select, NULL);
    Ui_SetAudioHandlers(on_ui_volume, on_ui_mute, NULL);
    Ui_SetSettingsHandler(on_ui_settings, NULL);
    {
        static const UiEditHandlers edit = { on_ui_favorite, on_ui_alias, on_ui_move };

        Ui_SetEditHandlers(&edit, NULL);
    }
    Link_Init(&s_link, &callbacks);
    {
        static const DashCallbacks dash = { cb_send_frame, cb_dash_card, cb_dash_detail, cb_dash_notice,
                                            cb_dash_time, NULL };
        static const UiDashHandlers ui_dash = { on_ui_open_detail, on_ui_close_detail, on_ui_detail_action,
                                                on_ui_media };

        Dash_Init(&s_dash, &dash);
        Dash_SetSequenceCounter(&s_dash, &s_link.sequence);
        Ui_SetDashHandlers(&ui_dash, NULL);
    }
}

void LinkPort_Run(void)
{
    const uint32_t now = HAL_GetTick();
    uint8_t frame[PROTO_FRAME_SIZE];
    bool vbus;
    bool configured;

    UsbLink_Poll();
    vbus = UsbLink_VbusPresent();
    configured = UsbLink_IsConfigured();

    if (vbus != s_vbus)
    {
        s_vbus = vbus;
        Log_Line("USB VBUS %s", vbus ? "ON" : "OFF");
    }
    if (configured != s_usb_configured)
    {
        s_usb_configured = configured;
        if (configured)
        {
            const uint32_t config_count = UsbLink_ConfigCount();

            /* No new SET_CONFIGURATION: the host resumed a suspended device. */
            Log_Line("USB %s", (config_count != s_config_count) ? "CONFIGURED" : "RESUMED");
            s_config_count = config_count;
            Link_OnConnected(&s_link, now);
            Dash_OnConnected(&s_dash, now);
        }
        else
        {
            Log_Line("USB DISCONNECTED");
            Link_OnDisconnected(&s_link);
            Dash_OnDisconnected(&s_dash);
        }
    }

    for (uint32_t i = 0U; (i < MAX_FRAMES_PER_RUN) && UsbLink_Receive(frame, 0U); i++)
    {
        const uint8_t active_before = s_link.active;
        ProtoMessage msg;

        if ((Proto_Decode(frame, &msg) == PROTO_OK) && Dash_Owns(&msg))
        {
            Dash_OnMessage(&s_dash, &msg, now);
            continue;
        }
        Link_OnFrame(&s_link, frame, now);
        if (s_link.active != active_before)
        {
            Log_Line("LINK ACTIVE id=%u", (unsigned)s_link.active);
        }
    }
    Link_Poll(&s_link, now);
    Dash_Poll(&s_dash, now);
}

void LinkPort_ToggleFlip(void)
{
    ProtoSettings s = *Link_Settings(&s_link);

    s.flipped = (s.flipped != 0U) ? 0U : 1U;
    Link_SetSettings(&s_link, &s, HAL_GetTick());
}
