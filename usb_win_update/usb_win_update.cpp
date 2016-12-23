// usb_win_update.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <io.h>

#include <Windows.h>

#include "usb.h"

#define DEBUG_PRINT 0

typedef int(*usb_file_transfer_func)(Transport *, const char *);

int on_adb_device_found(usb_ifc_info *info)
{
	printf("We found an adb device\n");
	printf("\tVendor ID: 0x%04x\n", info->dev_vendor);
	printf("\tProduct ID: 0x%04x\n", info->dev_product);
	printf("\tSerial Number: %s\n", info->serial_number);

	return 0;
}

int traverse_directory(const char *dirName,
	usb_file_transfer_func callback,
	Transport *transport,
	int *totalCount)
{
	struct _finddata_t file_find;
	intptr_t handle;
	int done = 0;
	char pattern[512];
	int count;
	int ret;

	snprintf(pattern, sizeof(pattern), "%s\\*.*", dirName);

	count = 0;

	if ((handle = _findfirst(pattern, &file_find)) != -1) {
		while (!(done = _findnext(handle, &file_find))) {
			if (strcmp(file_find.name, "..") == 0) {
				continue;
			}

			snprintf(pattern, sizeof(pattern), "%s\\%s", dirName, file_find.name);
			if (file_find.attrib == _A_SUBDIR) {
#if DEBUG_PRINT
				printf("[Dir]:\t%s\\%s\n", dirName, file_find.name);
#endif
				count += traverse_directory(pattern, callback, transport, totalCount);
			}
			else {
#if DEBUG_PRINT
				printf("[File]:\t%s\\%s\n", dirName, file_find.name);
#endif
				(*totalCount)++;
				if ((ret = callback(transport, pattern)) == 0) {
					count++;
				}
				else {
					fprintf(stderr, "Failed to transfer file: \t%s\\%s. ret is %d\n",
						dirName, file_find.name, ret);
				}
			}
		}
		_findclose(handle);
	}

	return count;
}

size_t polyGenerateMD5Sum(const char *fileName, char *md5sum)
{
	size_t retValue = 0;
	char cmd_buf[512];
	char *tmp_results = "md5_result_tmp.txt";
	FILE *md5_fp;

	snprintf(cmd_buf, sizeof(cmd_buf), "C:\\bin\\md5sums -u %s > %s", fileName, tmp_results);

	system(cmd_buf);

	fopen_s(&md5_fp, tmp_results, "r");

	if (md5_fp == NULL)
		return -1;

	retValue = fread(md5sum, sizeof(char), 32, md5_fp);
	if (retValue != 32) {
		fclose(md5_fp);
		return -2;
	}

	md5sum[retValue] = '\0';

	fclose(md5_fp);

	_unlink(tmp_results);

	return retValue;

}

char *buf;
size_t buf_size = 1024 * 1024;

static UINT8 fSync = 1;

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
		Sleep(1000);

		//Try to do sync
		memset(wup_status, 0x00, sizeof(struct wup_status));

		ret = polySendControlInfo(transport,
			true,
			PLCM_USB_REQUEST_GET_INFORMATION,
			PLCM_USB_REQ_WUP_SYNC,
			wup_status,
			sizeof(struct wup_status));

		if (ret < 0) {
			if (GetLastError() == ERROR_SEM_TIMEOUT) {
				fprintf(stderr, "Failed to get sync status. Retry!\n");
				wup_status->bStatus = WUP_STATUS_errSTATE;
			}
			else {
				break;
			}
		}
		else {
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

int polySendImageFile(Transport *transport, const char *fileName)
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

	fopen_s(&fp, fileName, "rb");

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
	if (ops == 0) {
		fclose(fp);
		return -1;
	}

	retries = 0;

	do {
		//Send the download information
		strncpy_s(dnload_info.sSwVersion, "1.3.0-110161", sizeof(dnload_info.sSwVersion));
		dnload_info.dwImageSize = (unsigned int)ops;
		dnload_info.dwSyncBlockSize = fSync ? WUP_SYNC_BLOCK_SIZE : 0;    /* Do not sync in the transfer. */
		dnload_info.bForced = 1;

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
	size_t total_len = 0;

	size_t sync_block_remain = fSync ? WUP_SYNC_BLOCK_SIZE : (int)ops + 1;

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
				fclose(fp);
				return -6;
			}

			if (wup_status.bStatus != WUP_STATUS_OK ||
				wup_status.u.dwWrittenBytes != total_len) {
				fprintf(stderr, "Something wrong in the transferring, error is (%d)"
					" BytesWritten: %u\n",
					wup_status.bStatus, wup_status.u.dwWrittenBytes);
				fclose(fp);
				return -7;
			}

			//Reset the sync block remain
			sync_block_remain = fSync ? WUP_SYNC_BLOCK_SIZE : (int)ops + 1;
		}
	}

#if DEBUG_PRINT
	printf("total_len is %zu\n", total_len);
#endif
	fclose(fp);

	ret = polySyncData(transport, &wup_status);

	if (ret < 0) {
		fprintf(stderr, "Failed to do sync\n");
		return -8;
	}

	if (wup_status.bStatus != WUP_STATUS_OK ||
		wup_status.u.dwWrittenBytes != total_len) {
		fprintf(stderr, "Something wrong in the transferring, error is (%d)"
			" writtenBytes: %d\n",
			wup_status.bStatus, wup_status.u.dwWrittenBytes);
		return -9;
	}

	//Try to do integration check.

	char md5_sum[40];

	ret = polyGenerateMD5Sum(fileName, md5_sum);
	if (ret < 0) {
		fprintf(stderr, "Failed to generate the MD5 Sum\n");
		return -10;
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
		return -11;
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
		return -12;
	}

	if (wup_status.bStatus != WUP_STATUS_OK) {
		fprintf(stderr, "Integration Checking Failed. status(%d) state(%d)\n",
			wup_status.bStatus,
			wup_status.bState);
		return -13;
	}

	return 0;
}

int polyStartDFU(Transport *transport, const char *fileName) {
	int ret;
	ssize_t read_len, written_len;
	struct wup_status wup_status;

	ret = polySendImageFile(transport, fileName);

	if (ret) {
		fprintf(stderr, "Failed to send the image file to device. ret: %d\n",
			ret);
		return -1;
	}

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

	Sleep(5000);

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

int main(int argc, char *argv[])
{
	buf = (char *)malloc(buf_size);

	if (buf == NULL)
		return -1;

	Transport *transport = usb_open(on_adb_device_found);

#if 1
	//char *base_dir = "c:\\aaa";
	char *base_dir = "C:\\Users\\test\\Downloads\\data";

	unsigned char fUpdate = 0;

	if (argc < 2) {
		fprintf(stderr, "Invaild argument!\n");
		fprintf(stderr, "Usage: usb_win_update.exe [DIRECTORY] SyncFlag (default 0) UpdateFlag (default 0)\n");
		fprintf(stderr, "Use the default directory: %s\n", base_dir);
	}
	else {
		base_dir = argv[1];
	}

	if (argc >= 3) {
		fSync = atoi(argv[2]);
	}

	if (argc >= 4) {
		fUpdate = atoi(argv[3]);
	}

	printf("Sync mode: %d, Update mode: %d\n",
		fSync, fUpdate);

	int i = 0;

	int pass_count = 0;
	int failure_count = 0;

	while (i++ < 1) {
		int total_file_count = 0;

		usb_file_transfer_func pFunc;
		if (fUpdate)
			pFunc = polyStartDFU;
		else
			pFunc = polySendImageFile;

		int file_count = traverse_directory(base_dir, pFunc, transport, &total_file_count);

		printf("%d times test: total file count: %d, transferred count: %d\n",
			i, total_file_count, file_count);

		if (total_file_count != file_count)
			failure_count++;
		else
			pass_count++;
	}

	printf("Test results: passed: %d, failed: %d\n",
		pass_count, failure_count);
#else

	//int count = snprintf(buf, 1024, "%s", "zhangjie");
	//buf[count + 1] = '\0';
	memset(buf, 0xFF, buf_size);

	int count = 1023;
	if (argc >= 2)
		count = atoi(argv[1]);

	printf("We are going to transfer %d bytes\n", count);

	transport->Write(buf, count);

	//transport->Write(buf, 0);
#endif

	free(buf);

    return 0;
}
