#include "stdafx.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "plcm_dfu.h"
#include "usb.h"
#include "md5_utils.h"

#define DEBUG_PRINT 0

struct setup_packet {
    unsigned char bRequestType;
    unsigned char bRequest;
    unsigned short wValue;
    unsigned short wIndex;
    unsigned short wLength;
};

#define PLCM_USB_REQUEST_SET_INFORMATION	0x01
#define PLCM_USB_REQUEST_GET_INFORMATION	0x81

/* WUP specific requests. */
#define PLCM_USB_REQ_WUP_SET_DNLOAD_INFO     0x0001
#define PLCM_USB_REQ_WUP_GETSTATUS           0x0002
#define PLCM_USB_REQ_WUP_CLRSTATUS           0x0003
#define PLCM_USB_REQ_WUP_GETSTATE            0x0005
#define PLCM_USB_REQ_WUP_ABORT               0x0006
#define PLCM_USB_REQ_WUP_SYNC                0x0007
#define PLCM_USB_REQ_WUP_INT_CHECK           0x0008
#define PLCM_USB_REQ_WUP_START_UPDATE		 0x0009

#if defined(_WIN32)
#pragma pack (1)
struct wup_status {
    UINT8 bStatus;
    UINT8 bState;
    union {
        UINT32 dwWrittenBytes;
        UINT8 bReserved[6];
    } u;
};

struct wup_dnload_info {
    char sSwVersion[32];
    UINT32 dwImageSize;
    UINT32 dwSyncBlockSize;
    UINT8 bForced;
    UINT8 bReserved[23];
};
#pragma pack ()
#else
struct wup_status {
    uint8_t bStatus;
    uint8_t bState;
    union {
        uint32_t dwWrittenBytes;
        uint32_t bReserved[6];
    } u;
} __attribute__((packed));

struct wup_dnload_info {
    char sSwVersion[32];
    uint32_t dwImageSize;
    uint32_t dwSyncBlockSize;
    uint8_t bForced;
    uint8_t bReserved[23];
} __attribute__((packed));
#endif

#define WUP_STATUS_OK                   0x00
#define WUP_STATUS_errSTATE             0x01
#define WUP_STATUS_errCHECK             0x02
#define WUP_STATUS_errTARGET            0x03
#define WUP_STATUS_errFILE              0x04
#define WUP_STATUS_errWRITE             0x05
#define WUP_STATUS_errVERIFY            0x06
#define WUP_STATUS_errNOTDONE           0x07
#define WUP_STATUS_errINVAL             0x08
#define WUP_STATUS_errTRANS             0x09
#define WUP_STATUS_errUNKNOWN           0x0A

enum wup_state {
    WUP_STATE_dfuDETACHED = 0,
    WUP_STATE_dfuIDLE = 1,
    WUP_STATE_dfuDNLOAD_IDLE = 2,
    WUP_STATE_dfuDNLOAD_BUSY = 3,
    WUP_STATE_dfuDNLOAD_SYNC = 4,
    WUP_STATE_dfuDNLOAD_VERY = 5,
    WUP_STATE_dfuUPDATE_BUSY = 6,
    WUP_STATE_dfuERROR = 7,
};

#define WUP_SYNC_BLOCK_SIZE		(64 * 1024 * 1024)

static ssize_t polySendControlInfo(Transport *transport, bool is_in_direction,
                                   unsigned char request, unsigned short value, void *data, size_t len)
{
    struct setup_packet setup;

    memset(&setup, 0x00, sizeof(struct setup_packet));

    setup.bRequest = request;
    setup.wValue = value;
    setup.wLength = (unsigned short)len;

    return transport->ControlIO(is_in_direction, &setup, data, len);
}


static int polySyncData(Transport *transport, struct wup_status *wup_status)
{
    int retries = 0;
    ssize_t ret = 0;

    do {
        transport->Wait(1000);

        //Try to do sync
        memset(wup_status, 0x00, sizeof(struct wup_status));

        ret = polySendControlInfo(transport,
                                  true,
                                  PLCM_USB_REQUEST_GET_INFORMATION,
                                  PLCM_USB_REQ_WUP_SYNC,
                                  wup_status,
                                  sizeof(struct wup_status));

        if (ret < 0) {
            if (ret == -2) {
                fprintf(stderr, "Failed to get sync status. Retry!\n");
                wup_status->bStatus = WUP_STATUS_errSTATE;
            } else {
                break;
            }
        } else {
#if DEBUG_PRINT
            printf("polySyncData - Got state: %d, status: %d\n",
                   wup_status->bState,
                   wup_status->bStatus);
#endif
        }

    } while (wup_status->bStatus == WUP_STATUS_errSTATE && (retries++) < 10);

    if (wup_status->bStatus != WUP_STATUS_OK || retries >= 10) {
        fprintf(stderr, "Failed to transfer all the data in %d times retries."
                "State: %d, Status: %d, WrittenBytes: %u\n",
                10, wup_status->bState, wup_status->bStatus, wup_status->u.dwWrittenBytes);
        return -1;
    }

    return 0;
}

#if defined(_WIN32)
#define POSITION(x) x
#else
#define POSITION(x) x.__pos
#endif

int polySendImageFile(Transport *transport, const char *fileName,
                      const char *swVersion, bool fUpdate = false,
                      bool fSync = false, bool fForced = false)
{
    FILE *fp;

    ssize_t read_len;
    ssize_t written_len;

    ssize_t ret;

    struct wup_status wup_status;

    struct wup_dnload_info dnload_info;
    int retries = 0;

    if (transport == NULL)
        return -EINVAL;

    fp = fopen(fileName, "rb");

    if (fp == NULL) {
        return -EINVAL;
    }

    //Get the file size
    fseek(fp, 0, SEEK_END);

    fpos_t ops;

    fgetpos(fp, &ops);

#if DEBUG_PRINT
    printf("File size is %lld\n", ops);
#endif

    //If the filesize is zero. Do not transfer it.
    if (POSITION(ops) == 0) {
        fclose(fp);
        return -1;
    }

    retries = 0;

    do {
        //Send the download information
        strncpy(dnload_info.sSwVersion, swVersion, sizeof(dnload_info.sSwVersion));
        dnload_info.dwImageSize = (unsigned int)(POSITION(ops));
        dnload_info.dwSyncBlockSize = fSync ? WUP_SYNC_BLOCK_SIZE : 0;    /* Do not sync in the transfer. */
        dnload_info.bForced = fForced ? 1 : 0;

        written_len = polySendControlInfo(transport,
                                          false,
                                          PLCM_USB_REQUEST_SET_INFORMATION,
                                          PLCM_USB_REQ_WUP_SET_DNLOAD_INFO,
                                          &dnload_info,
                                          sizeof(struct wup_dnload_info));

        if (written_len < 0) {
            fprintf(stderr, "Failed to set the download info\n");
            fclose(fp);
            return -2;
        }

        // Check the status
        memset(&wup_status, 0x00, sizeof(struct wup_status));

        read_len = polySendControlInfo(transport,
                                       true,
                                       PLCM_USB_REQUEST_GET_INFORMATION,
                                       PLCM_USB_REQ_WUP_GETSTATUS,
                                       &wup_status,
                                       sizeof(struct wup_status));

        if (read_len < 0) {
            fprintf(stderr, "Failed to read the status\n");
            fclose(fp);
            return -3;
        }

        if (wup_status.bStatus == WUP_STATUS_errSTATE) {
            // We need to send abort request
            written_len = polySendControlInfo(transport,
                                              false,
                                              PLCM_USB_REQUEST_SET_INFORMATION,
                                              PLCM_USB_REQ_WUP_ABORT,
                                              NULL,
                                              0);

            if (written_len < 0) {
                fprintf(stderr, "Failed to set abort\n");
                fclose(fp);
                return -4;
            }
        }

    } while (wup_status.bStatus == WUP_STATUS_errSTATE && (retries++) < 1);

    if (wup_status.bStatus != WUP_STATUS_OK &&
            wup_status.bState != WUP_STATE_dfuDNLOAD_IDLE) {
        fprintf(stderr, "Failed to set the download info."
                " Wrong status(%d) or state(%d)\n",
                wup_status.bStatus,
                wup_status.bState);
        fclose(fp);
        return -5;
    }

    //Set the pos to start
    fseek(fp, 0, SEEK_SET);

    //Start the data transfer
    char *buf;
    size_t buf_size = (1024 * 1024);

    buf = (char *)malloc(buf_size);

    if (!buf) {
        fprintf(stderr, "Failed to allocate buffer. Size: %zd\n", buf_size);
        fclose(fp);
        return -6;
    }

    size_t total_len = 0;

    size_t sync_block_remain = fSync ? WUP_SYNC_BLOCK_SIZE : (int)POSITION(ops) + 1;

    while (1) {
        size_t try_read_len = sync_block_remain > buf_size ? buf_size : sync_block_remain;

        // Try to read file data.
        read_len = fread(buf, sizeof(char), try_read_len, fp);

        if (read_len <= 0) {
            // Reach the end of the file.
            break;
        }

        // Send the data through USB
        written_len = transport->Write(buf, read_len);

        if (written_len < read_len) {
            fprintf(stderr, "Failed to write all the data. Written length : %zd, all data : %zd\n",
                    written_len, read_len);
            break;
        }

        sync_block_remain -= read_len;
        total_len += written_len;

        if (sync_block_remain == 0) {
            // Reach the sync point
            ret = polySyncData(transport, &wup_status);

            if (ret < 0) {
                fprintf(stderr, "Failed to sync data in transfer\n");
                free(buf);
                fclose(fp);
                return -7;
            }

            if (wup_status.bStatus != WUP_STATUS_OK ||
                    wup_status.u.dwWrittenBytes != total_len) {
                fprintf(stderr, "Something wrong in the transferring, error is (%d)"
                        " BytesWritten: %u\n",
                        wup_status.bStatus, wup_status.u.dwWrittenBytes);
                free(buf);
                fclose(fp);
                return -8;
            }

            //Reset the sync block remain
            sync_block_remain = fSync ? WUP_SYNC_BLOCK_SIZE : (int)POSITION(ops) + 1;
        }
    }

    //Free the buffer
    free(buf);

#if DEBUG_PRINT
    printf("total_len is %zu\n", total_len);
#endif
    fclose(fp);

    ret = polySyncData(transport, &wup_status);

    if (ret < 0) {
        fprintf(stderr, "Failed to do sync\n");
        return -9;
    }

    if (wup_status.bStatus != WUP_STATUS_OK ||
            wup_status.u.dwWrittenBytes != total_len) {
        fprintf(stderr, "Something wrong in the transferring, error is (%d)"
                " writtenBytes: %d\n",
                wup_status.bStatus, wup_status.u.dwWrittenBytes);
        return -10;
    }

    //Try to do integration check.

    char md5_sum[40];

    ret = polyGenerateMD5Sum(fileName, md5_sum);

    if (ret != 0) {
        fprintf(stderr, "Failed to generate the MD5 Sum\n");
        return -11;
    }

    //Send the MD5 sum for verification
    written_len = polySendControlInfo(transport,
                                      false,
                                      PLCM_USB_REQUEST_SET_INFORMATION,
                                      PLCM_USB_REQ_WUP_INT_CHECK,
                                      md5_sum,
                                      strnlen(md5_sum, sizeof(md5_sum)) + 1);

    if (written_len < 0) {
        fprintf(stderr, "Failed to issue the md5sum control msg: %s\n", md5_sum);
        return -12;
    }

    // Check the status
    memset(&wup_status, 0x00, sizeof(struct wup_status));

    read_len = polySendControlInfo(transport,
                                   true,
                                   PLCM_USB_REQUEST_GET_INFORMATION,
                                   PLCM_USB_REQ_WUP_GETSTATUS,
                                   &wup_status,
                                   sizeof(struct wup_status));

    if (read_len < 0) {
        fprintf(stderr, "Failed to read the status\n");
        return -13;
    }

    if (wup_status.bStatus != WUP_STATUS_OK) {
        fprintf(stderr, "Integration Checking Failed. status(%d) state(%d)\n",
                wup_status.bStatus,
                wup_status.bState);
        return -14;
    }

    if (!fUpdate)
        return 0;

    //Try to start the update
    written_len = polySendControlInfo(transport,
                                      false,
                                      PLCM_USB_REQUEST_SET_INFORMATION,
                                      PLCM_USB_REQ_WUP_START_UPDATE,
                                      NULL,
                                      0);

    if (written_len < 0) {
        fprintf(stderr, "Failed to issue UPDATE START request\n");
        return -14;
    }

    transport->Wait(5000);

    // Check the status
    memset(&wup_status, 0x00, sizeof(struct wup_status));

    read_len = polySendControlInfo(transport,
                                   true,
                                   PLCM_USB_REQUEST_GET_INFORMATION,
                                   PLCM_USB_REQ_WUP_GETSTATUS,
                                   &wup_status,
                                   sizeof(struct wup_status));

    if (read_len < 0) {
        fprintf(stderr, "Failed to read the status\n");
        return -15;
    }

    if (wup_status.bStatus != WUP_STATUS_OK) {
        fprintf(stderr, "Update Starting Failed. status(%d) state(%d)\n",
                wup_status.bStatus,
                wup_status.bState);
        return -16;
    }

#if DEBUG_PRINT
    printf("Current status: %d, state: %d\n",
           wup_status.bStatus,
           wup_status.bState);
#endif

    return 0;
}
