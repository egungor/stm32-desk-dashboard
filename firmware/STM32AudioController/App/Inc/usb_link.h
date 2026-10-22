/**
 * @file    usb_link.h
 * @brief   Vendor-defined USB HID interface carrying protocol frames
 *          (docs/decisions/ADR-003-usb-hid-interface.md).
 *
 * One 64-byte input report (device -> host) and one 64-byte output report
 * (host -> device), no report ID; each report is one protocol frame.
 *
 * Received frames are copied into a FreeRTOS queue by the USB interrupt;
 * while it is full the OUT endpoint is not armed, so the host waits (NAK)
 * instead of frames being lost (#68);
 * the application task takes them with UsbLink_Receive(). UsbLink_Send()
 * is called from the same task. Nothing here blocks in interrupt context
 * or allocates memory.
 */
#ifndef USB_LINK_H
#define USB_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"
#include "usbd_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Frames buffered between the USB interrupt and the task. Holds a full
 *  answer to GET_DEVICE_LIST (32 entries + DEVICE_CHANGED, VOLUME_CHANGED,
 *  MUTE_CHANGED), so a list survives the UI task being busy for a while
 *  (tests/integration). 40 x 64 bytes of static RAM. */
#define USB_LINK_RX_DEPTH 40U

typedef struct
{
    uint32_t rx_frames;
    uint32_t rx_dropped;  /**< wrong length (a full queue holds the host off instead, #68) */
    uint32_t rx_paused;   /**< times the queue was full and the host was held off */
    uint32_t tx_frames;
    uint32_t tx_timeouts;
} UsbLinkStats;

/**
 * @brief  Initialise the USB device stack with this class and start it.
 *
 * Called from the USB_DEVICE_Init_PreTreatment USER CODE section of
 * MX_USB_DEVICE_Init() (task context), which then returns before the
 * generated demo HID class is registered.
 */
void UsbLink_DeviceInit(USBD_HandleTypeDef *pdev);

/**
 * @brief  Watch VBUS (PJ12) and treat its loss as a disconnect.
 *
 * Call periodically (well under every 20 ms) from the application task,
 * before UsbLink_IsConfigured(). Needed because the OTG FS core has no
 * VBUS sensing on this board and would otherwise only see a suspend. Also
 * samples the SOF frame number, so that a spurious resume without a host
 * does not count as configured.
 */
void UsbLink_Poll(void);

/** VBUS state seen by the last UsbLink_Poll(). */
bool UsbLink_VbusPresent(void);

/** True while VBUS is present, the device is configured (not suspended)
 *  and the host sent an SOF within the last 20 ms, as seen by the last
 *  UsbLink_Poll(). */
bool UsbLink_IsConfigured(void);

/** Number of SET_CONFIGURATION requests so far. Unchanged across a
 *  suspend/resume, so the caller can tell a resume from a new
 *  configuration. */
uint32_t UsbLink_ConfigCount(void);

/**
 * @brief  Take the next received frame.
 * @return false if none arrived within @p timeout_ms (0: do not wait).
 */
bool UsbLink_Receive(uint8_t frame[PROTO_FRAME_SIZE], uint32_t timeout_ms);

/**
 * @brief  Send one frame as an input report.
 * @return false if not configured or the previous report was not
 *         collected by the host within @p timeout_ms.
 */
bool UsbLink_Send(const uint8_t frame[PROTO_FRAME_SIZE], uint32_t timeout_ms);

void UsbLink_GetStats(UsbLinkStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* USB_LINK_H */
