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

static DashClient s_dash;

static void reset(void)
{
    static const DashCallbacks cb = { fake_send, fake_card, fake_detail, fake_notice, fake_time, NULL };

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
    CHECK(g.sent_count == 1 && g.sent[0].sequence == 40U && shared == 41U);
    CHECK(Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, PROTO_MEDIA_NEXT));
    CHECK(g.sent[1].sequence == 41U && shared == 42U);
}

static void test_connect_asks_for_cards(void)
{
    reset();
    CHECK(!Dash_OpenDetail(&s_dash, 0U, 0, 0U)); /* not connected */
    CHECK(!Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, PROTO_MEDIA_NEXT));
    Dash_OnConnected(&s_dash, 10U);
    CHECK(g.sent_count == 1 && g.sent[0].command == PROTO_CMD_GET_CARDS);
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

    time.u.local_time = 1792593125U;
    Dash_OnMessage(&s_dash, &time, 0xFFFFFC18U); /* the tick wraps */
    CHECK(g.time_calls == 1 && g.time == 1792593125U);
    CHECK(Dash_LocalTime(&s_dash, 0xFFFFFC18U + 999U) == 1792593125U);
    CHECK(Dash_LocalTime(&s_dash, 0xFFFFFC18U + 61000U) == 1792593125U + 61U);

    /* After a disconnect nothing is sent. */
    Dash_OnDisconnected(&s_dash);
    CHECK(!Dash_Action(&s_dash, PROTO_ACTION_MEDIA, 0U, PROTO_MEDIA_NEXT));
    Dash_Poll(&s_dash, 0U);
    CHECK(g.sent_count == 1);
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

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
