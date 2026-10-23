/**
 * @file    usb_link.c
 * @brief   Vendor-defined USB HID interface carrying protocol frames
 *          (docs/decisions/ADR-003-usb-hid-interface.md).
 */
#include "usb_link.h"

#include <string.h>

#include "FreeRTOS.h"
#include "main.h"
#include "cmsis_os.h"
#include "queue.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_desc.h"
#include "usbd_ioreq.h"

#define EP_IN_ADDR        0x81U
#define EP_OUT_ADDR       0x01U
#define EP_SIZE           PROTO_FRAME_SIZE
#define EP_INTERVAL_MS    5U

/* A host that is not suspending the bus sends an SOF every 1 ms. */
#define SOF_TIMEOUT_MS    20U

#define HID_DESC_TYPE     0x21U
#define HID_REPORT_TYPE   0x22U
#define HID_DESC_SIZE     9U
#define CFG_DESC_SIZE     41U

#define HID_REQ_GET_REPORT   0x01U
#define HID_REQ_GET_IDLE     0x02U
#define HID_REQ_GET_PROTOCOL 0x03U
#define HID_REQ_SET_IDLE     0x0AU
#define HID_REQ_SET_PROTOCOL 0x0BU

/* Vendor page 0xFF00, usage 1: one 64-byte input report (usage 2) and one
 * 64-byte output report (usage 3), no report ID. */
__ALIGN_BEGIN static uint8_t s_report_desc[] __ALIGN_END = {
    0x06, 0x00, 0xFF,       /* Usage Page (Vendor 0xFF00) */
    0x09, 0x01,             /* Usage (0x01) */
    0xA1, 0x01,             /* Collection (Application) */
    0x09, 0x02,             /*   Usage (0x02) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,       /*   Logical Maximum (255) */
    0x75, 0x08,             /*   Report Size (8) */
    0x95, EP_SIZE,          /*   Report Count (64) */
    0x81, 0x02,             /*   Input (Data,Var,Abs) */
    0x09, 0x03,             /*   Usage (0x03) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,       /*   Logical Maximum (255) */
    0x75, 0x08,             /*   Report Size (8) */
    0x95, EP_SIZE,          /*   Report Count (64) */
    0x91, 0x02,             /*   Output (Data,Var,Abs) */
    0xC0                    /* End Collection */
};

#define HID_DESC_BYTES                                                          \
    HID_DESC_SIZE, HID_DESC_TYPE, 0x11, 0x01, /* bcdHID 1.11 */                 \
        0x00,                                 /* country */                     \
        0x01,                                 /* one class descriptor */        \
        HID_REPORT_TYPE, LOBYTE(sizeof(s_report_desc)), HIBYTE(sizeof(s_report_desc))

__ALIGN_BEGIN static uint8_t s_cfg_desc[] __ALIGN_END = {
    /* Configuration */
    0x09, USB_DESC_TYPE_CONFIGURATION, LOBYTE(CFG_DESC_SIZE), HIBYTE(CFG_DESC_SIZE),
    0x01,                   /* bNumInterfaces */
    0x01,                   /* bConfigurationValue */
    0x00,                   /* iConfiguration */
#if (USBD_SELF_POWERED == 1U)
    0xC0,                   /* bmAttributes: self powered */
#else
    0x80,                   /* bmAttributes: bus powered */
#endif
    USBD_MAX_POWER,
    /* Interface 0: HID, no boot protocol */
    0x09, USB_DESC_TYPE_INTERFACE, 0x00, 0x00,
    0x02,                   /* bNumEndpoints */
    0x03, 0x00, 0x00,       /* HID, no subclass, no protocol */
    0x00,                   /* iInterface */
    /* HID descriptor */
    HID_DESC_BYTES,
    /* Endpoint IN */
    0x07, USB_DESC_TYPE_ENDPOINT, EP_IN_ADDR, USBD_EP_TYPE_INTR, LOBYTE(EP_SIZE), HIBYTE(EP_SIZE),
    EP_INTERVAL_MS,
    /* Endpoint OUT */
    0x07, USB_DESC_TYPE_ENDPOINT, EP_OUT_ADDR, USBD_EP_TYPE_INTR, LOBYTE(EP_SIZE), HIBYTE(EP_SIZE),
    EP_INTERVAL_MS,
};

_Static_assert(sizeof(s_cfg_desc) == CFG_DESC_SIZE, "configuration descriptor length");

__ALIGN_BEGIN static uint8_t s_hid_desc[] __ALIGN_END = { HID_DESC_BYTES };
_Static_assert(sizeof(s_hid_desc) == HID_DESC_SIZE, "HID descriptor length");
_Static_assert(sizeof(s_report_desc) == 34U, "report descriptor length");

__ALIGN_BEGIN static uint8_t s_qualifier_desc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
    USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

/* Class state. Written by the USB interrupt and read by the task. */
static struct
{
    uint8_t           idle;
    uint8_t           protocol;
    uint8_t           alt_setting;
    volatile bool     tx_busy;
    volatile bool     rx_paused; /**< queue full: OUT not armed, the host is NAKed (#68) */
    __ALIGN_BEGIN uint8_t rx_buf[EP_SIZE] __ALIGN_END;
    __ALIGN_BEGIN uint8_t tx_buf[EP_SIZE] __ALIGN_END;
} s_class;

static USBD_HandleTypeDef *s_pdev;
static QueueHandle_t s_rx_queue;
static StaticQueue_t s_rx_queue_storage;
static uint8_t s_rx_queue_buf[USB_LINK_RX_DEPTH * EP_SIZE];
static volatile UsbLinkStats s_stats;
static volatile uint32_t s_config_count; /* SET_CONFIGURATION requests */
static bool s_vbus;
static uint32_t s_frame;          /* last SOF frame number seen by UsbLink_Poll */
static uint32_t s_frame_tick;     /* HAL tick when it last changed */

/* ------------------------------------------------------------- class */

static uint8_t link_init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    pdev->pClassDataCmsit[pdev->classId] = &s_class;
    pdev->pClassData = &s_class;
    s_class.tx_busy = false;
    s_class.rx_paused = false;
    s_config_count++;

    pdev->ep_in[EP_IN_ADDR & 0xFU].bInterval = EP_INTERVAL_MS;
    (void)USBD_LL_OpenEP(pdev, EP_IN_ADDR, USBD_EP_TYPE_INTR, EP_SIZE);
    pdev->ep_in[EP_IN_ADDR & 0xFU].is_used = 1U;

    pdev->ep_out[EP_OUT_ADDR & 0xFU].bInterval = EP_INTERVAL_MS;
    (void)USBD_LL_OpenEP(pdev, EP_OUT_ADDR, USBD_EP_TYPE_INTR, EP_SIZE);
    pdev->ep_out[EP_OUT_ADDR & 0xFU].is_used = 1U;

    (void)USBD_LL_PrepareReceive(pdev, EP_OUT_ADDR, s_class.rx_buf, EP_SIZE);
    return (uint8_t)USBD_OK;
}

static uint8_t link_deinit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    UNUSED(cfgidx);

    (void)USBD_LL_CloseEP(pdev, EP_IN_ADDR);
    pdev->ep_in[EP_IN_ADDR & 0xFU].is_used = 0U;
    (void)USBD_LL_CloseEP(pdev, EP_OUT_ADDR);
    pdev->ep_out[EP_OUT_ADDR & 0xFU].is_used = 0U;
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    pdev->pClassData = NULL;
    s_class.tx_busy = false;
    return (uint8_t)USBD_OK;
}

static uint8_t link_setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    static uint16_t status_info;
    uint8_t ret = (uint8_t)USBD_OK;

    switch (req->bmRequest & USB_REQ_TYPE_MASK)
    {
    case USB_REQ_TYPE_CLASS:
        switch (req->bRequest)
        {
        case HID_REQ_SET_PROTOCOL:
            s_class.protocol = (uint8_t)req->wValue;
            break;
        case HID_REQ_GET_PROTOCOL:
            (void)USBD_CtlSendData(pdev, &s_class.protocol, 1U);
            break;
        case HID_REQ_SET_IDLE:
            s_class.idle = (uint8_t)(req->wValue >> 8);
            break;
        case HID_REQ_GET_IDLE:
            (void)USBD_CtlSendData(pdev, &s_class.idle, 1U);
            break;
        default:
            /* GET_REPORT / SET_REPORT over EP0 are not supported: frames
             * use the interrupt endpoints only. */
            USBD_CtlError(pdev, req);
            ret = (uint8_t)USBD_FAIL;
            break;
        }
        break;

    case USB_REQ_TYPE_STANDARD:
        switch (req->bRequest)
        {
        case USB_REQ_GET_STATUS:
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
            break;
        case USB_REQ_GET_DESCRIPTOR:
            if ((req->wValue >> 8) == HID_REPORT_TYPE)
            {
                (void)USBD_CtlSendData(pdev, s_report_desc,
                                       (uint16_t)MIN(sizeof(s_report_desc), req->wLength));
            }
            else if ((req->wValue >> 8) == HID_DESC_TYPE)
            {
                (void)USBD_CtlSendData(pdev, s_hid_desc, (uint16_t)MIN(HID_DESC_SIZE, req->wLength));
            }
            else
            {
                USBD_CtlError(pdev, req);
                ret = (uint8_t)USBD_FAIL;
            }
            break;
        case USB_REQ_GET_INTERFACE:
            (void)USBD_CtlSendData(pdev, &s_class.alt_setting, 1U);
            break;
        case USB_REQ_SET_INTERFACE:
            s_class.alt_setting = (uint8_t)req->wValue;
            break;
        case USB_REQ_CLEAR_FEATURE:
            break;
        default:
            USBD_CtlError(pdev, req);
            ret = (uint8_t)USBD_FAIL;
            break;
        }
        break;

    default:
        USBD_CtlError(pdev, req);
        ret = (uint8_t)USBD_FAIL;
        break;
    }
    return ret;
}

/* Interrupt context: the host collected the input report. */
static uint8_t link_data_in(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    UNUSED(pdev);
    UNUSED(epnum);
    s_class.tx_busy = false;
    return (uint8_t)USBD_OK;
}

/* Interrupt context: an output report arrived. Copy it into the queue and
 * re-arm the endpoint, unless the queue is now full: then the endpoint
 * stays unarmed, the host's next report is NAKed (it waits, USB flow
 * control) and UsbLink_Receive arms it again once a frame was taken
 * (#68). Before, a full queue threw frames away and a lone card update
 * could be lost for good. */
static uint8_t link_data_out(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    BaseType_t woken = pdFALSE;

    if ((USBD_GetRxCount(pdev, epnum) == EP_SIZE) && (s_rx_queue != NULL) &&
        (xQueueSendFromISR(s_rx_queue, s_class.rx_buf, &woken) == pdPASS))
    {
        s_stats.rx_frames++;
    }
    else
    {
        s_stats.rx_dropped++;  /* wrong length, or no queue */
    }
    if ((s_rx_queue != NULL) && (xQueueIsQueueFullFromISR(s_rx_queue) != pdFALSE))
    {
        s_class.rx_paused = true;
        s_stats.rx_paused++;
    }
    else
    {
        (void)USBD_LL_PrepareReceive(pdev, EP_OUT_ADDR, s_class.rx_buf, EP_SIZE);
    }
    portYIELD_FROM_ISR(woken);
    return (uint8_t)USBD_OK;
}

static uint8_t *link_cfg_desc(uint16_t *length)
{
    *length = (uint16_t)sizeof(s_cfg_desc);
    return s_cfg_desc;
}

static uint8_t *link_qualifier_desc(uint16_t *length)
{
    *length = (uint16_t)sizeof(s_qualifier_desc);
    return s_qualifier_desc;
}

static USBD_ClassTypeDef s_link_class = {
    link_init,
    link_deinit,
    link_setup,
    NULL,              /* EP0_TxSent */
    NULL,              /* EP0_RxReady */
    link_data_in,
    link_data_out,
    NULL,              /* SOF */
    NULL,
    NULL,
    link_cfg_desc,     /* high speed: same descriptor (FS only device) */
    link_cfg_desc,
    link_cfg_desc,
    link_qualifier_desc,
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
    NULL,
#endif
};

/* --------------------------------------------------------------- API */

static bool read_vbus(void)
{
    return HAL_GPIO_ReadPin(OTG_FS_VBUS_GPIO_Port, OTG_FS_VBUS_Pin) == GPIO_PIN_SET;
}

static uint32_t read_frame(void)
{
    const USB_OTG_DeviceTypeDef *dev =
        (const USB_OTG_DeviceTypeDef *)((uint32_t)USB_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE);

    return (dev->DSTS & USB_OTG_DSTS_FNSOF) >> USB_OTG_DSTS_FNSOF_Pos;
}

void UsbLink_DeviceInit(USBD_HandleTypeDef *pdev)
{
    s_pdev = pdev;
    s_vbus = read_vbus();
    s_rx_queue = xQueueCreateStatic(USB_LINK_RX_DEPTH, EP_SIZE, s_rx_queue_buf, &s_rx_queue_storage);

    if ((USBD_Init(pdev, &FS_Desc, DEVICE_FS) != USBD_OK) ||
        (USBD_RegisterClass(pdev, &s_link_class) != USBD_OK) || (USBD_Start(pdev) != USBD_OK))
    {
        Error_Handler();
    }
}

/* The OTG FS core runs without VBUS sensing (PA9 is USART1_TX on this
 * board), so it never reports a disconnect: an unplugged cable is only a
 * suspend, and the stack keeps CONFIGURED as the state to resume to. A
 * wake-up interrupt from the floating lines then restores CONFIGURED with
 * no host attached (issue #52). VBUS is wired to the GPIO PJ12 instead;
 * losing it is handled here as the HAL would handle a session end.
 * VBUS takes longer to fall than the spurious resume takes to arrive, so
 * the device also counts as configured only while SOFs arrive: a real
 * resume is followed by SOFs, a spurious one is not. */
/* Note when the frame number last changed, i.e. when SOFs last arrived.
 * Called whenever the state is asked for, not only once per UI loop: a
 * long LVGL redraw between two loops must not look like a silent bus. */
static void sample_sof(void)
{
    const uint32_t frame = read_frame();

    if (frame != s_frame)
    {
        s_frame = frame;
        s_frame_tick = HAL_GetTick();
    }
}

void UsbLink_Poll(void)
{
    const bool vbus = read_vbus();

    sample_sof();
    if ((s_pdev != NULL) && s_vbus && !vbus)
    {
        /* Same path as HAL_PCD_DisconnectCallback, but from the task: keep
         * the USB interrupt out while the class is de-initialised. */
        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
        (void)USBD_LL_DevDisconnected(s_pdev);
        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    }
    s_vbus = vbus;
}

bool UsbLink_VbusPresent(void)
{
    return s_vbus;
}

bool UsbLink_IsConfigured(void)
{
    sample_sof();
    return (s_pdev != NULL) && s_vbus && (s_pdev->dev_state == USBD_STATE_CONFIGURED) &&
           ((HAL_GetTick() - s_frame_tick) < SOF_TIMEOUT_MS);
}

uint32_t UsbLink_ConfigCount(void)
{
    return s_config_count;
}

bool UsbLink_Receive(uint8_t frame[PROTO_FRAME_SIZE], uint32_t timeout_ms)
{
    if ((s_rx_queue == NULL) || (xQueueReceive(s_rx_queue, frame, pdMS_TO_TICKS(timeout_ms)) != pdPASS))
    {
        return false;
    }
    /* A place is free again: arm the endpoint the interrupt left unarmed.
     * The USB interrupt is masked meanwhile (it uses the FromISR API, so
     * its priority is within configMAX_SYSCALL_INTERRUPT_PRIORITY). */
    taskENTER_CRITICAL();
    if (s_class.rx_paused && (s_pdev != NULL))
    {
        s_class.rx_paused = false;
        (void)USBD_LL_PrepareReceive(s_pdev, EP_OUT_ADDR, s_class.rx_buf, EP_SIZE);
    }
    taskEXIT_CRITICAL();
    return true;
}

bool UsbLink_Send(const uint8_t frame[PROTO_FRAME_SIZE], uint32_t timeout_ms)
{
    const uint32_t start = HAL_GetTick();

    while (UsbLink_IsConfigured() && s_class.tx_busy)
    {
        if ((HAL_GetTick() - start) >= timeout_ms)
        {
            s_stats.tx_timeouts++;
            return false;
        }
        osDelay(1);
    }
    if (!UsbLink_IsConfigured())
    {
        return false;
    }
    memcpy(s_class.tx_buf, frame, EP_SIZE);
    s_class.tx_busy = true;
    if (USBD_LL_Transmit(s_pdev, EP_IN_ADDR, s_class.tx_buf, EP_SIZE) != USBD_OK)
    {
        s_class.tx_busy = false;
        return false;
    }
    s_stats.tx_frames++;
    return true;
}

void UsbLink_GetStats(UsbLinkStats *stats)
{
    stats->rx_frames = s_stats.rx_frames;
    stats->rx_dropped = s_stats.rx_dropped;
    stats->rx_paused = s_stats.rx_paused;
    stats->tx_frames = s_stats.tx_frames;
    stats->tx_timeouts = s_stats.tx_timeouts;
}
