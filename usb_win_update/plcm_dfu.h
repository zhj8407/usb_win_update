#pragma once

#ifndef _PLCM_USB_DFU_H
#define _PLCM_USB_DFU_H

#include "transport.h"

typedef void(*progress_notify)(const char *fileName, ssize_t total_bytes, ssize_t written_bytes, bool done, bool error);

typedef int(*usb_file_transfer_func)(Transport *, const char *, const char *, int, bool, bool, bool, progress_notify);

int polySendImageFile(Transport *transport, const char *fileName,
                      const char *swVersion, int buffer_size, bool fUpdate,
                      bool fSync, bool fForce,
                      progress_notify notify_callback);

#endif // !_PLCM_USB_DFU_H
