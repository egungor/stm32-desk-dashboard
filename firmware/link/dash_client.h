/**
 * @file    dash_client.h
 * @brief   STM32 side of the dashboard commands of protocol v3 (ADR-006;
 *          protocol/protocol.md, "Dashboard records").
 *
 * Hardware independent, like link_client.h: frames go out through a
 * callback; decoded dashboard messages come in through Dash_OnMessage()
 * (the port splits the frames between this client and the audio one);
 * the UI is driven through callbacks; time is passed in by the caller.
 *
 * Behaviour:
 *   - Dash_OnConnected() sends GET_CARDS; the host answers with TIME and
 *     every card slot (CARD_PART or CARD_CLEAR).
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
 *   - TIME sets the clock: Dash_LocalTime() counts on from the last TIME.
 *
 * Sequence numbers: the client numbers its frames from a counter it is
 * given (Dash_SetSequenceCounter), so that it can share the audio
 * client's counter (LinkClient.sequence): one counter per sender, as
 * protocol.md defines it, and the audio client still recognises the
 * ERROR frames that answer its own requests by their ref_sequence.
 *
 * Static buffers only (one card and one detail record being assembled).
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
    void *ctx;
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

/** USB configured: ask for the cards (and the time). */
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

/** Local time now (seconds since 1970, protocol.md "TIME"), or 0 if no
 *  TIME has arrived yet. */
uint32_t Dash_LocalTime(const DashClient *dash, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* DASH_CLIENT_H */
