/**
 * @file    dash_client.h
 * @brief   STM32 side of the dashboard commands of protocol v6 (ADR-006,
 *          ADR-008, ADR-009, ADR-010; protocol/protocol.md, "Dashboard
 *          records", "Now playing", "Banner", "Status icons and picture").
 *
 * Hardware independent, like link_client.h: frames go out through a
 * callback; decoded dashboard messages come in through Dash_OnMessage()
 * (the port splits the frames between this client and the audio one);
 * the UI is driven through callbacks; time is passed in by the caller.
 *
 * Behaviour:
 *   - Dash_OnConnected() sends GET_CARDS and GET_MEDIA; the host answers
 *     with TIME and every card slot (CARD_PART or CARD_CLEAR), and with
 *     what plays (MEDIA_PART, MEDIA_STATE, the cover).
 *   - Card and detail records are collected from their parts
 *     (ProtoAssembler) and handed to the UI only when complete. A record
 *     still incomplete LINK_RETRY_MS after its last part is asked for
 *     again: GET_CARDS for a card, OPEN_DETAIL for the detail.
 *   - Dash_OpenDetail() sends OPEN_DETAIL; the detail that comes back is
 *     shown. Without a complete detail or an ERROR within
 *     DASH_DETAIL_TIMEOUT_MS the UI is told it failed. Detail parts for
 *     another slot than the one open are ignored. Dash_CloseDetail()
 *     forgets the open slot.
 *   - Dash_Action() sends ACTION; an ERROR about it is a notice.
 *   - A complete PROMPT_PART record is shown (ADR-007: a Claude Code
 *     permission request); PROMPT_CLOSE closes it. A prompt still
 *     incomplete DASH_RETRY_MS after its last part is dropped (the host
 *     times it out). A disconnect closes the shown prompt.
 *     Dash_AnswerPrompt() sends ACTION kind 1 for it.
 *   - TIME sets the clock: Dash_LocalTime() counts on from the last TIME.
 *   - Now playing (ADR-008): a complete media record goes to show_media,
 *     MEDIA_STATE to media_state. The cover of the last MEDIA_STATE is
 *     collected into the buffer given with Dash_SetArtBuffer and handed to
 *     show_art when complete (NULL: no cover); a cover already shown is
 *     not collected again. A media record or cover still incomplete
 *     DASH_RETRY_MS after its last part (or after the MEDIA_STATE that
 *     named it) is asked for again with GET_MEDIA. A disconnect reports
 *     status none and no cover.
 *   - Banner (ADR-009): a complete BANNER_PART record goes to show_banner,
 *     unless a prompt is shown (a banner never covers a prompt; it is
 *     dropped). A banner still incomplete DASH_RETRY_MS after its last part
 *     is dropped; it is never asked for again.
 *   - Status icons and picture (ADR-010): INDICATORS goes to
 *     show_indicators. The picture of the last PICTURE is collected into the
 *     buffer given with Dash_SetPictureBuffer and handed to show_picture when
 *     complete (NULL: PICTURE 0, no picture); a picture already shown is not
 *     collected again; one still incomplete DASH_RETRY_MS after its last
 *     part (or its PICTURE) makes the client send GET_CARDS.
 *
 * Sequence numbers: the client numbers its frames from a counter it is
 * given (Dash_SetSequenceCounter), so that it can share the audio
 * client's counter (LinkClient.sequence): one counter per sender, as
 * protocol.md defines it, and the audio client still recognises the
 * ERROR frames that answer its own requests by their ref_sequence.
 *
 * Static buffers only (one card, one detail, one prompt, one media and one
 * banner record being assembled; the cover buffer is the caller's).
 * Not thread-safe: call everything from one task.
 */
#ifndef DASH_CLIENT_H
#define DASH_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DASH_RETRY_MS          1000U /**< = LINK_RETRY_MS */
#define DASH_DETAIL_TIMEOUT_MS 2000U /**< = LINK_RESPONSE_TIMEOUT_MS */
#define DASH_NO_SLOT           0xFFU
#define DASH_NO_PROMPT         0xFFFFU

typedef enum
{
    DASH_NOTICE_DETAIL_FAILED, /**< no detail: ERROR or timeout */
    DASH_NOTICE_ACTION_FAILED, /**< the host answered ERROR to an ACTION */
} DashNotice;

typedef struct
{
    /** Send one encoded frame; return false if it could not be queued. */
    bool (*send_frame)(const uint8_t frame[PROTO_FRAME_SIZE], void *ctx);
    /** A card slot changed: its record text, "" when the slot is empty. */
    void (*show_card)(uint8_t slot, const char *text, void *ctx);
    /** The detail of the open slot arrived (NUL-terminated record text). */
    void (*show_detail)(uint8_t slot, const char *text, void *ctx);
    void (*notice)(DashNotice notice, void *ctx);
    /** TIME arrived: local time in seconds since 1970 (protocol.md). */
    void (*set_time)(uint32_t local_time, void *ctx);
    /** A prompt arrived (NUL-terminated record text); replaces any shown. */
    void (*show_prompt)(uint8_t id, const char *text, void *ctx);
    /** The prompt @p id is to be closed (PROMPT_CLOSE or disconnect). */
    void (*close_prompt)(uint8_t id, void *ctx);
    void *ctx;
    /* Now playing (ADR-008), all optional. */
    /** The media record (NUL-terminated: title ␟ artist ␟ album ␟ app). */
    void (*show_media)(const char *text, void *ctx);
    /** MEDIA_STATE arrived (also status none on a disconnect). */
    void (*media_state)(const ProtoMediaState *state, void *ctx);
    /** A complete cover (PROTO_ART_BYTES of RGB565), or NULL: none. The
     *  buffer is reused for the next cover: copy what is to be kept. */
    void (*show_art)(const uint8_t *pixels, void *ctx);
    /* Banner (ADR-009), optional. */
    /** A banner arrived (NUL-terminated record text); not while a prompt is
     *  shown. */
    void (*show_banner)(const char *text, void *ctx);
    /* Status icons and picture (ADR-010), optional. */
    /** INDICATORS arrived. */
    void (*show_indicators)(const ProtoIndicators *indicators, void *ctx);
    /** A complete picture (PROTO_PICTURE_BYTES of RGB565), or NULL: none.
     *  The buffer is reused for the next picture: copy what is to be kept. */
    void (*show_picture)(const uint8_t *pixels, void *ctx);
} DashCallbacks;

typedef struct
{
    DashCallbacks cb;
    bool connected;
    uint8_t own_sequence;     /**< used until Dash_SetSequenceCounter */
    uint8_t *sequence;        /**< the counter frames are numbered from */

    ProtoAssembler card_asm;
    uint8_t card_buf[PROTO_CARD_MAX_BYTES + 1U];
    uint32_t card_part_ms;     /**< time of the last card part */

    ProtoAssembler detail_asm;
    uint8_t detail_buf[PROTO_DETAIL_MAX_BYTES + 1U];
    uint32_t detail_part_ms;   /**< time of the last detail part */
    uint8_t detail_slot;       /**< open slot, DASH_NO_SLOT if none */
    int8_t detail_arg;         /**< arg of the last OPEN_DETAIL */
    bool detail_waiting;       /**< OPEN_DETAIL sent, no complete detail yet */
    uint32_t detail_asked_ms;

    ProtoAssembler prompt_asm;
    uint8_t prompt_buf[PROTO_PROMPT_MAX_BYTES + 1U];
    uint32_t prompt_part_ms;   /**< time of the last prompt part */
    uint16_t prompt_shown;     /**< id of the shown prompt, DASH_NO_PROMPT if none */

    ProtoAssembler media_asm;
    uint8_t media_buf[PROTO_MEDIA_MAX_BYTES + 1U];
    uint32_t media_part_ms;    /**< time of the last media part */

    ProtoArtAssembler art_asm; /**< collects into the caller's buffer */
    uint32_t art_part_ms;      /**< time of the last cover part or its MEDIA_STATE */
    uint8_t art_shown;         /**< id of the cover handed to show_art, PROTO_NO_ART if none */

    ProtoAssembler banner_asm;
    uint8_t banner_buf[PROTO_BANNER_MAX_BYTES + 1U];
    uint32_t banner_part_ms;   /**< time of the last banner part */

    ProtoArtAssembler pic_asm; /**< collects the picture into the caller's buffer */
    uint32_t pic_part_ms;      /**< time of the last picture part or its PICTURE */
    uint8_t pic_shown;         /**< id handed to show_picture, PROTO_NO_PICTURE if none */

    bool time_known;
    uint32_t time_base;        /**< local time of the last TIME */
    uint32_t time_base_ms;     /**< now_ms when it arrived */

    uint32_t frames_ignored;   /**< parts for a slot not open, wrong direction */
} DashClient;

void Dash_Init(DashClient *dash, const DashCallbacks *callbacks);

/** True if a decoded message belongs to this client: a dashboard command
 *  (0x20..0x3F) or an ERROR about one. The port passes the others to the
 *  audio client (link_client.h). */
bool Dash_Owns(const ProtoMessage *msg);

/** Number frames from @p counter (e.g. &link.sequence of the audio client). */
void Dash_SetSequenceCounter(DashClient *dash, uint8_t *counter);

/** The buffer covers are collected in (PROTO_ART_BYTES); without one,
 *  covers are ignored. */
void Dash_SetArtBuffer(DashClient *dash, uint8_t *buffer);

/** The buffer the picture is collected in (PROTO_PICTURE_BYTES); without
 *  one, pictures are ignored. */
void Dash_SetPictureBuffer(DashClient *dash, uint8_t *buffer);

/** USB configured: ask for the cards (and the time) and what plays. */
void Dash_OnConnected(DashClient *dash, uint32_t now_ms);
void Dash_OnDisconnected(DashClient *dash);

/** A decoded dashboard message (0x20..0x3F) or an ERROR about one. */
void Dash_OnMessage(DashClient *dash, const ProtoMessage *msg, uint32_t now_ms);

/** Retries and timeouts; call from the UI loop. */
void Dash_Poll(DashClient *dash, uint32_t now_ms);

/** Ask for the detail of @p slot (arg: 0, -1 previous, +1 next). False if
 *  not connected or the frame could not be queued. */
bool Dash_OpenDetail(DashClient *dash, uint8_t slot, int8_t arg, uint32_t now_ms);

/** The detail view was closed. */
void Dash_CloseDetail(DashClient *dash);

/** Send ACTION. False if not connected, invalid or not queued. */
bool Dash_Action(DashClient *dash, uint8_t kind, uint8_t ref, uint8_t action);

/** Answer the shown prompt @p id with button @p action (1..255): ACTION
 *  kind 1. False if not connected, not the shown prompt, or not queued. The
 *  prompt stays until the host closes it. */
bool Dash_AnswerPrompt(DashClient *dash, uint8_t id, uint8_t action);

/** Local time now (seconds since 1970, protocol.md "TIME"), or 0 if no
 *  TIME has arrived yet. */
uint32_t Dash_LocalTime(const DashClient *dash, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* DASH_CLIENT_H */
