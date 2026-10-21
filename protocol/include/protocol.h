/**
 * @file    protocol.h
 * @brief   STM32 <-> Windows host protocol, version 4 (see protocol/protocol.md).
 *
 * Portable C99 codec shared by the firmware and the host. Every message is
 * one fixed-size frame of PROTO_FRAME_SIZE bytes:
 *
 *     offset  size  field
 *     0       1     magic      (PROTO_MAGIC)
 *     1       1     version    (PROTO_VERSION)
 *     2       1     command    (ProtoCommand)
 *     3       1     sequence   (sender's counter, wraps)
 *     4       1     length     (payload bytes, 0 .. PROTO_MAX_PAYLOAD)
 *     5       len   payload
 *     ..      ..    zero padding
 *     62      2     CRC-16/CCITT-FALSE of bytes 0..61, little endian
 *
 * No dynamic allocation, no platform headers. Decoding never reads or
 * writes outside the frame and the output structure.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_FRAME_SIZE   64U
#define PROTO_MAGIC        0xA5U
#define PROTO_VERSION      4U
#define PROTO_HEADER_SIZE  5U
#define PROTO_CRC_OFFSET   62U
#define PROTO_MAX_PAYLOAD  (PROTO_CRC_OFFSET - PROTO_HEADER_SIZE) /* 57 */

/** Largest number of devices in one device list. */
#define PROTO_MAX_DEVICES      32U
/** Largest device name, in bytes of UTF-8, without terminator. */
#define PROTO_MAX_NAME_BYTES   48U
/** device_index value meaning "no device". */
#define PROTO_NO_DEVICE        0xFFU
/** Largest volume value (percent). */
#define PROTO_MAX_VOLUME       100U
/** Largest device alias, in bytes of UTF-8, without terminator. */
#define PROTO_MAX_ALIAS_BYTES  32U

/* Dashboard records (ADR-006): text sent in parts. */
/** Bytes of the part header: id, generation, part, parts. */
#define PROTO_PART_HEADER_BYTES 4U
/** Record bytes in one part frame (57 - 4). */
#define PROTO_PART_DATA_BYTES   (PROTO_MAX_PAYLOAD - PROTO_PART_HEADER_BYTES) /* 53 */
/** Card slots; v1 shows 0..3. */
#define PROTO_MAX_CARDS         8U
/** Largest record of each kind, in bytes of UTF-8, without terminator. */
#define PROTO_CARD_MAX_BYTES    256U
#define PROTO_DETAIL_MAX_BYTES  2048U
#define PROTO_PROMPT_MAX_BYTES  1024U
#define PROTO_MEDIA_MAX_BYTES   512U
/** Most parts of a record of each kind (ceil(max / 53)). */
#define PROTO_CARD_MAX_PARTS    5U
#define PROTO_DETAIL_MAX_PARTS  39U
#define PROTO_PROMPT_MAX_PARTS  20U
#define PROTO_MEDIA_MAX_PARTS   10U
/** Record text: lines end with '\n', fields are separated by this byte. */
#define PROTO_FIELD_SEPARATOR   0x1FU

/* Now playing (ADR-008, version 4). */
/** MEDIA_STATE status. */
#define PROTO_MEDIA_STATUS_NONE    0U /**< no media session */
#define PROTO_MEDIA_STATUS_STOPPED 1U
#define PROTO_MEDIA_STATUS_PLAYING 2U
#define PROTO_MEDIA_STATUS_PAUSED  3U
#define PROTO_MEDIA_STATUS_COUNT   4U
/** MEDIA_STATE controls: the buttons the session takes. */
#define PROTO_MEDIA_CAN_PREVIOUS   0x01U
#define PROTO_MEDIA_CAN_PLAY_PAUSE 0x02U
#define PROTO_MEDIA_CAN_NEXT       0x04U
#define PROTO_MEDIA_CONTROLS_MASK  0x07U
/** MEDIA_STATE art_id meaning "no cover". */
#define PROTO_NO_ART               0U
/** Cover: square, RGB565 little endian, row by row from the top left. */
#define PROTO_ART_SIZE             120U
#define PROTO_ART_BYTES            (PROTO_ART_SIZE * PROTO_ART_SIZE * 2U) /* 28800 */
/** ART_PART header bytes (art_id, index u16) and data bytes per part. */
#define PROTO_ART_HEADER_BYTES     3U
#define PROTO_ART_DATA_BYTES       (PROTO_MAX_PAYLOAD - PROTO_ART_HEADER_BYTES) /* 54 */
/** Parts of one cover (ceil(28800 / 54)); the last holds 18 bytes. */
#define PROTO_ART_PARTS            ((PROTO_ART_BYTES + PROTO_ART_DATA_BYTES - 1U) / PROTO_ART_DATA_BYTES) /* 534 */

typedef enum
{
    PROTO_CMD_GET_DEVICE_LIST = 0x01, /**< STM32 -> host */
    PROTO_CMD_DEVICE_LIST     = 0x02, /**< host -> STM32, one device per frame */
    PROTO_CMD_SELECT_DEVICE   = 0x03, /**< STM32 -> host */
    PROTO_CMD_DEVICE_CHANGED  = 0x04, /**< host -> STM32 */
    PROTO_CMD_SET_VOLUME      = 0x05, /**< STM32 -> host */
    PROTO_CMD_VOLUME_CHANGED  = 0x06, /**< host -> STM32 */
    PROTO_CMD_SET_MUTE        = 0x07, /**< STM32 -> host */
    PROTO_CMD_MUTE_CHANGED    = 0x08, /**< host -> STM32 */
    PROTO_CMD_DEVICE_ALIAS    = 0x09, /**< host -> STM32, part of a device list */
    PROTO_CMD_SET_ALIAS       = 0x0A, /**< STM32 -> host */
    PROTO_CMD_SET_FAVORITE    = 0x0B, /**< STM32 -> host */
    PROTO_CMD_MOVE_DEVICE     = 0x0C, /**< STM32 -> host */
    PROTO_CMD_SETTINGS        = 0x0D, /**< host -> STM32 */
    PROTO_CMD_SET_SETTINGS    = 0x0E, /**< STM32 -> host */
    PROTO_CMD_TIME            = 0x20, /**< host -> STM32 */
    PROTO_CMD_CARD_PART       = 0x21, /**< host -> STM32, part of a card record */
    PROTO_CMD_CARD_CLEAR      = 0x22, /**< host -> STM32 */
    PROTO_CMD_DETAIL_PART     = 0x23, /**< host -> STM32, part of a detail record */
    PROTO_CMD_PROMPT_PART     = 0x24, /**< host -> STM32, part of a prompt record */
    PROTO_CMD_PROMPT_CLOSE    = 0x25, /**< host -> STM32 */
    PROTO_CMD_MEDIA_PART      = 0x26, /**< host -> STM32, part of the media record (v4) */
    PROTO_CMD_MEDIA_STATE     = 0x27, /**< host -> STM32 (v4) */
    PROTO_CMD_ART_PART        = 0x28, /**< host -> STM32, part of a cover (v4) */
    PROTO_CMD_GET_CARDS       = 0x30, /**< STM32 -> host */
    PROTO_CMD_OPEN_DETAIL     = 0x31, /**< STM32 -> host */
    PROTO_CMD_ACTION          = 0x32, /**< STM32 -> host */
    PROTO_CMD_GET_MEDIA       = 0x33, /**< STM32 -> host (v4) */
    PROTO_CMD_ERROR           = 0x7F  /**< either direction */
} ProtoCommand;

/** Device icon hint; the firmware picks the picture. */
typedef enum
{
    PROTO_ICON_GENERIC    = 0,
    PROTO_ICON_SPEAKERS   = 1,
    PROTO_ICON_HEADPHONES = 2,
    PROTO_ICON_HEADSET    = 3,
    PROTO_ICON_DISPLAY    = 4,
    PROTO_ICON_DIGITAL    = 5
} ProtoIcon;
#define PROTO_ICON_COUNT 6U

/** DEVICE_LIST flags. */
#define PROTO_DEVICE_FLAG_DEFAULT  0x01U /**< current default playback device */
#define PROTO_DEVICE_FLAG_FAVORITE 0x02U /**< marked as favorite by the user */
#define PROTO_DEVICE_FLAG_ALIAS    0x04U /**< a DEVICE_ALIAS frame follows for this entry */
#define PROTO_DEVICE_FLAGS_MASK    0x07U

/** Settings record field values (SETTINGS, SET_SETTINGS). */
#define PROTO_LAYOUT_COUNT       3U  /**< 0 cards, 1 list, 2 large tile */
#define PROTO_THEME_COUNT        2U  /**< 0 dark, 1 light */
#define PROTO_ACCENT_COUNT       6U  /**< 0 orange .. 5 yellow */
#define PROTO_GRID_COUNT         4U  /**< 0 2x2, 1 2x3, 2 3x3, 3 list */
#define PROTO_STARTUP_TAB_COUNT  3U  /**< 0 last used, 1 all, 2 favorites */
#define PROTO_TAB_COUNT          2U  /**< 0 all, 1 favorites */
#define PROTO_MAX_SCREEN_TIMEOUT 60U /**< minutes; 0 = never */
#define PROTO_LANGUAGE_COUNT     2U  /**< 0 Turkish, 1 English */
#define PROTO_STARTUP_SCREEN_COUNT 3U /**< 0 Dashboard, 1 Audio (ADR-005), 2 Music (ADR-008) */
#define PROTO_SETTINGS_BYTES     12U
#define PROTO_DEFAULT_VOLUME_LIMIT 50U

/** ERROR codes. */
typedef enum
{
    PROTO_ERROR_BAD_FRAME           = 0x01, /**< magic, CRC or length wrong */
    PROTO_ERROR_UNSUPPORTED_VERSION = 0x02,
    PROTO_ERROR_UNKNOWN_COMMAND     = 0x03,
    PROTO_ERROR_BAD_PAYLOAD         = 0x04,
    PROTO_ERROR_UNKNOWN_DEVICE      = 0x05, /**< device_index not in the current list */
    PROTO_ERROR_OPERATION_FAILED    = 0x06, /**< receiver could not carry out the request */
    PROTO_ERROR_BUSY                = 0x07
} ProtoErrorCode;

/** Result of encoding or decoding. */
typedef enum
{
    PROTO_OK = 0,
    PROTO_ERR_ARG,      /**< NULL pointer or invalid field in the message to encode */
    PROTO_ERR_MAGIC,
    PROTO_ERR_VERSION,
    PROTO_ERR_CRC,
    PROTO_ERR_LENGTH,   /**< length byte above PROTO_MAX_PAYLOAD */
    PROTO_ERR_COMMAND,  /**< unknown command */
    PROTO_ERR_PAYLOAD   /**< payload size or a field value invalid for the command */
} ProtoStatus;

/** One entry of a device list (DEVICE_LIST payload). */
typedef struct
{
    uint8_t generation;   /**< same for every frame of one list; changes per new list */
    uint8_t total;        /**< devices in the list, 0 .. PROTO_MAX_DEVICES */
    uint8_t position;     /**< 0 .. total-1; 0 when total is 0 */
    uint8_t device_index; /**< host-assigned id, valid until the next list */
    uint8_t flags;        /**< PROTO_DEVICE_FLAG_* */
    uint8_t icon;         /**< ProtoIcon */
    uint8_t name_len;     /**< bytes in name, 0 .. PROTO_MAX_NAME_BYTES */
    char    name[PROTO_MAX_NAME_BYTES + 1U]; /**< UTF-8, NUL-terminated after decode */
} ProtoDeviceEntry;

/** DEVICE_ALIAS payload: the alias of one entry of the list being sent. */
typedef struct
{
    uint8_t generation; /**< generation of the list it belongs to */
    uint8_t position;   /**< entry position in that list */
    uint8_t alias_len;  /**< 1 .. PROTO_MAX_ALIAS_BYTES */
    char    alias[PROTO_MAX_ALIAS_BYTES + 1U]; /**< UTF-8, NUL-terminated after decode */
} ProtoAliasEntry;

/** SELECT_DEVICE, SET_FAVORITE, MOVE_DEVICE, SET_ALIAS: a request about one
 *  device of the list with @p generation. */
typedef struct
{
    uint8_t generation;
    uint8_t device_index;
    uint8_t value;      /**< SET_FAVORITE: 0/1; MOVE_DEVICE: new position
                             (< PROTO_MAX_DEVICES); SELECT_DEVICE: 0 */
    uint8_t alias_len;  /**< SET_ALIAS only: 0 (remove) .. PROTO_MAX_ALIAS_BYTES */
    char    alias[PROTO_MAX_ALIAS_BYTES + 1U]; /**< SET_ALIAS only */
} ProtoDeviceRequest;

/** UI settings record, stored by the host (ADR-004). */
typedef struct
{
    uint8_t layout;          /**< < PROTO_LAYOUT_COUNT */
    uint8_t theme;           /**< < PROTO_THEME_COUNT */
    uint8_t accent;          /**< < PROTO_ACCENT_COUNT */
    uint8_t grid;            /**< < PROTO_GRID_COUNT */
    uint8_t startup_tab;     /**< < PROTO_STARTUP_TAB_COUNT */
    uint8_t last_tab;        /**< < PROTO_TAB_COUNT */
    uint8_t screen_timeout;  /**< minutes, 0 .. PROTO_MAX_SCREEN_TIMEOUT */
    uint8_t flipped;         /**< 0 or 1 */
    uint8_t language;        /**< < PROTO_LANGUAGE_COUNT */
    uint8_t volume_limit_on; /**< 0 or 1 */
    uint8_t volume_limit;    /**< 0 .. PROTO_MAX_VOLUME */
    uint8_t startup_screen;  /**< < PROTO_STARTUP_SCREEN_COUNT (version 3; 2 Music: version 4) */
} ProtoSettings;

/** ACTION kinds. */
typedef enum
{
    PROTO_ACTION_CARD   = 0, /**< a button of the detail of card slot `ref` */
    PROTO_ACTION_PROMPT = 1, /**< a button of the prompt with id `ref` */
    PROTO_ACTION_MEDIA  = 2  /**< the media strip; `ref` is 0 */
} ProtoActionKind;
#define PROTO_ACTION_KIND_COUNT 3U

/** ACTION values of the media strip. */
#define PROTO_MEDIA_PREVIOUS   1U
#define PROTO_MEDIA_PLAY_PAUSE 2U
#define PROTO_MEDIA_NEXT       3U

/** CARD_PART, DETAIL_PART, PROMPT_PART, MEDIA_PART: one part of a text record. */
typedef struct
{
    uint8_t id;         /**< card slot (< PROTO_MAX_CARDS), prompt id, or 0 (media) */
    uint8_t generation; /**< changes for every new version of the record */
    uint8_t part;       /**< 0 .. parts-1 */
    uint8_t parts;      /**< 1 .. the command's PROTO_*_MAX_PARTS */
    uint8_t len;        /**< bytes in data: PROTO_PART_DATA_BYTES except in the last part */
    uint8_t data[PROTO_PART_DATA_BYTES]; /**< UTF-8, no NUL, not terminated */
} ProtoRecordPart;

/** MEDIA_STATE payload (ADR-008). */
typedef struct
{
    uint8_t  status;   /**< PROTO_MEDIA_STATUS_* */
    uint8_t  controls; /**< PROTO_MEDIA_CAN_* */
    uint8_t  art_id;   /**< PROTO_NO_ART, or 1..255: the cover sent in ART_PARTs */
    uint32_t position; /**< seconds; <= duration when duration is not 0 */
    uint32_t duration; /**< seconds; 0 = unknown */
} ProtoMediaState;

/** ART_PART payload: one part of a cover. */
typedef struct
{
    uint8_t  art_id; /**< 1..255 */
    uint16_t index;  /**< 0 .. PROTO_ART_PARTS-1 */
    uint8_t  len;    /**< PROTO_ART_DATA_BYTES, or the rest in the last part */
    uint8_t  data[PROTO_ART_DATA_BYTES];
} ProtoArtPart;

/** OPEN_DETAIL payload. */
typedef struct
{
    uint8_t slot; /**< < PROTO_MAX_CARDS */
    int8_t  arg;  /**< -1, 0 or 1 (0: default; -1/+1: previous/next, e.g. menu day) */
} ProtoOpenDetail;

/** ACTION payload. */
typedef struct
{
    uint8_t kind;   /**< ProtoActionKind */
    uint8_t ref;    /**< card slot, prompt id, or 0 for media */
    uint8_t action; /**< 1..255; media: PROTO_MEDIA_* */
} ProtoAction;

typedef struct
{
    uint8_t code;        /**< ProtoErrorCode */
    uint8_t ref_command; /**< command being answered, 0 if none */
    uint8_t ref_sequence;
} ProtoError;

/** A decoded or to-be-encoded message. */
typedef struct
{
    uint8_t command;  /**< ProtoCommand */
    uint8_t sequence;
    union
    {
        ProtoDeviceEntry   device;       /**< DEVICE_LIST */
        ProtoAliasEntry    alias;        /**< DEVICE_ALIAS */
        ProtoDeviceRequest request;      /**< SELECT_DEVICE, SET_ALIAS, SET_FAVORITE, MOVE_DEVICE */
        ProtoSettings      settings;     /**< SETTINGS, SET_SETTINGS */
        uint8_t            device_index; /**< DEVICE_CHANGED */
        uint8_t            volume;       /**< SET_VOLUME, VOLUME_CHANGED: 0 .. 100 */
        uint8_t            muted;        /**< SET_MUTE, MUTE_CHANGED: 0 or 1 */
        ProtoError         error;        /**< ERROR */
        uint32_t           local_time;   /**< TIME: seconds since 1970-01-01 00:00 local time */
        ProtoRecordPart    part;         /**< CARD_PART, DETAIL_PART, PROMPT_PART, MEDIA_PART */
        ProtoMediaState    media;        /**< MEDIA_STATE */
        ProtoArtPart       art;          /**< ART_PART */
        uint8_t            slot;         /**< CARD_CLEAR */
        uint8_t            prompt_id;    /**< PROMPT_CLOSE */
        ProtoOpenDetail    open_detail;  /**< OPEN_DETAIL */
        ProtoAction        action;       /**< ACTION */
    } u;
} ProtoMessage;

/** CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout. */
uint16_t Proto_Crc16(const uint8_t *data, size_t len);

/**
 * @brief  Validate @p msg and write it as one frame.
 * @return PROTO_OK, PROTO_ERR_ARG, PROTO_ERR_COMMAND or PROTO_ERR_PAYLOAD.
 *         On error @p frame is left all zero.
 */
ProtoStatus Proto_Encode(const ProtoMessage *msg, uint8_t frame[PROTO_FRAME_SIZE]);

/**
 * @brief  Check and decode one received frame.
 *
 * Checks, in order: magic, version, CRC, length, command, payload. On any
 * error @p msg is cleared except command and sequence, which hold the
 * received bytes when the header could be read (for an ERROR reply).
 */
ProtoStatus Proto_Decode(const uint8_t frame[PROTO_FRAME_SIZE], ProtoMessage *msg);

/** Map a decode failure to the ERROR code to send back. */
ProtoErrorCode Proto_ErrorCodeFor(ProtoStatus status);

/** The settings a board uses until the host sends its own (ADR-004). */
void Proto_DefaultSettings(ProtoSettings *settings);

/** 1 if every field of @p settings is in range, else 0. */
int Proto_SettingsValid(const ProtoSettings *settings);

/* ------------------------------------------------------------ records */

/**
 * Largest record of a part command (CARD_PART, DETAIL_PART, PROMPT_PART,
 * MEDIA_PART), in bytes; 0 for any other command.
 */
uint16_t Proto_RecordMaxBytes(uint8_t command);

/** Number of parts for a record of @p length bytes (1 for an empty record). */
uint8_t Proto_RecordPartCount(uint16_t length);

/**
 * @brief  Fill @p out with part @p part of @p record (sender side).
 * @return 1, or 0 if @p part is not a part of a record of @p length bytes
 *         or @p record holds a NUL byte in that part.
 */
int Proto_RecordPart(const uint8_t *record, uint16_t length, uint8_t id, uint8_t generation,
                     uint8_t part, ProtoRecordPart *out);

/** Result of Proto_AssemblerAdd. */
typedef enum
{
    PROTO_ASM_PARTIAL = 0, /**< part kept, more parts to come */
    PROTO_ASM_COMPLETE,    /**< the record is complete in the buffer */
    PROTO_ASM_DUPLICATE,   /**< this part had arrived already; ignored */
    PROTO_ASM_OVERFLOW     /**< the record does not fit the buffer; dropped */
} ProtoAsmResult;

/**
 * Collects the parts of one record (receiver side). The parts of one id
 * and generation are written into a caller-owned buffer; a part of
 * another id or generation starts a new record and discards a partial
 * one. Parts may arrive in any order.
 */
typedef struct
{
    uint8_t *buffer;   /**< capacity + 1 bytes (room for a terminator) */
    uint16_t capacity; /**< largest record in bytes */
    uint8_t  active;   /**< 1 while a record is being collected */
    uint8_t  id;
    uint8_t  generation;
    uint8_t  parts;
    uint64_t received; /**< bit n set: part n arrived (parts <= 64) */
    uint16_t length;   /**< record length once complete (data of the last part known) */
} ProtoAssembler;

/** Prepare @p asm for records of at most @p capacity bytes; @p buffer holds
 *  capacity + 1 bytes. */
void Proto_AssemblerInit(ProtoAssembler *asm_, uint8_t *buffer, uint16_t capacity);

/** Forget a partial record. */
void Proto_AssemblerReset(ProtoAssembler *asm_);

/**
 * @brief  Add one decoded part.
 *
 * On PROTO_ASM_COMPLETE the record is in the buffer, NUL-terminated, with
 * `length` bytes; the assembler is idle again (the next part starts a new
 * record). After PROTO_ASM_OVERFLOW it is idle too.
 */
ProtoAsmResult Proto_AssemblerAdd(ProtoAssembler *asm_, const ProtoRecordPart *part);

/* -------------------------------------------------------------- covers */

/** Bytes in part @p index of a cover: PROTO_ART_DATA_BYTES, the rest in the
 *  last part, 0 for an index beyond it. */
uint8_t Proto_ArtPartBytes(uint16_t index);

/**
 * @brief  Fill @p out with part @p index of the cover @p art
 *         (PROTO_ART_BYTES bytes; sender side).
 * @return 1, or 0 if @p art_id is PROTO_NO_ART or @p index is not a part.
 */
int Proto_ArtPart(const uint8_t *art, uint8_t art_id, uint16_t index, ProtoArtPart *out);

/**
 * Collects the parts of one cover into a caller-owned buffer of
 * PROTO_ART_BYTES bytes (receiver side). Parts of another art_id than the
 * one set with Proto_ArtAssemblerStart are dropped.
 */
typedef struct
{
    uint8_t *buffer;   /**< PROTO_ART_BYTES bytes */
    uint8_t  art_id;   /**< cover being collected; PROTO_NO_ART = none */
    uint16_t count;    /**< parts received */
    uint8_t  received[(PROTO_ART_PARTS + 7U) / 8U]; /**< bit n: part n arrived */
} ProtoArtAssembler;

/** Prepare @p asm_ with @p buffer (PROTO_ART_BYTES bytes); collects nothing. */
void Proto_ArtAssemblerInit(ProtoArtAssembler *asm_, uint8_t *buffer);

/** Collect the cover @p art_id from now on (PROTO_NO_ART: none); parts
 *  collected for another id are forgotten. The same id again: no change. */
void Proto_ArtAssemblerStart(ProtoArtAssembler *asm_, uint8_t art_id);

/**
 * @brief  Add one decoded part.
 * @return PROTO_ASM_PARTIAL, PROTO_ASM_COMPLETE (the cover is in the buffer;
 *         the assembler then collects nothing until the next Start),
 *         PROTO_ASM_DUPLICATE, or PROTO_ASM_OVERFLOW for a part of another
 *         art_id or a bad part (dropped; parts collected so far are kept).
 */
ProtoAsmResult Proto_ArtAssemblerAdd(ProtoArtAssembler *asm_, const ProtoArtPart *part);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
