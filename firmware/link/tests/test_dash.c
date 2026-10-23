/**
 * @file    test_dash.c
 * @brief   Unit tests for the dashboard client (no test framework).
 */
#include "dash_client.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        if (!(cond))                                                         \
        {                                                                    \
            g_failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------- fakes */

#define MAX_SENT 32

static struct
{
    ProtoMessage sent[MAX_SENT];
    int          sent_count;
    bool         fail_send;

    int     card_calls;
    uint8_t card_slot;
    char    card_text[PROTO_CARD_MAX_BYTES + 1U];
    int     detail_calls;
    uint8_t detail_slot;
    char    detail_text[PROTO_DETAIL_MAX_BYTES + 1U];
    int     notices[2];
    int     time_calls;
    uint32_t time;
    int     prompt_calls;
    uint8_t prompt_id;
    char    prompt_text[PROTO_PROMPT_MAX_BYTES + 1U];
    int     close_calls;
    uint8_t close_id;
    int     media_calls;
    char    media_text[PROTO_MEDIA_MAX_BYTES + 1U];
    int     state_calls;
    ProtoMediaState state;
    int     art_calls;
    const uint8_t *art;
    int     banner_calls;
    char    banner_text[PROTO_BANNER_MAX_BYTES + 1U];
    int     ind_calls;
    ProtoIndicators ind;
    int     pic_calls;
    const uint8_t *pic;
} g;

static bool fake_send(const uint8_t frame[PROTO_FRAME_SIZE], void *ctx)
{
    ProtoMessage msg;

    (void)ctx;
    if (g.fail_send)
    {
        return false;
    }
    CHECK(Proto_Decode(frame, &msg) == PROTO_OK);
    if (g.sent_count < MAX_SENT)
    {
        g.sent[g.sent_count++] = msg;
    }
    return true;
}

static void fake_card(uint8_t slot, const char *text, void *ctx)
{
    (void)ctx;
    g.card_calls++;
    g.card_slot = slot;
    (void)snprintf(g.card_text, sizeof(g.card_text), "%s", text);
}

static void fake_detail(uint8_t slot, const char *text, void *ctx)
{
    (void)ctx;
    g.detail_calls++;
    g.detail_slot = slot;
    (void)snprintf(g.detail_text, sizeof(g.detail_text), "%s", text);
}

static void fake_notice(DashNotice n, void *ctx)
{
    (void)ctx;
    g.notices[n]++;
}

static void fake_time(uint32_t t, void *ctx)
{
    (void)ctx;
    g.time_calls++;
    g.time = t;
}

static void fake_prompt(uint8_t id, const char *text, void *ctx)
{
    (void)ctx;
    g.prompt_calls++;
    g.prompt_id = id;
    (void)snprintf(g.prompt_text, sizeof(g.prompt_text), "%s", text);
}

static void fake_close(uint8_t id, void *ctx)
{
    (void)ctx;
    g.close_calls++;
    g.close_id = id;
}

static void fake_media(const char *text, void *ctx)
{
    (void)ctx;
    g.media_calls++;
    (void)snprintf(g.media_text, sizeof(g.media_text), "%s", text);
}

static void fake_state(const ProtoMediaState *state, void *ctx)
{
    (void)ctx;
    g.state_calls++;
    g.state = *state;
}

static void fake_art(const uint8_t *pixels, void *ctx)
{
    (void)ctx;
    g.art_calls++;
    g.art = pixels;
}

static void fake_banner(const char *text, void *ctx)
{
    (void)ctx;
    g.banner_calls++;
    (void)snprintf(g.banner_text, sizeof(g.banner_text), "%s", text);
}

static void fake_indicators(const ProtoIndicators *ind, void *ctx)
{
    (void)ctx;
    g.ind_calls++;
    g.ind = *ind;
}

static void fake_picture(const uint8_t *pixels, void *ctx)
{
    (void)ctx;
    g.pic_calls++;
    g.pic = pixels;
}

static DashClient s_dash;
static uint8_t s_pic_buf[PROTO_PICTURE_BYTES];
static uint8_t s_art_buf[PROTO_ART_BYTES];

static void reset(void)
{
    static const DashCallbacks cb = { fake_send,  fake_card,  fake_detail, fake_notice, fake_time,
                                      fake_prompt, fake_close, NULL,        fake_media,  fake_state,
                                      fake_art,   fake_banner, fake_indicators, fake_picture };

    memset(&g, 0, sizeof(g));
    Dash_Init(&s_dash, &cb);
}

/* Feed every part of @p text as @p command (the host's side of a record). */
static void feed_record(uint8_t command, const char *text, uint8_t id, uint8_t generation, uint32_t now,
                        int skip_part)
{
    const uint16_t len = (uint16_t)strlen(text);
    const uint8_t parts = Proto_RecordPartCount(len);

    for (uint8_t p = 0U; p < parts; p++)
    {
        ProtoMessage msg;

        if ((int)p == skip_part)
        {
            continue;
        }
        memset(&msg, 0, sizeof(msg));
        msg.command = command;
        CHECK(Proto_RecordPart((const uint8_t *)text, len, id, generation, p, &msg.u.part) == 1);
        Dash_OnMessage(&s_dash, &msg, now);
    }
}

static ProtoMessage simple(uint8_t command)
{
    ProtoMessage msg;

    memset(&msg, 0, sizeof(msg));
    msg.command = command;
    return msg;
}

/* ------------------------------------------------------------- tests */

static void test_owns(void)
{
    ProtoMessage m = simple(PROTO_CMD_CARD_PART);

    CHECK(Dash_Owns(&m));
    m.command = PROTO_CMD_DEVICE_LIST;
    CHECK(!Dash_Owns(&m));
    m.command = PROTO_CMD_ERROR;
    m.u.error.ref_command = PROTO_CMD_OPEN_DETAIL;
    CHECK(Dash_Owns(&m));
    m.u.error.ref_command = PROTO_CMD_SELECT_DEVICE;
    CHECK(!Dash_Owns(&m));
    m.u.error.ref_command = 0U; /* about nothing: the audio client's */
    CHECK(!Dash_Owns(&m));
}

static void test_shared_sequence(void)
{
    uint8_t shared = 40U;

    reset();
    Dash_SetSequenceCounter(&s_dash, &shared);
    Dash_OnConnected(&s_dash, 0U);
    CHECK(g.sent_count == 2 && g.sent[0].sequence == 40U && g.sent[1].sequence == 41U && shared == 42U);
    CHECK(Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, PROTO_MEDIA_NEXT));
    CHECK(g.sent_count == 3 && g.sent[2].sequence == 42U && shared == 43U);
}

static void test_connect_asks_for_cards(void)
{
    reset();
    CHECK(!Dash_OpenDetail(&s_dash, 0U, 0, 0U)); /* not connected */
    CHECK(!Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, PROTO_MEDIA_NEXT));
    Dash_OnConnected(&s_dash, 10U);
    CHECK(g.sent_count == 2 && g.sent[0].command == PROTO_CMD_GET_CARDS && g.sent[1].command == PROTO_CMD_GET_MEDIA);
}

static void test_cards(void)
{
    static char long_text[PROTO_CARD_MAX_BYTES + 1U];

    reset();
    Dash_OnConnected(&s_dash, 0U);

    feed_record(PROTO_CMD_CARD_PART, "0\x1FHava\x1F\n14 C", 1U, 1U, 100U, -1);
    CHECK(g.card_calls == 1 && g.card_slot == 1U && strcmp(g.card_text, "0\x1FHava\x1F\n14 C") == 0);

    /* A full-size card in five parts. */
    memset(long_text, 'k', PROTO_CARD_MAX_BYTES);
    long_text[PROTO_CARD_MAX_BYTES] = '\0';
    feed_record(PROTO_CMD_CARD_PART, long_text, 3U, 2U, 200U, -1);
    CHECK(g.card_calls == 2 && g.card_slot == 3U && strlen(g.card_text) == PROTO_CARD_MAX_BYTES);

    /* CARD_CLEAR empties a slot. */
    {
        ProtoMessage clear = simple(PROTO_CMD_CARD_CLEAR);

        clear.u.slot = 3U;
        Dash_OnMessage(&s_dash, &clear, 300U);
        CHECK(g.card_calls == 3 && g.card_slot == 3U && g.card_text[0] == '\0');
    }
}

static void test_lost_card_part_asks_again(void)
{
    static char text[120];

    reset();
    Dash_OnConnected(&s_dash, 0U);
    g.sent_count = 0;
    memset(text, 'a', sizeof(text) - 1U);
    text[sizeof(text) - 1U] = '\0';
    feed_record(PROTO_CMD_CARD_PART, text, 0U, 5U, 1000U, 1); /* part 1 of 3 lost */
    CHECK(g.card_calls == 0);
    Dash_Poll(&s_dash, 1000U + DASH_RETRY_MS - 1U);
    CHECK(g.sent_count == 0);
    Dash_Poll(&s_dash, 1000U + DASH_RETRY_MS);
    CHECK(g.sent_count == 1 && g.sent[0].command == PROTO_CMD_GET_CARDS);
    Dash_Poll(&s_dash, 1000U + (3U * DASH_RETRY_MS));
    CHECK(g.sent_count == 1); /* asked once; the next answer starts afresh */
    feed_record(PROTO_CMD_CARD_PART, text, 0U, 5U, 4000U, -1);
    CHECK(g.card_calls == 1 && strcmp(g.card_text, text) == 0);
}

static void test_detail(void)
{
    const char *menu = "Yemek\x1F" "5 Ekim\x1Fn\n0\x1F" "Ana yemek\x1F" "Ka\xC4\x9F\xC4\xB1t kebab\xC4\xB1";

    reset();
    Dash_OnConnected(&s_dash, 0U);
    g.sent_count = 0;

    CHECK(Dash_OpenDetail(&s_dash, 1U, 0, 100U));
    CHECK(g.sent_count == 1 && g.sent[0].command == PROTO_CMD_OPEN_DETAIL && g.sent[0].u.open_detail.slot == 1U &&
          g.sent[0].u.open_detail.arg == 0);

    /* A detail for another slot is ignored. */
    feed_record(PROTO_CMD_DETAIL_PART, "x", 2U, 1U, 150U, -1);
    CHECK(g.detail_calls == 0 && s_dash.frames_ignored == 1U);

    feed_record(PROTO_CMD_DETAIL_PART, menu, 1U, 2U, 200U, -1);
    CHECK(g.detail_calls == 1 && g.detail_slot == 1U && strcmp(g.detail_text, menu) == 0);
    Dash_Poll(&s_dash, 200U + DASH_DETAIL_TIMEOUT_MS + 10U);
    CHECK(g.notices[DASH_NOTICE_DETAIL_FAILED] == 0); /* it arrived: no timeout */

    /* Next day: OPEN_DETAIL with +1. */
    CHECK(Dash_OpenDetail(&s_dash, 1U, 1, 5000U));
    CHECK(g.sent[g.sent_count - 1].u.open_detail.arg == 1);

    /* An update of the open detail arrives unasked. */
    feed_record(PROTO_CMD_DETAIL_PART, "Yemek\x1F" "6 Ekim", 1U, 3U, 5100U, -1);
    CHECK(g.detail_calls == 2 && strcmp(g.detail_text, "Yemek\x1F" "6 Ekim") == 0);

    /* Closed: further parts are ignored. */
    Dash_CloseDetail(&s_dash);
    feed_record(PROTO_CMD_DETAIL_PART, "late", 1U, 4U, 6000U, -1);
    CHECK(g.detail_calls == 2);
}

static void test_detail_failures(void)
{
    ProtoMessage err = simple(PROTO_CMD_ERROR);
    static char text[200];

    reset();
    Dash_OnConnected(&s_dash, 0U);

    /* ERROR about OPEN_DETAIL. */
    CHECK(Dash_OpenDetail(&s_dash, 0U, 0, 100U));
    err.u.error.code = PROTO_ERROR_OPERATION_FAILED;
    err.u.error.ref_command = PROTO_CMD_OPEN_DETAIL;
    Dash_OnMessage(&s_dash, &err, 150U);
    CHECK(g.notices[DASH_NOTICE_DETAIL_FAILED] == 1 && s_dash.detail_slot == DASH_NO_SLOT);

    /* No answer at all. */
    CHECK(Dash_OpenDetail(&s_dash, 0U, 0, 1000U));
    Dash_Poll(&s_dash, 1000U + DASH_DETAIL_TIMEOUT_MS - 1U);
    CHECK(g.notices[DASH_NOTICE_DETAIL_FAILED] == 1);
    Dash_Poll(&s_dash, 1000U + DASH_DETAIL_TIMEOUT_MS);
    CHECK(g.notices[DASH_NOTICE_DETAIL_FAILED] == 2);

    /* A lost detail part: OPEN_DETAIL again, with the same arg. */
    memset(text, 'b', sizeof(text) - 1U);
    text[sizeof(text) - 1U] = '\0';
    CHECK(Dash_OpenDetail(&s_dash, 2U, -1, 10000U));
    g.sent_count = 0;
    feed_record(PROTO_CMD_DETAIL_PART, text, 2U, 7U, 10100U, 2);
    Dash_Poll(&s_dash, 10100U + DASH_RETRY_MS);
    CHECK(g.sent_count == 1 && g.sent[0].command == PROTO_CMD_OPEN_DETAIL && g.sent[0].u.open_detail.slot == 2U &&
          g.sent[0].u.open_detail.arg == -1);
    feed_record(PROTO_CMD_DETAIL_PART, text, 2U, 8U, 11200U, -1);
    CHECK(g.detail_calls == 1 && strcmp(g.detail_text, text) == 0);

    /* A send failure leaves nothing open. */
    g.fail_send = true;
    CHECK(!Dash_OpenDetail(&s_dash, 0U, 0, 20000U));
    CHECK(s_dash.detail_slot == DASH_NO_SLOT);
}

static void test_action_and_time(void)
{
    ProtoMessage err = simple(PROTO_CMD_ERROR);
    ProtoMessage time = simple(PROTO_CMD_TIME);

    reset();
    CHECK(Dash_LocalTime(&s_dash, 5000U) == 0U); /* unknown */
    Dash_OnConnected(&s_dash, 0U);
    g.sent_count = 0;

    CHECK(Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, PROTO_MEDIA_PLAY_PAUSE));
    CHECK(g.sent_count == 1 && g.sent[0].command == PROTO_CMD_ACTION && g.sent[0].u.action.kind == PROTO_ACTION_MEDIA &&
          g.sent[0].u.action.action == PROTO_MEDIA_PLAY_PAUSE);
    CHECK(!Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, 9U)); /* invalid: not sent */
    CHECK(g.sent_count == 1);
    err.u.error.code = PROTO_ERROR_OPERATION_FAILED;
    err.u.error.ref_command = PROTO_CMD_ACTION;
    Dash_OnMessage(&s_dash, &err, 100U);
    CHECK(g.notices[DASH_NOTICE_ACTION_FAILED] == 1);

    /* SEEK (v7): sent as is; an ERROR about it is a notice and GET_MEDIA. */
    g.sent_count = 0;
    CHECK(Dash_Seek(&s_dash, 177U));
    CHECK(g.sent_count == 1 && g.sent[0].command == PROTO_CMD_SEEK && g.sent[0].u.seek_s == 177U);
    err.u.error.ref_command = PROTO_CMD_SEEK;
    Dash_OnMessage(&s_dash, &err, 200U);
    CHECK(g.notices[DASH_NOTICE_ACTION_FAILED] == 2);
    CHECK(g.sent_count == 2 && g.sent[1].command == PROTO_CMD_GET_MEDIA);
    g.sent_count = 1;

    time.u.local_time = 1792593125U;
    Dash_OnMessage(&s_dash, &time, 0xFFFFFC18U); /* the tick wraps */
    CHECK(g.time_calls == 1 && g.time == 1792593125U);
    CHECK(Dash_LocalTime(&s_dash, 0xFFFFFC18U + 999U) == 1792593125U);
    CHECK(Dash_LocalTime(&s_dash, 0xFFFFFC18U + 61000U) == 1792593125U + 61U);

    /* After a disconnect nothing is sent. */
    Dash_OnDisconnected(&s_dash);
    CHECK(!Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, PROTO_MEDIA_NEXT));
    CHECK(!Dash_Seek(&s_dash, 10U));
    Dash_Poll(&s_dash, 0U);
    CHECK(g.sent_count == 1);
}

static void test_prompt(void)
{
    static char big[PROTO_PROMPT_MAX_BYTES + 1U];
    ProtoMessage close = simple(PROTO_CMD_PROMPT_CLOSE);
    const char *text = "Claude Code izin istiyor\x1F" "ayrac \xC2\xB7 1/1\x1F" "60\n\x1F" "Ara\xC3\xA7\x1F" "Bash\n!1\x1FOnayla";

    reset();
    Dash_OnConnected(&s_dash, 0U);
    CHECK(!Dash_AnswerPrompt(&s_dash, 7U, 1U)); /* nothing shown */

    feed_record(PROTO_CMD_PROMPT_PART, text, 7U, 1U, 100U, -1);
    CHECK(g.prompt_calls == 1 && g.prompt_id == 7U && strcmp(g.prompt_text, text) == 0);
    CHECK(!Dash_AnswerPrompt(&s_dash, 8U, 1U) && !Dash_AnswerPrompt(&s_dash, 7U, 0U));
    CHECK(Dash_AnswerPrompt(&s_dash, 7U, 2U));
    CHECK(g.sent[g.sent_count - 1].command == PROTO_CMD_ACTION &&
          g.sent[g.sent_count - 1].u.action.kind == PROTO_ACTION_PROMPT &&
          g.sent[g.sent_count - 1].u.action.ref == 7U && g.sent[g.sent_count - 1].u.action.action == 2U);

    /* PROMPT_CLOSE for another id is ignored; for the shown one it closes. */
    close.u.prompt_id = 6U;
    Dash_OnMessage(&s_dash, &close, 200U);
    CHECK(g.close_calls == 0);
    close.u.prompt_id = 7U;
    Dash_OnMessage(&s_dash, &close, 200U);
    CHECK(g.close_calls == 1 && g.close_id == 7U && !Dash_AnswerPrompt(&s_dash, 7U, 1U));

    /* A full-size prompt (20 parts); a lost part drops it after the retry time. */
    memset(big, 'p', PROTO_PROMPT_MAX_BYTES);
    big[PROTO_PROMPT_MAX_BYTES] = '\0';
    feed_record(PROTO_CMD_PROMPT_PART, big, 8U, 2U, 300U, -1);
    CHECK(g.prompt_calls == 2 && g.prompt_id == 8U && strlen(g.prompt_text) == PROTO_PROMPT_MAX_BYTES);
    feed_record(PROTO_CMD_PROMPT_PART, text, 9U, 3U, 400U, 0);
    CHECK(g.prompt_calls == 2);
    Dash_Poll(&s_dash, 400U + DASH_RETRY_MS);
    feed_record(PROTO_CMD_PROMPT_PART, text, 9U, 3U, 2000U, 1); /* the rest alone: not complete */
    CHECK(g.prompt_calls == 2);

    /* A disconnect closes the shown prompt (8). */
    Dash_OnDisconnected(&s_dash);
    CHECK(g.close_calls == 2 && g.close_id == 8U);
    CHECK(!Dash_AnswerPrompt(&s_dash, 8U, 1U));
}

/* ----------------------------------------------------------- now playing */

static void feed_state(uint8_t status, uint8_t art_id, uint32_t now)
{
    ProtoMessage msg;

    memset(&msg, 0, sizeof(msg));
    msg.command = PROTO_CMD_MEDIA_STATE;
    msg.u.media.status = status;
    msg.u.media.controls = PROTO_MEDIA_CONTROLS_MASK;
    msg.u.media.art_id = art_id;
    msg.u.media.position = 5U;
    msg.u.media.duration = 100U;
    Dash_OnMessage(&s_dash, &msg, now);
}

/* Parts @p from .. @p to - 1 of a cover filled with @p seed. */
static void feed_art(uint8_t art_id, uint8_t seed, uint16_t from, uint16_t to, uint32_t now)
{
    static uint8_t art[PROTO_ART_BYTES];

    memset(art, seed, sizeof(art));
    for (uint16_t i = from; i < to; i++)
    {
        ProtoMessage msg;

        memset(&msg, 0, sizeof(msg));
        msg.command = PROTO_CMD_ART_PART;
        CHECK(Proto_ArtPart(art, art_id, i, &msg.u.art) == 1);
        Dash_OnMessage(&s_dash, &msg, now);
    }
}

static int sent(uint8_t command)
{
    int n = 0;

    for (int i = 0; i < g.sent_count; i++)
    {
        n += (g.sent[i].command == command) ? 1 : 0;
    }
    return n;
}

static void test_media(void)
{
    reset();
    Dash_SetArtBuffer(&s_dash, s_art_buf);
    Dash_OnConnected(&s_dash, 0U);
    g.sent_count = 0;

    /* The record, then the state. */
    feed_record(PROTO_CMD_MEDIA_PART, "Song\x1F" "Artist\x1F" "Album\x1F" "Spotify", 0U, 1U, 10U, -1);
    CHECK(g.media_calls == 1 && strcmp(g.media_text, "Song\x1F" "Artist\x1F" "Album\x1F" "Spotify") == 0);
    feed_state(PROTO_MEDIA_STATUS_PLAYING, 4U, 20U);
    CHECK(g.state_calls == 1 && g.state.status == PROTO_MEDIA_STATUS_PLAYING && g.state.art_id == 4U);
    CHECK(g.state.position == 5U && g.state.duration == 100U);
    CHECK(g.art_calls == 0);

    /* The cover, complete only with the last part; out of order is fine. */
    feed_art(4U, 0x5AU, 100U, PROTO_ART_PARTS, 30U);
    CHECK(g.art_calls == 0);
    feed_art(4U, 0x5AU, 0U, 100U, 40U);
    CHECK(g.art_calls == 1 && g.art == s_art_buf && s_art_buf[0] == 0x5AU && s_art_buf[PROTO_ART_BYTES - 1U] == 0x5AU);

    /* The same cover named again: not collected again. */
    feed_state(PROTO_MEDIA_STATUS_PAUSED, 4U, 50U);
    feed_art(4U, 0x11U, 0U, PROTO_ART_PARTS, 60U);
    CHECK(g.art_calls == 1 && s_art_buf[0] == 0x5AU);
    Dash_Poll(&s_dash, 5000U);
    CHECK(sent(PROTO_CMD_GET_MEDIA) == 0);

    /* Parts of another cover than the one named are dropped. */
    feed_state(PROTO_MEDIA_STATUS_PLAYING, 5U, 6000U);
    feed_art(6U, 0x22U, 0U, PROTO_ART_PARTS, 6010U);
    CHECK(g.art_calls == 1);

    /* Cover 5 stops halfway: GET_MEDIA after DASH_RETRY_MS, once. */
    feed_art(5U, 0x33U, 0U, 200U, 6020U);
    Dash_Poll(&s_dash, 6020U + DASH_RETRY_MS - 1U);
    CHECK(sent(PROTO_CMD_GET_MEDIA) == 0);
    Dash_Poll(&s_dash, 6020U + DASH_RETRY_MS);
    CHECK(sent(PROTO_CMD_GET_MEDIA) == 1);
    Dash_Poll(&s_dash, 6020U + DASH_RETRY_MS + 10U);
    CHECK(sent(PROTO_CMD_GET_MEDIA) == 1);
    /* The answer resends from part 0; the parts kept make it complete. */
    feed_state(PROTO_MEDIA_STATUS_PLAYING, 5U, 7100U);
    feed_art(5U, 0x33U, 0U, PROTO_ART_PARTS, 7110U);
    CHECK(g.art_calls == 2 && s_art_buf[0] == 0x33U);

    /* A state without a cover: show_art(NULL). */
    feed_state(PROTO_MEDIA_STATUS_PLAYING, PROTO_NO_ART, 8000U);
    CHECK(g.art_calls == 3 && g.art == NULL);
    feed_state(PROTO_MEDIA_STATUS_PLAYING, PROTO_NO_ART, 8100U);
    CHECK(g.art_calls == 3); /* not again */

    /* A lost media part: GET_MEDIA. */
    g.sent_count = 0;
    feed_record(PROTO_CMD_MEDIA_PART,
                "A long title that needs more than one part of fifty three bytes\x1F" "Artist\x1F\x1F" "App",
                0U, 2U, 9000U, 0);
    CHECK(g.media_calls == 1);
    Dash_Poll(&s_dash, 9000U + DASH_RETRY_MS);
    CHECK(sent(PROTO_CMD_GET_MEDIA) == 1);

    /* Disconnect: status none, no cover. */
    feed_state(PROTO_MEDIA_STATUS_PLAYING, 7U, 10000U);
    feed_art(7U, 0x44U, 0U, PROTO_ART_PARTS, 10010U);
    CHECK(g.art_calls == 4 && g.art == s_art_buf);
    Dash_OnDisconnected(&s_dash);
    CHECK(g.state.status == PROTO_MEDIA_STATUS_NONE && g.art_calls == 5 && g.art == NULL);

    /* Without a buffer, covers are ignored. */
    reset();
    Dash_OnConnected(&s_dash, 0U);
    feed_state(PROTO_MEDIA_STATUS_PLAYING, 3U, 10U);
    feed_art(3U, 0x55U, 0U, PROTO_ART_PARTS, 20U);
    CHECK(g.art_calls == 0 && g.state_calls == 1);
    g.sent_count = 0;
    Dash_Poll(&s_dash, 5000U);
    CHECK(sent(PROTO_CMD_GET_MEDIA) == 0);
}

/* ---------------------------------------------------------------- banner */

static void test_banner(void)
{
    static char big[PROTO_BANNER_MAX_BYTES + 1U];
    ProtoMessage close = simple(PROTO_CMD_PROMPT_CLOSE);
    const char *text = "12\x1FWhatsApp\x1F" "5\x1F" "2\nAy\xC5\x9F" "e\nAk\xC5\x9F" "am geliyor musun? Saat yedide kap\xC4\xB1" "da.";
    const char *prompt = "Claude Code izin istiyor\x1F\x1F" "60\n!1\x1FOnayla";

    reset();
    CHECK(Dash_Owns(&(ProtoMessage){ .command = PROTO_CMD_BANNER_PART }));
    Dash_OnConnected(&s_dash, 0U);
    g.sent_count = 0;

    /* A complete banner (2 parts) is shown; nothing is sent back. */
    feed_record(PROTO_CMD_BANNER_PART, text, 200U, 1U, 100U, -1);
    CHECK(g.banner_calls == 1 && strcmp(g.banner_text, text) == 0 && g.sent_count == 0);
    /* The largest one (5 parts). */
    memset(big, 'b', PROTO_BANNER_MAX_BYTES);
    big[PROTO_BANNER_MAX_BYTES] = '\0';
    feed_record(PROTO_CMD_BANNER_PART, big, 201U, 2U, 200U, -1);
    CHECK(g.banner_calls == 2 && strlen(g.banner_text) == PROTO_BANNER_MAX_BYTES);

    /* A lost part: dropped after the retry time, never asked for again. */
    feed_record(PROTO_CMD_BANNER_PART, text, 202U, 3U, 300U, 1);
    CHECK(g.banner_calls == 2);
    Dash_Poll(&s_dash, 300U + DASH_RETRY_MS);
    CHECK(g.sent_count == 0 && s_dash.banner_asm.active == 0U);
    feed_record(PROTO_CMD_BANNER_PART, text, 202U, 3U, 2000U, 0); /* the rest alone: not complete */
    CHECK(g.banner_calls == 2);

    /* While a prompt is shown, banners are dropped. */
    feed_record(PROTO_CMD_PROMPT_PART, prompt, 9U, 1U, 3000U, -1);
    CHECK(g.prompt_calls == 1);
    feed_record(PROTO_CMD_BANNER_PART, text, 203U, 4U, 3100U, -1);
    CHECK(g.banner_calls == 2);
    close.u.prompt_id = 9U;
    Dash_OnMessage(&s_dash, &close, 3200U);
    feed_record(PROTO_CMD_BANNER_PART, text, 204U, 5U, 3300U, -1);
    CHECK(g.banner_calls == 3);

    /* A disconnect forgets a half banner. */
    feed_record(PROTO_CMD_BANNER_PART, text, 205U, 6U, 4000U, 1);
    Dash_OnDisconnected(&s_dash);
    CHECK(s_dash.banner_asm.active == 0U);
}

/* ---------------------------------------------- status icons and picture */

static int count_sent(uint8_t command)
{
    int n = 0;

    for (int i = 0; i < g.sent_count; i++)
    {
        n += (g.sent[i].command == command) ? 1 : 0;
    }
    return n;
}

static void feed_picture(uint8_t pic_id, uint32_t now)
{
    ProtoMessage msg = simple(PROTO_CMD_PICTURE);

    msg.u.pic_id = pic_id;
    Dash_OnMessage(&s_dash, &msg, now);
}

static void feed_picture_parts(uint8_t pic_id, uint8_t fill, uint16_t from, uint16_t to, uint32_t now)
{
    static uint8_t pixels[PROTO_PICTURE_BYTES];
    ProtoMessage msg = simple(PROTO_CMD_PICTURE_PART);

    memset(pixels, fill, sizeof(pixels));
    for (uint16_t i = from; i < to; i++)
    {
        CHECK(Proto_PicturePart(pixels, pic_id, i, &msg.u.art) == 1);
        Dash_OnMessage(&s_dash, &msg, now);
    }
}

static void test_indicators_and_picture(void)
{
    ProtoMessage ind = simple(PROTO_CMD_INDICATORS);

    reset();
    Dash_SetPictureBuffer(&s_dash, s_pic_buf);
    Dash_OnConnected(&s_dash, 0U);

    /* INDICATORS go to the UI as they are. */
    ind.u.indicators.count = 2U;
    ind.u.indicators.items[0] = (ProtoIndicator){ 12U, PROTO_INDICATOR_RED, 3U, 2U };
    ind.u.indicators.items[1] = (ProtoIndicator){ 13U, PROTO_INDICATOR_GREEN, 0U, 3U };
    CHECK(Dash_Owns(&ind));
    Dash_OnMessage(&s_dash, &ind, 10U);
    CHECK(g.ind_calls == 1 && g.ind.count == 2U && g.ind.items[1].colour == PROTO_INDICATOR_GREEN);

    /* PICTURE 0 while nothing is shown: nothing to tell. A picture: shown when complete. */
    feed_picture(PROTO_NO_PICTURE, 20U);
    CHECK(g.pic_calls == 0);
    feed_picture(4U, 30U);
    feed_picture_parts(4U, 0x5AU, 0U, PROTO_PICTURE_PARTS - 1U, 40U);
    CHECK(g.pic_calls == 0);
    feed_picture_parts(4U, 0x5AU, PROTO_PICTURE_PARTS - 1U, PROTO_PICTURE_PARTS, 50U);
    CHECK(g.pic_calls == 1 && g.pic == s_pic_buf && s_pic_buf[100] == 0x5AU);

    /* The same id again (e.g. after GET_CARDS): not collected again. */
    feed_picture(4U, 60U);
    feed_picture_parts(4U, 0x11U, 0U, PROTO_PICTURE_PARTS, 70U);
    CHECK(g.pic_calls == 1 && s_pic_buf[100] == 0x5AU);

    /* A lost part: GET_CARDS after the retry time; the rest completes it. */
    g.sent_count = 0;
    feed_picture(5U, 1000U);
    feed_picture_parts(5U, 0x22U, 0U, 300U, 1010U);
    feed_picture_parts(5U, 0x22U, 301U, PROTO_PICTURE_PARTS, 1020U);
    Dash_Poll(&s_dash, 1020U + DASH_RETRY_MS - 1U);
    CHECK(count_sent(PROTO_CMD_GET_CARDS) == 0);
    Dash_Poll(&s_dash, 1020U + DASH_RETRY_MS);
    CHECK(count_sent(PROTO_CMD_GET_CARDS) == 1);
    feed_picture(5U, 2100U);
    feed_picture_parts(5U, 0x22U, 300U, 301U, 2110U);
    CHECK(g.pic_calls == 2 && s_pic_buf[100] == 0x22U);
    Dash_Poll(&s_dash, 9000U);
    CHECK(count_sent(PROTO_CMD_GET_CARDS) == 1);  /* nothing missing any more */

    /* A cover's part is not a picture's; PICTURE 0 removes it. */
    feed_picture(6U, 9100U);
    {
        static uint8_t art[PROTO_ART_BYTES];
        ProtoMessage msg = simple(PROTO_CMD_ART_PART);

        CHECK(Proto_ArtPart(art, 6U, 0U, &msg.u.art) == 1);
        Dash_OnMessage(&s_dash, &msg, 9110U);
    }
    CHECK(g.pic_calls == 2);
    feed_picture(PROTO_NO_PICTURE, 9200U);
    CHECK(g.pic_calls == 3 && g.pic == NULL);

    /* No parts for a while: GET_CARDS; the host starts over from part 0.
     * The parts kept so far come again for longer than the retry time;
     * they do not make the client ask again (#68). */
    g.sent_count = 0;
    feed_picture(8U, 10000U);
    feed_picture_parts(8U, 0x44U, 0U, 1000U, 10010U);
    Dash_Poll(&s_dash, 10010U + DASH_RETRY_MS);
    CHECK(count_sent(PROTO_CMD_GET_CARDS) == 1);
    feed_picture(8U, 11100U);
    feed_picture_parts(8U, 0x44U, 0U, 500U, 11800U);
    Dash_Poll(&s_dash, 11800U + DASH_RETRY_MS - 1U);
    feed_picture_parts(8U, 0x44U, 500U, 1000U, 12700U);
    Dash_Poll(&s_dash, 12700U + DASH_RETRY_MS - 1U);
    CHECK(count_sent(PROTO_CMD_GET_CARDS) == 1);
    feed_picture_parts(8U, 0x44U, 1000U, PROTO_PICTURE_PARTS, 13000U);
    CHECK(g.pic_calls == 4 && s_pic_buf[100] == 0x44U);

    /* Without a buffer pictures are ignored. */
    reset();
    Dash_OnConnected(&s_dash, 0U);
    feed_picture(7U, 10U);
    feed_picture_parts(7U, 0x33U, 0U, PROTO_PICTURE_PARTS, 20U);
    CHECK(g.pic_calls == 0);
    g.sent_count = 0;
    Dash_Poll(&s_dash, 5000U);
    CHECK(count_sent(PROTO_CMD_GET_CARDS) == 0);
}

int main(void)
{
    test_owns();
    test_shared_sequence();
    test_connect_asks_for_cards();
    test_cards();
    test_lost_card_part_asks_again();
    test_detail();
    test_detail_failures();
    test_action_and_time();
    test_prompt();
    test_media();
    test_banner();
    test_indicators_and_picture();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
