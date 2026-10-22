/**
 * @file    test_protocol.c
 * @brief   Unit tests for the protocol codec, version 6 (no test framework).
 */
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

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

static ProtoMessage simple(uint8_t command, uint8_t sequence)
{
    ProtoMessage m;

    memset(&m, 0, sizeof(m));
    m.command = command;
    m.sequence = sequence;
    return m;
}

static ProtoMessage device(uint8_t total, uint8_t position, uint8_t index, const char *name)
{
    ProtoMessage m = simple(PROTO_CMD_DEVICE_LIST, 7U);

    m.u.device.generation = 3U;
    m.u.device.total = total;
    m.u.device.position = position;
    m.u.device.device_index = index;
    m.u.device.flags = PROTO_DEVICE_FLAG_DEFAULT;
    m.u.device.icon = PROTO_ICON_HEADPHONES;
    m.u.device.name_len = (uint8_t)strlen(name);
    memcpy(m.u.device.name, name, m.u.device.name_len);
    return m;
}

/* Recompute the CRC after a test modified a frame on purpose. */
static void reseal(uint8_t frame[PROTO_FRAME_SIZE])
{
    const uint16_t crc = Proto_Crc16(frame, PROTO_CRC_OFFSET);

    frame[PROTO_CRC_OFFSET] = (uint8_t)(crc & 0xFFU);
    frame[PROTO_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8);
}

static void test_crc_check_value(void)
{
    /* Standard check value of CRC-16/CCITT-FALSE. */
    const uint8_t text[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    CHECK(Proto_Crc16(text, sizeof(text)) == 0x29B1U);
    CHECK(Proto_Crc16(text, 0U) == 0xFFFFU);
}

static void test_header_and_golden_frame(void)
{
    /* Byte-exact example documented in protocol.md. */
    static const uint8_t expected_head[] = { 0xA5, 0x07, 0x03, 0x2A, 0x02, 0x05, 0x02 };
    ProtoMessage m = simple(PROTO_CMD_SELECT_DEVICE, 0x2AU);
    uint8_t frame[PROTO_FRAME_SIZE];

    m.u.request.generation = 5U;
    m.u.request.device_index = 2U;
    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    CHECK(memcmp(frame, expected_head, sizeof(expected_head)) == 0);
    for (size_t i = sizeof(expected_head); i < PROTO_CRC_OFFSET; i++)
    {
        CHECK(frame[i] == 0U);
    }
    CHECK(strtoul(PROTO_VERSION_TEXT, NULL, 10) == PROTO_VERSION); /* the text follows the number */
    CHECK(frame[62] == 0x8BU);
    CHECK(frame[63] == 0x2BU);
}

static void test_round_trip_simple(void)
{
    static const struct
    {
        uint8_t command;
        uint8_t value;
    } cases[] = {
        { PROTO_CMD_DEVICE_CHANGED, 5U },    { PROTO_CMD_DEVICE_CHANGED, PROTO_NO_DEVICE },
        { PROTO_CMD_SET_VOLUME, 0U },        { PROTO_CMD_SET_VOLUME, 100U },
        { PROTO_CMD_VOLUME_CHANGED, 42U },   { PROTO_CMD_SET_MUTE, 1U },
        { PROTO_CMD_MUTE_CHANGED, 0U },
    };

    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        ProtoMessage in = simple(cases[i].command, (uint8_t)i);
        ProtoMessage out;
        uint8_t frame[PROTO_FRAME_SIZE];

        /* All one-byte payloads share the union's first byte. */
        in.u.volume = cases[i].value;
        CHECK(Proto_Encode(&in, frame) == PROTO_OK);
        CHECK(frame[4] == 1U);
        CHECK(Proto_Decode(frame, &out) == PROTO_OK);
        CHECK(out.command == cases[i].command);
        CHECK(out.sequence == (uint8_t)i);
        CHECK(out.u.volume == cases[i].value);
    }
}

static void test_round_trip_get_list_and_error(void)
{
    ProtoMessage in = simple(PROTO_CMD_GET_DEVICE_LIST, 255U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 0U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.command == PROTO_CMD_GET_DEVICE_LIST);
    CHECK(out.sequence == 255U);

    in = simple(PROTO_CMD_ERROR, 9U);
    in.u.error.code = PROTO_ERROR_UNKNOWN_DEVICE;
    in.u.error.ref_command = PROTO_CMD_SELECT_DEVICE;
    in.u.error.ref_sequence = 8U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.error.code == PROTO_ERROR_UNKNOWN_DEVICE);
    CHECK(out.u.error.ref_command == PROTO_CMD_SELECT_DEVICE);
    CHECK(out.u.error.ref_sequence == 8U);
}

static void test_round_trip_device(void)
{
    /* 48 bytes of UTF-8 including two-byte Turkish letters. */
    const char *long_name = "Hoparl\xC3\xB6rler (Realtek(R) Audio) \xC3\xA7\xC4\xB1k\xC4\xB1\xC5\x9F 123456";
    ProtoMessage in = device(4U, 3U, 17U, long_name);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(strlen(long_name) == PROTO_MAX_NAME_BYTES);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 7U + PROTO_MAX_NAME_BYTES);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.device.generation == 3U);
    CHECK(out.u.device.total == 4U);
    CHECK(out.u.device.position == 3U);
    CHECK(out.u.device.device_index == 17U);
    CHECK(out.u.device.flags == PROTO_DEVICE_FLAG_DEFAULT);
    CHECK(out.u.device.icon == PROTO_ICON_HEADPHONES);
    CHECK(out.u.device.name_len == PROTO_MAX_NAME_BYTES);
    CHECK(strcmp(out.u.device.name, long_name) == 0);

    /* Empty list. */
    in = device(0U, 0U, PROTO_NO_DEVICE, "");
    in.u.device.flags = 0U;
    in.u.device.icon = PROTO_ICON_GENERIC;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.device.total == 0U);
    CHECK(out.u.device.name[0] == '\0');
}

static void test_encode_rejects(void)
{
    uint8_t frame[PROTO_FRAME_SIZE];
    ProtoMessage m = simple(0x0FU, 0U);

    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_COMMAND);
    CHECK(frame[0] == 0U); /* frame cleared on error */
    CHECK(Proto_Encode(NULL, frame) == PROTO_ERR_ARG);
    CHECK(Proto_Encode(&m, NULL) == PROTO_ERR_ARG);

    m = simple(PROTO_CMD_SET_VOLUME, 0U);
    m.u.volume = 101U;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = simple(PROTO_CMD_SET_MUTE, 0U);
    m.u.muted = 2U;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = simple(PROTO_CMD_SELECT_DEVICE, 0U);
    m.u.request.device_index = PROTO_NO_DEVICE;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = simple(PROTO_CMD_ERROR, 0U);
    m.u.error.code = 0U;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);

    m = device(4U, 4U, 1U, "x"); /* position == total */
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = device(PROTO_MAX_DEVICES + 1U, 0U, 1U, "x");
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = device(1U, 0U, PROTO_NO_DEVICE, "x");
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = device(1U, 0U, 1U, "x");
    m.u.device.icon = PROTO_ICON_COUNT;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = device(1U, 0U, 1U, "x");
    m.u.device.flags = 0x08U; /* bits 3..7 are reserved */
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = device(1U, 0U, 1U, "x");
    m.u.device.name_len = PROTO_MAX_NAME_BYTES + 1U;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = device(1U, 0U, 1U, "ab");
    m.u.device.name[0] = '\0'; /* embedded NUL */
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m = device(0U, 0U, 3U, ""); /* empty list must use PROTO_NO_DEVICE */
    m.u.device.flags = 0U;
    m.u.device.icon = 0U;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
}

static void test_decode_rejects_frame(void)
{
    ProtoMessage m = simple(PROTO_CMD_SET_VOLUME, 0x11U);
    ProtoMessage out;
    uint8_t good[PROTO_FRAME_SIZE];
    uint8_t frame[PROTO_FRAME_SIZE];

    m.u.volume = 50U;
    CHECK(Proto_Encode(&m, good) == PROTO_OK);

    memcpy(frame, good, sizeof(frame));
    frame[0] = 0x5AU;
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_MAGIC);
    CHECK(out.sequence == 0x11U); /* header still reported */

    memcpy(frame, good, sizeof(frame));
    frame[1] = 1U; /* a version 1 frame */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_VERSION);
    CHECK(Proto_ErrorCodeFor(PROTO_ERR_VERSION) == PROTO_ERROR_UNSUPPORTED_VERSION);

    /* Every single-bit error in the frame is caught. */
    for (size_t byte = 2U; byte < PROTO_FRAME_SIZE; byte++)
    {
        for (unsigned bit = 0U; bit < 8U; bit++)
        {
            memcpy(frame, good, sizeof(frame));
            frame[byte] ^= (uint8_t)(1U << bit);
            CHECK(Proto_Decode(frame, &out) == PROTO_ERR_CRC);
        }
    }

    memcpy(frame, good, sizeof(frame));
    frame[4] = PROTO_MAX_PAYLOAD + 1U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_LENGTH);

    memcpy(frame, good, sizeof(frame));
    frame[2] = 0x42U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_COMMAND);
    CHECK(out.command == 0x42U);
    CHECK(Proto_ErrorCodeFor(PROTO_ERR_COMMAND) == PROTO_ERROR_UNKNOWN_COMMAND);

    CHECK(Proto_Decode(NULL, &out) == PROTO_ERR_ARG);
    CHECK(Proto_Decode(good, NULL) == PROTO_ERR_ARG);
}

static void test_decode_rejects_payload(void)
{
    ProtoMessage m = simple(PROTO_CMD_SET_VOLUME, 1U);
    ProtoMessage dev = device(2U, 1U, 4U, "Monitor");
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    m.u.volume = 10U;
    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    frame[5] = 101U; /* volume out of range */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    frame[4] = 2U; /* wrong payload length */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    m = simple(PROTO_CMD_GET_DEVICE_LIST, 1U);
    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    frame[4] = 1U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    CHECK(Proto_Encode(&dev, frame) == PROTO_OK);
    frame[5 + 6] = 8U; /* name_len no longer matches length */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    CHECK(out.u.device.total == 0U); /* fields cleared on error */
    CHECK(out.command == PROTO_CMD_DEVICE_LIST);

    CHECK(Proto_Encode(&dev, frame) == PROTO_OK);
    frame[5 + 7] = 0U; /* NUL inside the name */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    CHECK(Proto_Encode(&dev, frame) == PROTO_OK);
    frame[4] = 6U; /* shorter than the fixed device fields */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    m = simple(PROTO_CMD_ERROR, 1U);
    m.u.error.code = PROTO_ERROR_BUSY;
    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    frame[5] = 0x08U; /* unknown error code */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    CHECK(Proto_ErrorCodeFor(PROTO_ERR_PAYLOAD) == PROTO_ERROR_BAD_PAYLOAD);
    CHECK(Proto_ErrorCodeFor(PROTO_ERR_CRC) == PROTO_ERROR_BAD_FRAME);
}

static void test_round_trip_select_and_requests(void)
{
    ProtoMessage in = simple(PROTO_CMD_SELECT_DEVICE, 1U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    in.u.request.generation = 200U;
    in.u.request.device_index = 254U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 2U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.request.generation == 200U && out.u.request.device_index == 254U);

    in = simple(PROTO_CMD_SET_FAVORITE, 2U);
    in.u.request.generation = 7U;
    in.u.request.device_index = 3U;
    in.u.request.value = 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 3U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.command == PROTO_CMD_SET_FAVORITE);
    CHECK(out.u.request.generation == 7U && out.u.request.device_index == 3U && out.u.request.value == 1U);

    in = simple(PROTO_CMD_MOVE_DEVICE, 3U);
    in.u.request.generation = 7U;
    in.u.request.device_index = 3U;
    in.u.request.value = PROTO_MAX_DEVICES - 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.request.value == PROTO_MAX_DEVICES - 1U);
}

static void test_round_trip_aliases(void)
{
    /* 32 bytes of UTF-8 with Turkish letters. */
    const char *alias = "Oyun kulakl\xC4\xB1\xC4\x9F\xC4\xB1 \xC3\xA7" "al\xC4\xB1\xC5\x9Fma 123";
    ProtoMessage in = simple(PROTO_CMD_DEVICE_ALIAS, 4U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(strlen(alias) == PROTO_MAX_ALIAS_BYTES);
    in.u.alias.generation = 9U;
    in.u.alias.position = 31U;
    in.u.alias.alias_len = (uint8_t)strlen(alias);
    memcpy(in.u.alias.alias, alias, in.u.alias.alias_len);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 3U + PROTO_MAX_ALIAS_BYTES);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.alias.generation == 9U && out.u.alias.position == 31U);
    CHECK(strcmp(out.u.alias.alias, alias) == 0);

    in = simple(PROTO_CMD_SET_ALIAS, 5U);
    in.u.request.generation = 9U;
    in.u.request.device_index = 12U;
    in.u.request.alias_len = 3U;
    memcpy(in.u.request.alias, "DAC", 3U);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 6U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.request.device_index == 12U && out.u.request.alias_len == 3U);
    CHECK(strcmp(out.u.request.alias, "DAC") == 0);

    /* alias_len 0 removes the alias. */
    in.u.request.alias_len = 0U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.request.alias_len == 0U && out.u.request.alias[0] == '\0');
}

static void test_settings(void)
{
    static const uint8_t bytes[PROTO_SETTINGS_BYTES] = { 2, 1, 5, 3, 2, 1, 60, 1, 1, 1, 40, 1 };
    static const uint8_t first_bad[PROTO_SETTINGS_BYTES] = { 3, 2, 6, 4, 3, 2, 61, 2, 2, 2, 101, 3 };
    ProtoMessage in = simple(PROTO_CMD_SET_SETTINGS, 6U);
    ProtoMessage out;
    ProtoSettings d;
    uint8_t frame[PROTO_FRAME_SIZE];

    Proto_DefaultSettings(&d);
    CHECK(Proto_SettingsValid(&d));
    CHECK(d.layout == 0U && d.theme == 0U && d.accent == 0U && d.flipped == 0U);
    CHECK(d.volume_limit_on == 0U && d.volume_limit == PROTO_DEFAULT_VOLUME_LIMIT);
    CHECK(d.startup_screen == 0U); /* Dashboard */

    in.u.settings.layout = 2U;
    in.u.settings.theme = 1U;
    in.u.settings.accent = 5U;
    in.u.settings.grid = 3U;
    in.u.settings.startup_tab = 2U;
    in.u.settings.last_tab = 1U;
    in.u.settings.screen_timeout = 60U;
    in.u.settings.flipped = 1U;
    in.u.settings.language = 1U;
    in.u.settings.volume_limit_on = 1U;
    in.u.settings.volume_limit = 40U;
    in.u.settings.startup_screen = 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == PROTO_SETTINGS_BYTES);
    CHECK(memcmp(&frame[5], bytes, sizeof(bytes)) == 0); /* field order of protocol.md */
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(memcmp(&out.u.settings, &in.u.settings, sizeof(ProtoSettings)) == 0);

    in.command = PROTO_CMD_SETTINGS;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.command == PROTO_CMD_SETTINGS);

    /* Each field one past its range is rejected on decode. */
    for (size_t field = 0U; field < PROTO_SETTINGS_BYTES; field++)
    {
        CHECK(Proto_Encode(&in, frame) == PROTO_OK);
        frame[5U + field] = first_bad[field];
        reseal(frame);
        CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    }
    in.u.settings.screen_timeout = PROTO_MAX_SCREEN_TIMEOUT + 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    CHECK(Proto_SettingsValid(NULL) == 0);

    /* Version 4: startup_screen 2 is the Music tab (ADR-008). */
    in.u.settings.screen_timeout = 1U;
    in.u.settings.startup_screen = 2U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.settings.startup_screen == 2U);
}

static void test_v2_rejects(void)
{
    ProtoMessage m;
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    m = simple(PROTO_CMD_SET_FAVORITE, 0U);
    m.u.request.device_index = 1U;
    m.u.request.value = 2U;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);

    m = simple(PROTO_CMD_MOVE_DEVICE, 0U);
    m.u.request.device_index = 1U;
    m.u.request.value = PROTO_MAX_DEVICES;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);

    m = simple(PROTO_CMD_SET_ALIAS, 0U);
    m.u.request.device_index = 1U;
    m.u.request.alias_len = PROTO_MAX_ALIAS_BYTES + 1U;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m.u.request.alias_len = 2U;
    m.u.request.alias[0] = 'a'; /* alias[1] is NUL */
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);

    m = simple(PROTO_CMD_DEVICE_ALIAS, 0U);
    m.u.alias.alias_len = 0U; /* an announced alias is never empty */
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);
    m.u.alias.alias_len = 1U;
    m.u.alias.alias[0] = 'x';
    m.u.alias.position = PROTO_MAX_DEVICES;
    CHECK(Proto_Encode(&m, frame) == PROTO_ERR_PAYLOAD);

    /* SELECT_DEVICE in the version 1 layout (one byte) is rejected. */
    m = simple(PROTO_CMD_SELECT_DEVICE, 0U);
    m.u.request.device_index = 1U;
    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    frame[4] = 1U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    CHECK(out.command == PROTO_CMD_SELECT_DEVICE);

    /* SET_ALIAS whose alias_len does not match the length. */
    m = simple(PROTO_CMD_SET_ALIAS, 0U);
    m.u.request.device_index = 1U;
    m.u.request.alias_len = 2U;
    memcpy(m.u.request.alias, "ab", 2U);
    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    frame[5 + 2] = 3U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    CHECK(out.u.request.alias_len == 0U); /* fields cleared on error */
}


/* ------------------------------------------------- version 3 (ADR-006) */

static void test_v2_frame_rejected(void)
{
    ProtoMessage m = simple(PROTO_CMD_GET_DEVICE_LIST, 1U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    frame[1] = 2U; /* a version 2 frame (stm32-audio-controller) */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_VERSION);
    CHECK(Proto_ErrorCodeFor(PROTO_ERR_VERSION) == PROTO_ERROR_UNSUPPORTED_VERSION);

    /* A version 2 settings record (11 bytes) is too short now. */
    m = simple(PROTO_CMD_SETTINGS, 2U);
    Proto_DefaultSettings(&m.u.settings);
    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    CHECK(frame[4] == 12U);
    frame[4] = 11U;
    frame[5 + 11] = 0U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
}

static void test_time_and_small_commands(void)
{
    ProtoMessage in;
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    in = simple(PROTO_CMD_TIME, 3U);
    in.u.local_time = 0x6512A0B4UL;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 4U);
    CHECK(frame[5] == 0xB4U && frame[6] == 0xA0U && frame[7] == 0x12U && frame[8] == 0x65U); /* little endian */
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.local_time == 0x6512A0B4UL);
    frame[4] = 3U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    /* SEEK (version 7): u32 seconds, little endian, as TIME. */
    in = simple(PROTO_CMD_SEEK, 8U);
    in.u.seek_s = 0x00010203UL;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[2] == 0x34U && frame[4] == 4U && frame[5] == 0x03U && frame[6] == 0x02U && frame[7] == 0x01U &&
          frame[8] == 0x00U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.command == PROTO_CMD_SEEK && out.u.seek_s == 0x00010203UL);
    frame[4] = 5U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    in = simple(PROTO_CMD_GET_CARDS, 4U);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 0U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.command == PROTO_CMD_GET_CARDS);

    in = simple(PROTO_CMD_CARD_CLEAR, 5U);
    in.u.slot = PROTO_MAX_CARDS - 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.slot == PROTO_MAX_CARDS - 1U);
    in.u.slot = PROTO_MAX_CARDS;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);

    in = simple(PROTO_CMD_PROMPT_CLOSE, 6U);
    in.u.prompt_id = 255U; /* any id */
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.prompt_id == 255U);

    in = simple(PROTO_CMD_OPEN_DETAIL, 7U);
    in.u.open_detail.slot = 1U;
    in.u.open_detail.arg = -1;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[6] == 0xFFU);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.open_detail.slot == 1U && out.u.open_detail.arg == -1);
    in.u.open_detail.arg = 2;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.open_detail.arg = 1;
    in.u.open_detail.slot = PROTO_MAX_CARDS;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
}

static void test_action(void)
{
    ProtoMessage in = simple(PROTO_CMD_ACTION, 8U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    in.u.action.kind = PROTO_ACTION_MEDIA;
    in.u.action.ref = 0U;
    in.u.action.action = PROTO_MEDIA_PLAY_PAUSE;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 3U && frame[5] == 2U && frame[6] == 0U && frame[7] == 2U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.action.kind == PROTO_ACTION_MEDIA && out.u.action.action == PROTO_MEDIA_PLAY_PAUSE);

    in.u.action.action = PROTO_MEDIA_NEXT + 1U; /* no such media key */
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.action.action = PROTO_MEDIA_NEXT;
    in.u.action.ref = 1U; /* media has no ref */
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);

    in.u.action.kind = PROTO_ACTION_CARD;
    in.u.action.ref = 3U;
    in.u.action.action = 200U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    in.u.action.ref = PROTO_MAX_CARDS;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);

    in.u.action.kind = PROTO_ACTION_PROMPT;
    in.u.action.ref = 250U;
    in.u.action.action = 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    in.u.action.action = 0U; /* 0 is never an action */
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.action.action = 1U;
    in.u.action.kind = PROTO_ACTION_KIND_COUNT;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
}

static void test_record_parts_on_the_wire(void)
{
    static const uint8_t cmds[] = { PROTO_CMD_CARD_PART, PROTO_CMD_DETAIL_PART, PROTO_CMD_PROMPT_PART };
    ProtoMessage in;
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    for (size_t c = 0U; c < sizeof(cmds); c++)
    {
        in = simple(cmds[c], (uint8_t)c);
        in.u.part.id = 2U;
        in.u.part.generation = 9U;
        in.u.part.part = 0U;
        in.u.part.parts = 1U;
        in.u.part.len = 5U;
        memcpy(in.u.part.data, "a\x1F" "b\nc", 5U);
        CHECK(Proto_Encode(&in, frame) == PROTO_OK);
        CHECK(frame[4] == 9U);
        CHECK(frame[5] == 2U && frame[6] == 9U && frame[7] == 0U && frame[8] == 1U);
        CHECK(Proto_Decode(frame, &out) == PROTO_OK);
        CHECK(out.u.part.len == 5U && memcmp(out.u.part.data, "a\x1F" "b\nc", 5U) == 0);

        /* An empty record: one part, no data. */
        in.u.part.len = 0U;
        CHECK(Proto_Encode(&in, frame) == PROTO_OK);
        CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.part.len == 0U);

        /* A part that is not the last must be full. */
        in.u.part.parts = 2U;
        in.u.part.len = 10U;
        CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
        /* The last part of several may not be empty. */
        in.u.part.part = 1U;
        in.u.part.len = 0U;
        CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
        /* part >= parts, parts 0 */
        in.u.part.len = 1U;
        in.u.part.part = 2U;
        CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
        in.u.part.part = 0U;
        in.u.part.parts = 0U;
        CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
        /* NUL in the data */
        in.u.part.parts = 1U;
        in.u.part.len = 2U;
        in.u.part.data[0] = 'x';
        in.u.part.data[1] = '\0';
        CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    }

    /* Slot range: cards and details 0..7, prompts any id. */
    in = simple(PROTO_CMD_CARD_PART, 0U);
    in.u.part.id = PROTO_MAX_CARDS;
    in.u.part.parts = 1U;
    in.u.part.len = 1U;
    in.u.part.data[0] = 'x';
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.command = PROTO_CMD_DETAIL_PART;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.command = PROTO_CMD_PROMPT_PART;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);

    /* Part count limits: card 5, detail 39, prompt 20, and the byte limit. */
    in = simple(PROTO_CMD_CARD_PART, 0U);
    in.u.part.parts = PROTO_CARD_MAX_PARTS + 1U;
    in.u.part.part = 0U;
    in.u.part.len = PROTO_PART_DATA_BYTES;
    memset(in.u.part.data, 'y', PROTO_PART_DATA_BYTES);
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.part.parts = PROTO_CARD_MAX_PARTS;
    in.u.part.part = PROTO_CARD_MAX_PARTS - 1U; /* bytes 212..264 > 256 */
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.part.len = (uint8_t)(PROTO_CARD_MAX_BYTES - (PROTO_CARD_MAX_PARTS - 1U) * PROTO_PART_DATA_BYTES);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK); /* exactly 256 bytes */

    in.command = PROTO_CMD_DETAIL_PART;
    in.u.part.parts = PROTO_DETAIL_MAX_PARTS;
    in.u.part.part = PROTO_DETAIL_MAX_PARTS - 1U;
    in.u.part.len = (uint8_t)(PROTO_DETAIL_MAX_BYTES - (PROTO_DETAIL_MAX_PARTS - 1U) * PROTO_PART_DATA_BYTES);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK); /* exactly 2048 bytes */
    in.u.part.len++;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.part.parts = PROTO_DETAIL_MAX_PARTS + 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);

    in.command = PROTO_CMD_PROMPT_PART;
    in.u.part.parts = PROTO_PROMPT_MAX_PARTS;
    in.u.part.part = PROTO_PROMPT_MAX_PARTS - 1U;
    in.u.part.len = (uint8_t)(PROTO_PROMPT_MAX_BYTES - (PROTO_PROMPT_MAX_PARTS - 1U) * PROTO_PART_DATA_BYTES);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK); /* exactly 1024 bytes */
    in.u.part.len++;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);

    /* A frame shorter than the part header. */
    in = simple(PROTO_CMD_CARD_PART, 0U);
    in.u.part.parts = 1U;
    in.u.part.len = 1U;
    in.u.part.data[0] = 'x';
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[4] = 3U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    CHECK(Proto_RecordMaxBytes(PROTO_CMD_CARD_PART) == PROTO_CARD_MAX_BYTES);
    CHECK(Proto_RecordMaxBytes(PROTO_CMD_DETAIL_PART) == PROTO_DETAIL_MAX_BYTES);
    CHECK(Proto_RecordMaxBytes(PROTO_CMD_PROMPT_PART) == PROTO_PROMPT_MAX_BYTES);
    CHECK(Proto_RecordMaxBytes(PROTO_CMD_TIME) == 0U);
}

/* Split @p record with Proto_RecordPart, send every part through
 * Encode/Decode in @p order, and feed the assembler. */
static ProtoAsmResult send_record(ProtoAssembler *a, uint8_t command, const char *record, uint8_t id,
                                  uint8_t generation, const uint8_t *order)
{
    const uint16_t length = (uint16_t)strlen(record);
    const uint8_t parts = Proto_RecordPartCount(length);
    ProtoAsmResult result = PROTO_ASM_PARTIAL;

    for (uint8_t i = 0U; i < parts; i++)
    {
        const uint8_t part = (order != NULL) ? order[i] : i;
        ProtoMessage in = simple(command, i);
        ProtoMessage out;
        uint8_t frame[PROTO_FRAME_SIZE];

        CHECK(Proto_RecordPart((const uint8_t *)record, length, id, generation, part, &in.u.part) == 1);
        CHECK(Proto_Encode(&in, frame) == PROTO_OK);
        CHECK(Proto_Decode(frame, &out) == PROTO_OK);
        result = Proto_AssemblerAdd(a, &out.u.part);
        CHECK(result == (((i + 1U) == parts) ? PROTO_ASM_COMPLETE : PROTO_ASM_PARTIAL));
    }
    return result;
}

static void test_record_split_and_assemble(void)
{
    static uint8_t detail_buf[PROTO_DETAIL_MAX_BYTES + 1U];
    static uint8_t card_buf[PROTO_CARD_MAX_BYTES + 1U];
    static char big[PROTO_DETAIL_MAX_BYTES + 1U];
    static const uint8_t reversed[3] = { 2U, 0U, 1U };
    const char *card = "3\x1F" "Hava\x1F" "\n14\xC2\xB0 Par\xC3\xA7" "al\xC4\xB1 bulutlu\nE 17\xC2\xB0 / 8\xC2\xB0";
    ProtoAssembler a;
    ProtoRecordPart p;

    CHECK(Proto_RecordPartCount(0U) == 1U);
    CHECK(Proto_RecordPartCount(1U) == 1U);
    CHECK(Proto_RecordPartCount(53U) == 1U);
    CHECK(Proto_RecordPartCount(54U) == 2U);
    CHECK(Proto_RecordPartCount(PROTO_DETAIL_MAX_BYTES) == PROTO_DETAIL_MAX_PARTS);
    CHECK(Proto_RecordPartCount(PROTO_PROMPT_MAX_BYTES) == PROTO_PROMPT_MAX_PARTS);
    CHECK(Proto_RecordPartCount(PROTO_CARD_MAX_BYTES) == PROTO_CARD_MAX_PARTS);

    /* A short card in one part. */
    Proto_AssemblerInit(&a, card_buf, PROTO_CARD_MAX_BYTES);
    CHECK(send_record(&a, PROTO_CMD_CARD_PART, card, 0U, 1U, NULL) == PROTO_ASM_COMPLETE);
    CHECK(a.length == strlen(card) && strcmp((const char *)card_buf, card) == 0);

    /* A detail of the largest size, in order and in another order. */
    for (size_t i = 0U; i < PROTO_DETAIL_MAX_BYTES; i++)
    {
        big[i] = (char)('a' + (i % 26U));
    }
    big[PROTO_DETAIL_MAX_BYTES] = '\0';
    Proto_AssemblerInit(&a, detail_buf, PROTO_DETAIL_MAX_BYTES);
    CHECK(send_record(&a, PROTO_CMD_DETAIL_PART, big, 1U, 7U, NULL) == PROTO_ASM_COMPLETE);
    CHECK(a.length == PROTO_DETAIL_MAX_BYTES && memcmp(detail_buf, big, PROTO_DETAIL_MAX_BYTES) == 0);
    CHECK(detail_buf[PROTO_DETAIL_MAX_BYTES] == '\0');

    big[150] = '\0'; /* 3 parts */
    CHECK(send_record(&a, PROTO_CMD_DETAIL_PART, big, 1U, 8U, reversed) == PROTO_ASM_COMPLETE);
    CHECK(a.length == 150U && strcmp((const char *)detail_buf, big) == 0);

    /* A duplicate part is ignored; the record still completes. */
    CHECK(Proto_RecordPart((const uint8_t *)big, 150U, 1U, 9U, 0U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_PARTIAL);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_DUPLICATE);
    CHECK(Proto_RecordPart((const uint8_t *)big, 150U, 1U, 9U, 1U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_PARTIAL);
    CHECK(Proto_RecordPart((const uint8_t *)big, 150U, 1U, 9U, 2U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_COMPLETE);

    /* A part of another generation discards the partial record. */
    CHECK(Proto_RecordPart((const uint8_t *)big, 150U, 1U, 10U, 0U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_PARTIAL);
    CHECK(Proto_RecordPart((const uint8_t *)big, 150U, 1U, 10U, 1U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_PARTIAL);
    CHECK(Proto_RecordPart((const uint8_t *)"new", 3U, 1U, 11U, 0U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_COMPLETE);
    CHECK(strcmp((const char *)detail_buf, "new") == 0);

    /* ... and so does a part of another id. */
    CHECK(Proto_RecordPart((const uint8_t *)big, 150U, 1U, 12U, 0U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_PARTIAL);
    CHECK(Proto_RecordPart((const uint8_t *)big, 150U, 2U, 12U, 1U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_PARTIAL);
    CHECK(a.id == 2U && a.received == 2U);

    /* A record larger than the buffer is dropped. */
    big[PROTO_DETAIL_MAX_BYTES] = '\0';
    big[150] = 'x';
    Proto_AssemblerInit(&a, card_buf, PROTO_CARD_MAX_BYTES);
    CHECK(Proto_RecordPart((const uint8_t *)big, 300U, 0U, 1U, 5U, &p) == 1);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_OVERFLOW);
    CHECK(a.active == 0U);

    /* An empty record. */
    CHECK(Proto_RecordPart(NULL, 0U, 0U, 3U, 0U, &p) == 1);
    CHECK(p.parts == 1U && p.len == 0U);
    CHECK(Proto_AssemblerAdd(&a, &p) == PROTO_ASM_COMPLETE);
    CHECK(a.length == 0U && card_buf[0] == '\0');

    /* Sender-side errors. */
    CHECK(Proto_RecordPart((const uint8_t *)"ab", 2U, 0U, 0U, 1U, &p) == 0); /* no part 1 */
    CHECK(Proto_RecordPart((const uint8_t *)"a\0b", 3U, 0U, 0U, 0U, &p) == 0); /* NUL */
    CHECK(Proto_RecordPart((const uint8_t *)"ab", 2U, 0U, 0U, 0U, NULL) == 0);
    CHECK(Proto_AssemblerAdd(NULL, &p) == PROTO_ASM_OVERFLOW);
}

/* ------------------------------------------------- version 4 (ADR-008) */

static void test_v3_frame_rejected(void)
{
    ProtoMessage m = simple(PROTO_CMD_GET_CARDS, 1U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    CHECK(frame[1] == PROTO_VERSION);
    frame[1] = 3U; /* a version 3 frame (v0.9.0-beta.2 and before) */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_VERSION);
}

static void test_media_state(void)
{
    ProtoMessage in = simple(PROTO_CMD_MEDIA_STATE, 8U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    in.u.media.status = PROTO_MEDIA_STATUS_PLAYING;
    in.u.media.controls = PROTO_MEDIA_CAN_PREVIOUS | PROTO_MEDIA_CAN_PLAY_PAUSE | PROTO_MEDIA_CAN_NEXT;
    in.u.media.art_id = 7U;
    in.u.media.position = 0x00010203UL;
    in.u.media.duration = 0x00020304UL;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[2] == 0x27U && frame[4] == 11U);
    CHECK(frame[5] == 2U && frame[6] == 7U && frame[7] == 7U);
    CHECK(frame[8] == 0x03U && frame[9] == 0x02U && frame[10] == 0x01U && frame[11] == 0x00U); /* little endian */
    CHECK(frame[12] == 0x04U && frame[13] == 0x03U && frame[14] == 0x02U && frame[15] == 0x00U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(memcmp(&out.u.media, &in.u.media, sizeof(ProtoMediaState)) == 0);

    /* No session, nothing known: all zero is valid. */
    memset(&in.u.media, 0, sizeof(in.u.media));
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.media.status == PROTO_MEDIA_STATUS_NONE);
    /* Unknown duration: any position. */
    in.u.media.position = 500U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);

    /* Out of range: status, controls, position beyond a known duration. */
    in.u.media.duration = 499U;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.media.duration = 500U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    in.u.media.status = PROTO_MEDIA_STATUS_COUNT;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.media.status = PROTO_MEDIA_STATUS_PAUSED;
    in.u.media.controls = PROTO_MEDIA_CAN_SEEK; /* version 7 */
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    in.u.media.controls = 0x10U;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.media.controls = 0U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[5] = 4U; /* status */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[4] = 10U; /* short payload */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    in = simple(PROTO_CMD_GET_MEDIA, 9U);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[2] == 0x33U && frame[4] == 0U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.command == PROTO_CMD_GET_MEDIA);
    frame[4] = 1U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
}

static void test_media_record(void)
{
    static const char text[] = "Song\x1F" "Artist\x1F" "Album\x1F" "Spotify";
    static uint8_t big[PROTO_MEDIA_MAX_BYTES + 1U];
    uint8_t buffer[PROTO_MEDIA_MAX_BYTES + 1U];
    ProtoAssembler a;
    ProtoMessage in = simple(PROTO_CMD_MEDIA_PART, 1U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(Proto_RecordMaxBytes(PROTO_CMD_MEDIA_PART) == PROTO_MEDIA_MAX_BYTES);
    CHECK(Proto_RecordPartCount(PROTO_MEDIA_MAX_BYTES) == PROTO_MEDIA_MAX_PARTS);

    CHECK(Proto_RecordPart((const uint8_t *)text, (uint16_t)strlen(text), 0U, 4U, 0U, &in.u.part));
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[2] == 0x26U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    Proto_AssemblerInit(&a, buffer, PROTO_MEDIA_MAX_BYTES);
    CHECK(Proto_AssemblerAdd(&a, &out.u.part) == PROTO_ASM_COMPLETE);
    CHECK(strcmp((const char *)buffer, text) == 0);

    /* One media record: id must be 0. */
    in.u.part.id = 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.part.id = 0U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[5] = 1U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    /* A full record fits in 10 parts; a byte more does not. */
    memset(big, 'x', sizeof(big));
    for (uint8_t part = 0U; part < PROTO_MEDIA_MAX_PARTS; part++)
    {
        CHECK(Proto_RecordPart(big, PROTO_MEDIA_MAX_BYTES, 0U, 5U, part, &in.u.part));
        CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    }
    CHECK(Proto_RecordPart(big, PROTO_MEDIA_MAX_BYTES + 1U, 0U, 6U, PROTO_MEDIA_MAX_PARTS - 1U, &in.u.part));
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
}

static void test_art_parts(void)
{
    static uint8_t art[PROTO_ART_BYTES];
    static uint8_t got[PROTO_ART_BYTES];
    ProtoArtAssembler a;
    ProtoMessage in = simple(PROTO_CMD_ART_PART, 2U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];
    ProtoAsmResult r = PROTO_ASM_PARTIAL;

    CHECK(PROTO_ART_BYTES == 28800U);
    CHECK(PROTO_ART_DATA_BYTES == 54U);
    CHECK(PROTO_ART_PARTS == 534U);
    CHECK(Proto_ArtPartBytes(0U) == 54U);
    CHECK(Proto_ArtPartBytes(PROTO_ART_PARTS - 1U) == 18U);
    CHECK(Proto_ArtPartBytes(PROTO_ART_PARTS) == 0U);

    for (uint32_t i = 0U; i < PROTO_ART_BYTES; i++)
    {
        art[i] = (uint8_t)((i * 7U) + (i >> 8));
    }

    /* On the wire: art_id, index (u16 little endian), data. */
    CHECK(Proto_ArtPart(art, 9U, 0x0102U, &in.u.art));
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[2] == 0x28U && frame[4] == 57U);
    CHECK(frame[5] == 9U && frame[6] == 0x02U && frame[7] == 0x01U);
    CHECK(memcmp(&frame[8], &art[0x0102U * 54U], 54U) == 0);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(out.u.art.art_id == 9U && out.u.art.index == 0x0102U && out.u.art.len == 54U);

    CHECK(Proto_ArtPart(art, 9U, PROTO_ART_PARTS - 1U, &in.u.art));
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[4] == 3U + 18U);

    /* Rejected: art_id 0, index beyond the cover, a short part. */
    CHECK(!Proto_ArtPart(art, PROTO_NO_ART, 0U, &in.u.art));
    CHECK(!Proto_ArtPart(art, 9U, PROTO_ART_PARTS, &in.u.art));
    CHECK(Proto_ArtPart(art, 9U, 3U, &in.u.art));
    in.u.art.len = 53U;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.art.len = 54U;
    in.u.art.index = PROTO_ART_PARTS;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.art.index = 3U;
    in.u.art.art_id = PROTO_NO_ART;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.art.art_id = 9U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[4] = 50U; /* the frame ends early */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[6] = 0x16U; /* index 534 */
    frame[7] = 0x02U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    /* Assembler: nothing collected before Start; parts of another id dropped. */
    Proto_ArtAssemblerInit(&a, got);
    CHECK(Proto_ArtPart(art, 9U, 0U, &in.u.art));
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_OVERFLOW);
    Proto_ArtAssemblerStart(&a, 9U);
    CHECK(Proto_ArtPart(art, 8U, 0U, &in.u.art));
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_OVERFLOW);
    CHECK(a.count == 0U);

    /* Backwards, with a repeat; complete only with the last missing part. */
    for (uint16_t i = PROTO_ART_PARTS; i > 1U; i--)
    {
        CHECK(Proto_ArtPart(art, 9U, (uint16_t)(i - 1U), &in.u.art));
        r = Proto_ArtAssemblerAdd(&a, &in.u.art);
        if (r != PROTO_ASM_PARTIAL)
        {
            break;
        }
    }
    CHECK(r == PROTO_ASM_PARTIAL);
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_DUPLICATE);
    /* The same id again keeps what was collected. */
    Proto_ArtAssemblerStart(&a, 9U);
    CHECK(a.count == PROTO_ART_PARTS - 1U);
    CHECK(Proto_ArtPart(art, 9U, 0U, &in.u.art));
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_COMPLETE);
    CHECK(memcmp(got, art, sizeof(art)) == 0);
    /* After completion nothing is collected until the next Start. */
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_OVERFLOW);

    /* A new id forgets a partial cover. */
    Proto_ArtAssemblerStart(&a, 10U);
    CHECK(Proto_ArtPart(art, 10U, 5U, &in.u.art));
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_PARTIAL);
    Proto_ArtAssemblerStart(&a, 11U);
    CHECK(a.count == 0U && a.art_id == 11U);
    Proto_ArtAssemblerStart(&a, PROTO_NO_ART);
    CHECK(Proto_ArtPart(art, 11U, 5U, &in.u.art));
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_OVERFLOW);
}

/* ------------------------------------------------- version 5 (ADR-009) */

static void test_v4_frame_rejected(void)
{
    ProtoMessage m = simple(PROTO_CMD_GET_MEDIA, 1U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    CHECK(frame[1] == PROTO_VERSION);
    frame[1] = 4U; /* a version 4 frame (media, #30-#32) */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_VERSION);
}

static void test_banner_record(void)
{
    static const char text[] = "12\x1F" "WhatsApp\x1F" "5\x1F" "2\n" "Ay\xC5\x9F" "e\n" "Ak\xC5\x9F" "am geliyor musun? Saat yedide kapÄ±nÄ±n Ã¶nÃ¼nde olalÄ±m.";
    static uint8_t big[PROTO_BANNER_MAX_BYTES + 1U];
    uint8_t buffer[PROTO_BANNER_MAX_BYTES + 1U];
    ProtoAssembler a;
    ProtoMessage in = simple(PROTO_CMD_BANNER_PART, 2U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(Proto_RecordMaxBytes(PROTO_CMD_BANNER_PART) == PROTO_BANNER_MAX_BYTES);
    CHECK(Proto_RecordPartCount(PROTO_BANNER_MAX_BYTES) == PROTO_BANNER_MAX_PARTS);

    /* Banner ids span 0-255, like prompt ids (not card slots). */
    CHECK(Proto_RecordPart((const uint8_t *)text, (uint16_t)strlen(text), 200U, 7U, 0U, &in.u.part));
    CHECK(in.u.part.parts == 2U);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[2] == 0x29U && frame[5] == 200U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.command == PROTO_CMD_BANNER_PART);
    Proto_AssemblerInit(&a, buffer, PROTO_BANNER_MAX_BYTES);
    CHECK(Proto_AssemblerAdd(&a, &out.u.part) == PROTO_ASM_PARTIAL);
    CHECK(Proto_RecordPart((const uint8_t *)text, (uint16_t)strlen(text), 200U, 7U, 1U, &in.u.part));
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK);
    CHECK(Proto_AssemblerAdd(&a, &out.u.part) == PROTO_ASM_COMPLETE);
    CHECK(strcmp((const char *)buffer, text) == 0);

    /* A full record fits in 5 parts; a byte more does not. */
    memset(big, 'x', sizeof(big));
    for (uint8_t part = 0U; part < PROTO_BANNER_MAX_PARTS; part++)
    {
        CHECK(Proto_RecordPart(big, PROTO_BANNER_MAX_BYTES, 255U, 8U, part, &in.u.part));
        CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    }
    CHECK(Proto_RecordPart(big, PROTO_BANNER_MAX_BYTES + 1U, 255U, 9U, PROTO_BANNER_MAX_PARTS - 1U, &in.u.part));
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);

    /* parts above 5 on the wire is a bad payload. */
    CHECK(Proto_RecordPart(big, 53U, 1U, 1U, 0U, &in.u.part));
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[8] = (uint8_t)(PROTO_BANNER_MAX_PARTS + 1U);
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
}

/* ------------------------------------------------- version 6 (ADR-010) */

static void test_v6_frame_rejected(void)
{
    ProtoMessage m = simple(PROTO_CMD_GET_CARDS, 1U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    CHECK(Proto_Encode(&m, frame) == PROTO_OK);
    CHECK(frame[1] == 7U);
    frame[1] = 6U; /* a version 6 frame (200 x 92 picture, #47) */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_VERSION);
}

static void test_indicators(void)
{
    ProtoMessage in = simple(PROTO_CMD_INDICATORS, 3U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];

    /* Bell: 3 toasts, unseen (red), opens slot 2; robot: normal, slot 3. */
    in.u.indicators.count = 2U;
    in.u.indicators.items[0] = (ProtoIndicator){ 12U, PROTO_INDICATOR_RED, 3U, 2U };
    in.u.indicators.items[1] = (ProtoIndicator){ 13U, PROTO_INDICATOR_NORMAL, 0U, 3U };
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    CHECK(frame[2] == 0x2AU && frame[4] == 9U);
    CHECK(frame[5] == 2U && frame[6] == 12U && frame[7] == 1U && frame[8] == 3U && frame[9] == 2U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.command == PROTO_CMD_INDICATORS);
    CHECK(out.u.indicators.count == 2U && out.u.indicators.items[1].icon == 13U &&
          out.u.indicators.items[1].slot == 3U && out.u.indicators.items[0].number == 3U);

    /* None at all; four; PROTO_NO_SLOT. */
    in.u.indicators.count = 0U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK && frame[4] == 1U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.indicators.count == 0U);
    in.u.indicators.count = PROTO_MAX_INDICATORS;
    for (uint8_t i = 0U; i < PROTO_MAX_INDICATORS; i++)
    {
        in.u.indicators.items[i] = (ProtoIndicator){ i, PROTO_INDICATOR_GREEN, 255U, PROTO_NO_SLOT };
    }
    CHECK(Proto_Encode(&in, frame) == PROTO_OK && frame[4] == 17U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.indicators.items[3].slot == PROTO_NO_SLOT);

    /* Bad: five; a colour out of range; slot 8; a length not matching count. */
    in.u.indicators.count = PROTO_MAX_INDICATORS + 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.indicators.count = 1U;
    in.u.indicators.items[0].colour = PROTO_INDICATOR_COLOUR_COUNT;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.indicators.items[0].colour = PROTO_INDICATOR_YELLOW;
    in.u.indicators.items[0].slot = PROTO_MAX_CARDS;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    in.u.indicators.items[0].slot = 1U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK);
    frame[4] = 6U; /* one entry, six bytes */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
    frame[4] = 5U;
    frame[7] = 9U; /* colour */
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);
}

static void test_picture(void)
{
    static uint8_t pic[PROTO_PICTURE_BYTES];
    static uint8_t got[PROTO_PICTURE_BYTES];
    ProtoArtAssembler a;
    ProtoMessage in = simple(PROTO_CMD_PICTURE, 4U);
    ProtoMessage out;
    uint8_t frame[PROTO_FRAME_SIZE];
    ProtoAsmResult r = PROTO_ASM_PARTIAL;

    CHECK(PROTO_PICTURE_BYTES == 261120U && PROTO_PICTURE_PARTS == 4836U);
    CHECK(Proto_PicturePartBytes(0U) == 54U && Proto_PicturePartBytes(4835U) == 30U &&
          Proto_PicturePartBytes(4836U) == 0U);

    /* PICTURE: one byte, any id (0 = none). */
    in.u.pic_id = 7U;
    CHECK(Proto_Encode(&in, frame) == PROTO_OK && frame[2] == 0x2CU && frame[4] == 1U && frame[5] == 7U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.pic_id == 7U);
    frame[4] = 2U;
    reseal(frame);
    CHECK(Proto_Decode(frame, &out) == PROTO_ERR_PAYLOAD);

    /* PICTURE_PART: the last part has 30 bytes; a cover's last length is wrong here. */
    for (uint32_t i = 0U; i < sizeof(pic); i++)
    {
        pic[i] = (uint8_t)(i * 7U);
    }
    in = simple(PROTO_CMD_PICTURE_PART, 5U);
    CHECK(Proto_PicturePart(pic, 7U, 4835U, &in.u.art) && in.u.art.len == 30U);
    CHECK(Proto_Encode(&in, frame) == PROTO_OK && frame[2] == 0x2BU && frame[4] == 33U);
    CHECK(Proto_Decode(frame, &out) == PROTO_OK && out.u.art.index == 4835U && out.u.art.len == 30U);
    CHECK(!Proto_PicturePart(pic, PROTO_NO_PICTURE, 0U, &in.u.art));
    CHECK(!Proto_PicturePart(pic, 7U, 4836U, &in.u.art));
    CHECK(Proto_ArtPart(pic, 7U, 533U, &in.u.art)); /* a cover's last part: 18 bytes */
    in.command = PROTO_CMD_PICTURE_PART;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);
    /* Index 534..4835 only exist for the picture. */
    CHECK(Proto_PicturePart(pic, 7U, 600U, &in.u.art));
    in.command = PROTO_CMD_ART_PART;
    CHECK(Proto_Encode(&in, frame) == PROTO_ERR_PAYLOAD);

    /* The assembler collects all 4836 parts, in any order. */
    Proto_PictureAssemblerInit(&a, got);
    Proto_ArtAssemblerStart(&a, 7U);
    for (uint16_t i = PROTO_PICTURE_PARTS; i > 0U; i--)
    {
        CHECK(Proto_PicturePart(pic, 7U, (uint16_t)(i - 1U), &in.u.art));
        r = Proto_ArtAssemblerAdd(&a, &in.u.art);
        if ((i > 1U) && (r != PROTO_ASM_PARTIAL))
        {
            break;
        }
    }
    CHECK(r == PROTO_ASM_COMPLETE);
    CHECK(memcmp(got, pic, sizeof(pic)) == 0);
    /* A cover's part is not a picture part. */
    Proto_ArtAssemblerStart(&a, 8U);
    CHECK(Proto_ArtPart(pic, 8U, 533U, &in.u.art));
    CHECK(Proto_ArtAssemblerAdd(&a, &in.u.art) == PROTO_ASM_OVERFLOW);
}

int main(void)
{
    test_crc_check_value();
    test_header_and_golden_frame();
    test_round_trip_simple();
    test_round_trip_get_list_and_error();
    test_round_trip_device();
    test_encode_rejects();
    test_decode_rejects_frame();
    test_decode_rejects_payload();
    test_round_trip_select_and_requests();
    test_round_trip_aliases();
    test_settings();
    test_v2_rejects();
    test_v2_frame_rejected();
    test_time_and_small_commands();
    test_action();
    test_record_parts_on_the_wire();
    test_record_split_and_assemble();
    test_v3_frame_rejected();
    test_media_state();
    test_media_record();
    test_art_parts();
    test_v4_frame_rejected();
    test_banner_record();
    test_v6_frame_rejected();
    test_indicators();
    test_picture();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
