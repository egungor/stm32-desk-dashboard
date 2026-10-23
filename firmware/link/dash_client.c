/**
 * @file    dash_client.c
 * @brief   STM32 side of the dashboard commands of protocol v7 (see
 *          dash_client.h).
 */
#include "dash_client.h"

#include <string.h>

static bool send_message(DashClient *dash, ProtoMessage *msg)
{
    uint8_t frame[PROTO_FRAME_SIZE];

    msg->sequence = (*dash->sequence)++;
    if (Proto_Encode(msg, frame) != PROTO_OK)
    {
        return false;
    }
    return (dash->cb.send_frame != NULL) && dash->cb.send_frame(frame, dash->cb.ctx);
}

static void send_get_cards(DashClient *dash)
{
    ProtoMessage msg;

    memset(&msg, 0, sizeof(msg));
    msg.command = PROTO_CMD_GET_CARDS;
    (void)send_message(dash, &msg);
}

static void send_get_media(DashClient *dash)
{
    ProtoMessage msg;

    memset(&msg, 0, sizeof(msg));
    msg.command = PROTO_CMD_GET_MEDIA;
    (void)send_message(dash, &msg);
}

static bool send_open_detail(DashClient *dash, uint8_t slot, int8_t arg)
{
    ProtoMessage msg;

    memset(&msg, 0, sizeof(msg));
    msg.command = PROTO_CMD_OPEN_DETAIL;
    msg.u.open_detail.slot = slot;
    msg.u.open_detail.arg = arg;
    return send_message(dash, &msg);
}

static void notify(const DashClient *dash, DashNotice notice)
{
    if (dash->cb.notice != NULL)
    {
        dash->cb.notice(notice, dash->cb.ctx);
    }
}

static void detail_failed(DashClient *dash)
{
    dash->detail_waiting = false;
    dash->detail_slot = DASH_NO_SLOT;
    Proto_AssemblerReset(&dash->detail_asm);
    notify(dash, DASH_NOTICE_DETAIL_FAILED);
}

static void show_card(const DashClient *dash, uint8_t slot, const char *text)
{
    if (dash->cb.show_card != NULL)
    {
        dash->cb.show_card(slot, text, dash->cb.ctx);
    }
}

static void on_card_part(DashClient *dash, const ProtoRecordPart *part, uint32_t now_ms)
{
    if (Proto_AssemblerAdd(&dash->card_asm, part) == PROTO_ASM_COMPLETE)
    {
        show_card(dash, part->id, (const char *)dash->card_buf);
    }
    else
    {
        dash->card_part_ms = now_ms;
    }
}

static void on_detail_part(DashClient *dash, const ProtoRecordPart *part, uint32_t now_ms)
{
    if ((dash->detail_slot == DASH_NO_SLOT) || (part->id != dash->detail_slot))
    {
        dash->frames_ignored++;  /* the user left that detail */
        return;
    }
    if (Proto_AssemblerAdd(&dash->detail_asm, part) == PROTO_ASM_COMPLETE)
    {
        dash->detail_waiting = false;
        if (dash->cb.show_detail != NULL)
        {
            dash->cb.show_detail(part->id, (const char *)dash->detail_buf, dash->cb.ctx);
        }
    }
    else
    {
        dash->detail_part_ms = now_ms;
    }
}

static void close_prompt(DashClient *dash)
{
    if (dash->prompt_shown != DASH_NO_PROMPT)
    {
        const uint8_t id = (uint8_t)dash->prompt_shown;

        dash->prompt_shown = DASH_NO_PROMPT;
        if (dash->cb.close_prompt != NULL)
        {
            dash->cb.close_prompt(id, dash->cb.ctx);
        }
    }
}

static void on_prompt_part(DashClient *dash, const ProtoRecordPart *part, uint32_t now_ms)
{
    if (Proto_AssemblerAdd(&dash->prompt_asm, part) == PROTO_ASM_COMPLETE)
    {
        dash->prompt_shown = part->id;
        if (dash->cb.show_prompt != NULL)
        {
            dash->cb.show_prompt(part->id, (const char *)dash->prompt_buf, dash->cb.ctx);
        }
    }
    else
    {
        dash->prompt_part_ms = now_ms;
    }
}

static void on_prompt_close(DashClient *dash, uint8_t id)
{
    if ((dash->prompt_asm.active != 0U) && (dash->prompt_asm.id == id))
    {
        Proto_AssemblerReset(&dash->prompt_asm);  /* a newer version of it, half here */
    }
    if (dash->prompt_shown == id)
    {
        close_prompt(dash);
    }
    else
    {
        dash->frames_ignored++;
    }
}

static void on_banner_part(DashClient *dash, const ProtoRecordPart *part, uint32_t now_ms)
{
    if (Proto_AssemblerAdd(&dash->banner_asm, part) != PROTO_ASM_COMPLETE)
    {
        dash->banner_part_ms = now_ms;
        return;
    }
    if (dash->prompt_shown != DASH_NO_PROMPT)
    {
        dash->frames_ignored++;  /* a banner never covers a prompt */
        return;
    }
    if (dash->cb.show_banner != NULL)
    {
        dash->cb.show_banner((const char *)dash->banner_buf, dash->cb.ctx);
    }
}

static void on_media_part(DashClient *dash, const ProtoRecordPart *part, uint32_t now_ms)
{
    if (Proto_AssemblerAdd(&dash->media_asm, part) == PROTO_ASM_COMPLETE)
    {
        if (dash->cb.show_media != NULL)
        {
            dash->cb.show_media((const char *)dash->media_buf, dash->cb.ctx);
        }
    }
    else
    {
        dash->media_part_ms = now_ms;
    }
}

static void show_art(DashClient *dash, uint8_t art_id, const uint8_t *pixels)
{
    dash->art_shown = art_id;
    if (dash->cb.show_art != NULL)
    {
        dash->cb.show_art(pixels, dash->cb.ctx);
    }
}

static void on_media_state(DashClient *dash, const ProtoMediaState *state, uint32_t now_ms)
{
    if (dash->cb.media_state != NULL)
    {
        dash->cb.media_state(state, dash->cb.ctx);
    }
    if (state->art_id == PROTO_NO_ART)
    {
        Proto_ArtAssemblerStart(&dash->art_asm, PROTO_NO_ART);
        if (dash->art_shown != PROTO_NO_ART)
        {
            show_art(dash, PROTO_NO_ART, NULL);
        }
    }
    else if (state->art_id == dash->art_shown)
    {
        Proto_ArtAssemblerStart(&dash->art_asm, PROTO_NO_ART);  /* shown already: no need for its parts */
    }
    else if (dash->art_asm.buffer != NULL)
    {
        if (dash->art_asm.art_id != state->art_id)
        {
            dash->art_part_ms = now_ms;  /* its parts follow */
        }
        Proto_ArtAssemblerStart(&dash->art_asm, state->art_id);
    }
    else
    {
        /* No buffer: covers are not shown. */
    }
}

static void on_art_part(DashClient *dash, const ProtoArtPart *part, uint32_t now_ms)
{
    switch (Proto_ArtAssemblerAdd(&dash->art_asm, part))
    {
    case PROTO_ASM_COMPLETE:
        show_art(dash, part->art_id, dash->art_asm.buffer);
        break;
    case PROTO_ASM_PARTIAL:
        dash->art_part_ms = now_ms;
        break;
    case PROTO_ASM_DUPLICATE:
        /* Sent again after GET_MEDIA: the cover is still coming. */
        dash->art_part_ms = now_ms;
        dash->frames_ignored++;
        break;
    default:
        dash->frames_ignored++;  /* another cover, or one shown already */
        break;
    }
}

static void show_picture(DashClient *dash, uint8_t pic_id, const uint8_t *pixels)
{
    dash->pic_shown = pic_id;
    if (dash->cb.show_picture != NULL)
    {
        dash->cb.show_picture(pixels, dash->cb.ctx);
    }
}

static void on_picture(DashClient *dash, uint8_t pic_id, uint32_t now_ms)
{
    if (pic_id == PROTO_NO_PICTURE)
    {
        Proto_ArtAssemblerStart(&dash->pic_asm, PROTO_NO_ART);
        if (dash->pic_shown != PROTO_NO_PICTURE)
        {
            show_picture(dash, PROTO_NO_PICTURE, NULL);
        }
    }
    else if (pic_id == dash->pic_shown)
    {
        Proto_ArtAssemblerStart(&dash->pic_asm, PROTO_NO_ART);  /* shown already */
    }
    else if (dash->pic_asm.buffer != NULL)
    {
        if (dash->pic_asm.art_id != pic_id)
        {
            dash->pic_part_ms = now_ms;  /* its parts follow */
        }
        Proto_ArtAssemblerStart(&dash->pic_asm, pic_id);
    }
    else
    {
        /* No buffer: pictures are not shown. */
    }
}

static void on_picture_part(DashClient *dash, const ProtoArtPart *part, uint32_t now_ms)
{
    switch (Proto_ArtAssemblerAdd(&dash->pic_asm, part))
    {
    case PROTO_ASM_COMPLETE:
        show_picture(dash, part->art_id, dash->pic_asm.buffer);
        break;
    case PROTO_ASM_PARTIAL:
        dash->pic_part_ms = now_ms;
        break;
    case PROTO_ASM_DUPLICATE:
        /* After GET_CARDS the host sends the picture again from part 0;
         * the parts kept so far come again for a few seconds (about 250 a
         * second). They show the picture is still coming: without this the
         * client asked again every DASH_RETRY_MS and the picture never
         * completed (#68). */
        dash->pic_part_ms = now_ms;
        dash->frames_ignored++;
        break;
    default:
        dash->frames_ignored++;  /* another picture, or one shown already */
        break;
    }
}

static void on_error(DashClient *dash, const ProtoError *error)
{
    if ((error->ref_command == PROTO_CMD_OPEN_DETAIL) && dash->detail_waiting)
    {
        detail_failed(dash);
    }
    else if (error->ref_command == PROTO_CMD_ACTION)
    {
        notify(dash, DASH_NOTICE_ACTION_FAILED);
    }
    else if (error->ref_command == PROTO_CMD_SEEK)
    {
        notify(dash, DASH_NOTICE_ACTION_FAILED);
        send_get_media(dash);  /* the bar moved already: the real position */
    }
    else
    {
        /* About a frame of ours the host could not read: nothing to undo. */
    }
}

bool Dash_Owns(const ProtoMessage *msg)
{
    const uint8_t about = (msg->command == PROTO_CMD_ERROR) ? msg->u.error.ref_command : msg->command;

    return (about >= 0x20U) && (about <= 0x3FU);
}

void Dash_SetSequenceCounter(DashClient *dash, uint8_t *counter)
{
    dash->sequence = (counter != NULL) ? counter : &dash->own_sequence;
}

void Dash_Init(DashClient *dash, const DashCallbacks *callbacks)
{
    memset(dash, 0, sizeof(*dash));
    if (callbacks != NULL)
    {
        dash->cb = *callbacks;
    }
    dash->sequence = &dash->own_sequence;
    Proto_AssemblerInit(&dash->card_asm, dash->card_buf, PROTO_CARD_MAX_BYTES);
    Proto_AssemblerInit(&dash->detail_asm, dash->detail_buf, PROTO_DETAIL_MAX_BYTES);
    Proto_AssemblerInit(&dash->prompt_asm, dash->prompt_buf, PROTO_PROMPT_MAX_BYTES);
    Proto_AssemblerInit(&dash->media_asm, dash->media_buf, PROTO_MEDIA_MAX_BYTES);
    Proto_AssemblerInit(&dash->banner_asm, dash->banner_buf, PROTO_BANNER_MAX_BYTES);
    Proto_PictureAssemblerInit(&dash->pic_asm, NULL);
    dash->pic_shown = PROTO_NO_PICTURE;
    Proto_ArtAssemblerInit(&dash->art_asm, NULL);
    dash->art_shown = PROTO_NO_ART;
    dash->detail_slot = DASH_NO_SLOT;
    dash->prompt_shown = DASH_NO_PROMPT;
}

void Dash_SetArtBuffer(DashClient *dash, uint8_t *buffer)
{
    Proto_ArtAssemblerInit(&dash->art_asm, buffer);
}

void Dash_SetPictureBuffer(DashClient *dash, uint8_t *buffer)
{
    Proto_PictureAssemblerInit(&dash->pic_asm, buffer);
}

void Dash_OnConnected(DashClient *dash, uint32_t now_ms)
{
    (void)now_ms;
    dash->connected = true;
    Proto_AssemblerReset(&dash->card_asm);
    Proto_AssemblerReset(&dash->media_asm);
    Proto_ArtAssemblerStart(&dash->art_asm, PROTO_NO_ART);
    Proto_ArtAssemblerStart(&dash->pic_asm, PROTO_NO_ART);
    send_get_cards(dash);
    send_get_media(dash);
}

void Dash_OnDisconnected(DashClient *dash)
{
    dash->connected = false;
    Proto_AssemblerReset(&dash->card_asm);
    Proto_AssemblerReset(&dash->detail_asm);
    Proto_AssemblerReset(&dash->prompt_asm);
    Proto_AssemblerReset(&dash->banner_asm);
    Proto_ArtAssemblerStart(&dash->pic_asm, PROTO_NO_ART);  /* the shown picture stays */
    dash->detail_waiting = false;
    dash->detail_slot = DASH_NO_SLOT;
    close_prompt(dash);  /* nobody could receive its answer */

    /* Nothing is known to play any more. */
    Proto_AssemblerReset(&dash->media_asm);
    {
        ProtoMediaState none;

        memset(&none, 0, sizeof(none));
        on_media_state(dash, &none, 0U);
    }
}

void Dash_OnMessage(DashClient *dash, const ProtoMessage *msg, uint32_t now_ms)
{
    switch (msg->command)
    {
    case PROTO_CMD_TIME:
        dash->time_known = true;
        dash->time_base = msg->u.local_time;
        dash->time_base_ms = now_ms;
        if (dash->cb.set_time != NULL)
        {
            dash->cb.set_time(msg->u.local_time, dash->cb.ctx);
        }
        break;
    case PROTO_CMD_CARD_PART:
        on_card_part(dash, &msg->u.part, now_ms);
        break;
    case PROTO_CMD_CARD_CLEAR:
        show_card(dash, msg->u.slot, "");
        break;
    case PROTO_CMD_DETAIL_PART:
        on_detail_part(dash, &msg->u.part, now_ms);
        break;
    case PROTO_CMD_PROMPT_PART:
        on_prompt_part(dash, &msg->u.part, now_ms);
        break;
    case PROTO_CMD_PROMPT_CLOSE:
        on_prompt_close(dash, msg->u.prompt_id);
        break;
    case PROTO_CMD_MEDIA_PART:
        on_media_part(dash, &msg->u.part, now_ms);
        break;
    case PROTO_CMD_MEDIA_STATE:
        on_media_state(dash, &msg->u.media, now_ms);
        break;
    case PROTO_CMD_ART_PART:
        on_art_part(dash, &msg->u.art, now_ms);
        break;
    case PROTO_CMD_BANNER_PART:
        on_banner_part(dash, &msg->u.part, now_ms);
        break;
    case PROTO_CMD_INDICATORS:
        if (dash->cb.show_indicators != NULL)
        {
            dash->cb.show_indicators(&msg->u.indicators, dash->cb.ctx);
        }
        break;
    case PROTO_CMD_PICTURE:
        on_picture(dash, msg->u.pic_id, now_ms);
        break;
    case PROTO_CMD_PICTURE_PART:
        on_picture_part(dash, &msg->u.art, now_ms);
        break;
    case PROTO_CMD_ERROR:
        on_error(dash, &msg->u.error);
        break;
    default:
        /* STM32 -> host commands. */
        dash->frames_ignored++;
        break;
    }
}

void Dash_Poll(DashClient *dash, uint32_t now_ms)
{
    if (!dash->connected)
    {
        return;
    }
    if (((dash->card_asm.active != 0U) && ((now_ms - dash->card_part_ms) >= DASH_RETRY_MS)) ||
        ((dash->pic_asm.art_id != PROTO_NO_ART) && ((now_ms - dash->pic_part_ms) >= DASH_RETRY_MS)))
    {
        /* A card part or a picture part was lost: ask for all cards again
         * (the picture follows them). Picture parts collected so far are
         * kept (same pic_id). */
        Proto_AssemblerReset(&dash->card_asm);
        dash->pic_part_ms = now_ms;
        send_get_cards(dash);
    }
    if (((dash->media_asm.active != 0U) && ((now_ms - dash->media_part_ms) >= DASH_RETRY_MS)) ||
        ((dash->art_asm.art_id != PROTO_NO_ART) && ((now_ms - dash->art_part_ms) >= DASH_RETRY_MS)))
    {
        /* A media part or cover part was lost: ask for all of it again. The
         * cover parts collected so far are kept (same art_id). */
        Proto_AssemblerReset(&dash->media_asm);
        dash->art_part_ms = now_ms;
        send_get_media(dash);
    }
    if ((dash->prompt_asm.active != 0U) && ((now_ms - dash->prompt_part_ms) >= DASH_RETRY_MS))
    {
        /* A prompt part was lost: drop it; the host times the request out. */
        Proto_AssemblerReset(&dash->prompt_asm);
        dash->frames_ignored++;
    }
    if ((dash->banner_asm.active != 0U) && ((now_ms - dash->banner_part_ms) >= DASH_RETRY_MS))
    {
        /* A banner part was lost: drop it; a banner is about now. */
        Proto_AssemblerReset(&dash->banner_asm);
        dash->frames_ignored++;
    }
    if (dash->detail_slot == DASH_NO_SLOT)
    {
        return;
    }
    if ((dash->detail_asm.active != 0U) && ((now_ms - dash->detail_part_ms) >= DASH_RETRY_MS))
    {
        Proto_AssemblerReset(&dash->detail_asm);
        (void)send_open_detail(dash, dash->detail_slot, dash->detail_arg);
        dash->detail_asked_ms = now_ms;
        dash->detail_waiting = true;
    }
    else if (dash->detail_waiting && (dash->detail_asm.active == 0U) &&
             ((now_ms - dash->detail_asked_ms) >= DASH_DETAIL_TIMEOUT_MS))
    {
        detail_failed(dash);
    }
    else
    {
        /* nothing due */
    }
}

bool Dash_OpenDetail(DashClient *dash, uint8_t slot, int8_t arg, uint32_t now_ms)
{
    if (!dash->connected || (slot >= PROTO_MAX_CARDS))
    {
        return false;
    }
    Proto_AssemblerReset(&dash->detail_asm);
    dash->detail_slot = slot;
    dash->detail_arg = arg;
    dash->detail_waiting = true;
    dash->detail_asked_ms = now_ms;
    if (!send_open_detail(dash, slot, arg))
    {
        dash->detail_waiting = false;
        dash->detail_slot = DASH_NO_SLOT;
        return false;
    }
    return true;
}

void Dash_CloseDetail(DashClient *dash)
{
    dash->detail_slot = DASH_NO_SLOT;
    dash->detail_waiting = false;
    Proto_AssemblerReset(&dash->detail_asm);
}

bool Dash_Action(DashClient *dash, uint8_t kind, uint8_t ref, uint8_t action)
{
    ProtoMessage msg;

    if (!dash->connected)
    {
        return false;
    }
    memset(&msg, 0, sizeof(msg));
    msg.command = PROTO_CMD_ACTION;
    msg.u.action.kind = kind;
    msg.u.action.ref = ref;
    msg.u.action.action = action;
    return send_message(dash, &msg);
}

bool Dash_Seek(DashClient *dash, uint32_t seconds)
{
    ProtoMessage msg;

    if (!dash->connected)
    {
        return false;
    }
    memset(&msg, 0, sizeof(msg));
    msg.command = PROTO_CMD_SEEK;
    msg.u.seek_s = seconds;
    return send_message(dash, &msg);
}

bool Dash_AnswerPrompt(DashClient *dash, uint8_t id, uint8_t action)
{
    if ((dash->prompt_shown != id) || (action == 0U))
    {
        return false;
    }
    return Dash_Action(dash, PROTO_ACTION_PROMPT, id, action);
}

uint32_t Dash_LocalTime(const DashClient *dash, uint32_t now_ms)
{
    if (!dash->time_known)
    {
        return 0U;
    }
    return dash->time_base + ((now_ms - dash->time_base_ms) / 1000U);
}
