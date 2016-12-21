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

typedef int(*usb_file_transfer_func)(Transport *, const char *, const char *);

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
	int handle;
	int done = 0;
	char pattern[512];
	int count;

	snprintf(pattern, sizeof(pattern), "%s\\*.*", dirName);

	count = 0;

	if ((handle = _findfirst(pattern, &file_find)) != -1) {
		while (!(done = _findnext(handle, &file_find))) {
			if (strcmp(file_find.name, "..") == 0) {
				continue;
			}

			snprintf(pattern, sizeof(pattern), "%s\\%s", dirName, file_find.name);
			if (file_find.attrib == _A_SUBDIR) {
				printf("[Dir]:\t%s\\%s\n", dirName, file_find.name);
				count += traverse_directory(pattern, callback, transport, totalCount);
			}
			else {
				printf("[File]:\t%s\\%s\n", dirName, file_find.name);
				(*totalCount)++;
				if (!callback(transport, pattern, file_find.name))
					count++;
			}
		}
		_findclose(handle);
	}

	return count;
}

int polyGenerateMD5Sum(const char *fileName, char *md5sum)
{
	int retValue = 0;
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
int buf_size = 1024 * 1024;

struct setup_packet {
	unsigned char bRequestType;
	unsigned char bRequest;
	unsigned short wValue;
	unsigned short wIndex;
	unsigned short wLength;
};

#define PLCM_USB_REQUEST_SET_INFORMATION	0x01
#define PLCM_USB_REQUEST_GET_INFORMATION	0x81

#define PLCM_USB_REQUEST_VALUE_IMG_LENGTH		0x0001
#define PLCM_USB_REQUEST_VALUE_IMG_NAME			0x0002
#define PLCM_USB_REQUEST_VALUE_IMG_MD5_SUM		0x0003
#define PLCM_USB_REQUEST_VALUE_STATUS			0x0004
#define PLCM_USB_REQUEST_VALUE_WRITTEN_BYTES	0x0005

int polySendControlInfo(Transport *transport, bool is_in_direction,
	unsigned char request, unsigned short value, void *data, unsigned int len)
{
	struct setup_packet setup;
	
	memset(&setup, 0x00, sizeof(struct setup_packet));

	setup.bRequest = request;
	setup.wValue = value;
	setup.wLength = len;

	return transport->ControlIO(is_in_direction, &setup, data, len);
}

int polySendImageFile(Transport *transport, const char *fileName, const char *destFileName)
{
	FILE *fp;

	int read_len;
	int write_len;

	int ret;

	int status;

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

	printf("File size is %lld\n", ops);

	//If the filesize is zero. Do not transfer it.
	if (ops == 0) {
		fclose(fp);
		return -1;
	}

	//Send the image filesize
	char msg[64];

	unsigned int size = (unsigned int)ops;
	memcpy(msg, &size, sizeof(size));

	write_len = polySendControlInfo(transport,
		false,
		PLCM_USB_REQUEST_SET_INFORMATION,
		PLCM_USB_REQUEST_VALUE_IMG_LENGTH,
		msg,
		sizeof(size));

	if (write_len < 0) {
		fprintf(stderr, "Failed to set the image size\n");
		fclose(fp);
		return -1;
	}

	read_len = polySendControlInfo(transport,
		true,
		PLCM_USB_REQUEST_GET_INFORMATION,
		PLCM_USB_REQUEST_VALUE_STATUS,
		&status,
		sizeof(status));

	if (read_len < 0) {
		fprintf(stderr, "Failed to read the status\n");
		fclose(fp);
		return -1;
	}

	if (status != 0) {
		fprintf(stderr, "Status error in set the image size: %d\n",
			status);
		fclose(fp);
		return -1;
	}

	//Send the image name
	int msg_count = snprintf(msg, sizeof(msg), "%s", destFileName);
	msg[msg_count + 1] = '\0';

	write_len = polySendControlInfo(transport,
		false,
		PLCM_USB_REQUEST_SET_INFORMATION,
		PLCM_USB_REQUEST_VALUE_IMG_NAME,
		msg,
		msg_count + 1);

	if (write_len < 0) {
		fprintf(stderr, "Failed to issue the control transer, msg: %s\n", msg);
		fclose(fp);
		return -1;
	}

	read_len = polySendControlInfo(transport,
		true,
		PLCM_USB_REQUEST_GET_INFORMATION,
		PLCM_USB_REQUEST_VALUE_STATUS,
		&status,
		sizeof(status));

	if (read_len < 0) {
		fprintf(stderr, "Failed to read the status\n");
		fclose(fp);
		return -1;
	}

	if (status != 0) {
		fprintf(stderr, "Status error in set the image file: %d\n",
			status);
		fclose(fp);
		return -1;
	}

	//Set the pos to start
	fseek(fp, 0, SEEK_SET);

	int total_len = 0;

	while ((read_len = fread(buf, sizeof(char), buf_size, fp)) > 0) {
		total_len += read_len;

		write_len = transport->Write(buf, read_len);
		if (write_len < read_len) {
			fprintf(stderr, "Failed to write all the data. Written length : %d, all data : %d\n",
				write_len, read_len);
			break;
		}
	}

	printf("total_len is %d\n", total_len);
	fclose(fp);

	int retries = 0;

	int written_bytes = 0;

	do {
		Sleep(100);

		ret = polySendControlInfo(transport,
			true,
			PLCM_USB_REQUEST_GET_INFORMATION,
			PLCM_USB_REQUEST_VALUE_WRITTEN_BYTES,
			&written_bytes,
			4);

		if (ret < 0) {
			return -1;
		}

		printf("Got written_bytes: %d\n", written_bytes);

	} while (written_bytes < total_len && (retries++) < 10);

	if (written_bytes != total_len && retries >= 10) {
		fprintf(stderr, "Failed to transfer all the data in %d times retries\n", 10);
		return -1;
	}

	char md5_sum[40];

	ret = polyGenerateMD5Sum(fileName, md5_sum);
	if (ret < 0) {
		fprintf(stderr, "Failed to generate the MD5 Sum\n");
		return -1;
	}

	//Send the MD5 sum for verification
	write_len = polySendControlInfo(transport,
		false,
		PLCM_USB_REQUEST_SET_INFORMATION,
		PLCM_USB_REQUEST_VALUE_IMG_MD5_SUM,
		md5_sum,
		strnlen(md5_sum, sizeof(md5_sum)) + 1);

	if (write_len < 0) {
		fprintf(stderr, "Failed to issue the md5sum control msg: %s\n", md5_sum);
		return -1;
	}

	ret = polySendControlInfo(transport,
		true,
		PLCM_USB_REQUEST_GET_INFORMATION,
		PLCM_USB_REQUEST_VALUE_STATUS,
		&status,
		sizeof(status));

	if (ret < 0) {
		fprintf(stderr, "Failed to read out the status\n");
		return -1;
	}

	if (status != 0) {
		fprintf(stderr, "MD5 checking failed. status: %d\n", status);
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	buf = (char *)malloc(buf_size);

	if (buf == NULL)
		return -1;

	Transport *transport = usb_open(on_adb_device_found);

#if 1
	char *base_dir = "c:\\aaa2";

	if (argc < 2) {
		fprintf(stderr, "Invaild argument!\n");
		fprintf(stderr, "Usage: usb_win_update.exe [DIRECTORY]\n");
		fprintf(stderr, "Use the default directory: %s\n", base_dir);
	}
	else {
		base_dir = argv[1];
	}

	int total_file_count = 0;

	int file_count = traverse_directory(base_dir, polySendImageFile, transport, &total_file_count);

	printf("total file count: %d, transferred count: %d\n", total_file_count, file_count);
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
