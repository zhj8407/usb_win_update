// usb_win_update.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "usb.h"
#include "plcm_dfu.h"

#if defined(_WIN32)
#include <io.h>
#include <Windows.h>
#else
#include <dirent.h>
#endif

#define DEBUG_PRINT 0

#define PLCM_VENDOR_ID                  0x095D
#define PLCM_DFU_INTERFACE_CLASS        0xFF
#define PLCM_DFU_INTERFACE_SUBCLASS     0xF0
#define PLCM_DFU_INTERFACE_PROTOCOL     0x00

int on_plcm_dfu_device_found(usb_ifc_info *info)
{
    if (info->dev_vendor == PLCM_VENDOR_ID &&
            info->ifc_class == PLCM_DFU_INTERFACE_CLASS &&
            info->ifc_subclass == PLCM_DFU_INTERFACE_SUBCLASS &&
            info->ifc_protocol == PLCM_DFU_INTERFACE_PROTOCOL &&
            !info->has_bulk_in &&
            info->has_bulk_out) {
        printf("We found an Polycom Device\n");
        printf("\tVendor ID: 0x%04x\n", info->dev_vendor);
        printf("\tProduct ID: 0x%04x\n", info->dev_product);
        printf("\tSerial Number: %s\n", info->serial_number);
        return 0;
    }

    return -1;
}

int traverse_directory(const char *dirName,
                       usb_file_transfer_func callback,
                       Transport *transport,
                       const char *swVersion,
                       bool fUpdate,
                       bool fSync,
                       bool fForced,
                       int *totalCount)
{
#if defined(_WIN32)
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
                count += traverse_directory(pattern,
                                            callback,
                                            transport,
                                            swVersion,
                                            fUpdate,
                                            fSync,
                                            fForced,
                                            totalCount);
            } else {
#if DEBUG_PRINT
                printf("[File]:\t%s\\%s\n", dirName, file_find.name);
#endif
                (*totalCount)++;

                if ((ret = callback(transport, pattern, swVersion, fUpdate, fSync, fForced)) == 0) {
                    count++;
                } else {
                    fprintf(stderr, "Failed to transfer file: \t%s\\%s. ret is %d\n",
                            dirName, file_find.name, ret);
                }
            }
        }

        _findclose(handle);
    }

    return count;
#else
    DIR *basedir;
    struct dirent *de;
    char pattern[512];
    int count;
    int ret;

    count = 0;
    basedir = opendir(dirName);

    if (basedir == 0)
        return 0;

    while ((de = readdir(basedir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        snprintf(pattern, sizeof(pattern), "%s/%s", dirName, de->d_name);

        if (de->d_type == DT_DIR) {
#if DEBUG_PRINT
            printf("[Dir]:\t%s/%s\n", dirName, de->d_name);
#endif
            count += traverse_directory(pattern,
                                        callback,
                                        transport,
                                        swVersion,
                                        fUpdate,
                                        fSync,
                                        fForced,
                                        totalCount);
        } else if (de->d_type == DT_REG) {
#if DEBUG_PRINT
            printf("[File]:\t%s/%s\n", dirName, de->d_name);
#endif
            (*totalCount)++;

            if ((ret = callback(transport, pattern, swVersion, fUpdate, fSync, fUpdate)) == 0) {
                count++;
            } else {
                fprintf(stderr, "Failed to transfer file: \t%s/%s. ret is %d\n",
                        dirName, de->d_name, ret);
            }
        }
    }

    closedir(basedir);

    return count;
#endif
}

int main(int argc, char *argv[])
{
    Transport *transport = usb_open(on_plcm_dfu_device_found);

    if (transport == NULL) {
        fprintf(stderr, "Failed to find the available device\n");
        return -1;
    }

#if 1
    //char *base_dir = "c:\\aaa";
    char const *base_dir = "C:\\Users\\test\\Downloads\\data";
    //char *base_dir = "C:\\Python27amd64";

    bool fSync = true;
    bool fUpdate = false;
    bool fForced = true;

    char const *version = "1.3.0-110176";

    if (argc < 2) {
        fprintf(stderr, "Invaild argument!\n");
        fprintf(stderr, "Usage: usb_win_update.exe [DIRECTORY] [ForceFlag(1)]"
                " [UpdateFlag(0)] [VersionNumber(1.3.0-110176)]\n");
        fprintf(stderr, "Use the default directory: %s\n", base_dir);
    } else {
        base_dir = argv[1];
    }

    if (argc >= 3) {
        fForced = atoi(argv[2]) != 0;
    }

    if (argc >= 4) {
        fUpdate = atoi(argv[3]) != 0;
    }

    if (argc >= 5) {
        version = argv[4];
    }

    printf("Sync mode: %d, Update mode: %d, Forced: %d, Version: %s\n",
           fSync, fUpdate, fForced, version);

    int i = 0;

    int pass_count = 0;
    int failure_count = 0;

    while (i++ < 1) {
        int total_file_count = 0;

        int file_count = traverse_directory(base_dir,
                                            polySendImageFile,
                                            transport,
                                            version,
                                            fUpdate,
                                            fSync,
                                            fForced,
                                            &total_file_count);

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

#if 1
    char *buf;
    size_t buf_size = 1024 * 1024;
    buf = (char *)malloc(buf_size);
    //int count = snprintf(buf, 1024, "%s", "zhangjie");
    //buf[count + 1] = '\0';
    memset(buf, 0xFF, buf_size);

    int count = 1023;

    if (argc >= 2)
        count = atoi(argv[1]);

    printf("We are going to transfer %d bytes\n", count);

    transport->Write(buf, count);

    free(buf);

    //transport->Write(buf, 0);
#else
    char md5_sum[40];

    int ret = polyGenerateMD5Sum("C:\\Users\\jiezhang\\Downloads\\55.0.2883.87_chrome_installer_x64.exe", md5_sum);

    if (ret == 0)
        printf("MD5 sum is %s\n", md5_sum);

    ret = polyGenerateMD5SumExt("C:\\Users\\jiezhang\\Downloads\\55.0.2883.87_chrome_installer_x64.exe", md5_sum);

    if (ret == 0)
        printf("MD5 sum by external tool is %s\n", md5_sum);

#endif
#endif

    transport->Close();

    delete transport;

    return 0;
}
