#pragma once

#ifndef _PLCM_USB_DFU_H
#define _PLCM_USB_DFU_H

#include "transport.h"

typedef int(*usb_file_transfer_func)(Transport *, const char *, bool, bool, bool);

int polySendImageFile(Transport *transport, const char *fileName,
	bool fUpdate, bool fSync, bool fForced);

#endif // !_PLCM_USB_DFU_H
