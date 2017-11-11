// usb_win_update.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <memory>

#include "usb.h"
#include "plcm_dfu.h"

#if defined(_WIN32)
#include <io.h>
#include <Windows.h>
#else
#include <dirent.h>
#include <unistd.h>
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

void progress_show(const char *fileName,
    ssize_t total_bytes,
    ssize_t written_bytes,
    bool done,
    bool error)
{
    static char stored_FileName[128] = { 0 };
    static int count = 0;
    float percent;

    if (strncmp(fileName, stored_FileName, sizeof(stored_FileName))) {
        strncpy(stored_FileName, fileName, sizeof(stored_FileName));
        printf("\nCount   : %d\n", ++count);
        printf("File Name: %s\n", fileName);
        printf("File Size: %ld\n", (long int)total_bytes);
    }

    printf("\rProgress: [");
    percent = (float)written_bytes / (float)total_bytes * 100;

    for (int i = 0; i < 50; i++) {
        if (i < (int)percent / 2)
            printf("=");
        else
            printf(" ");
    }

    printf("] %.1f%%", percent);

    if (done)
        printf("  Done\n");

    if (error)
        printf("  Error\n");
}

static bool is_directory(const char *path)
{
    bool directory = true;
#if defined(_WIN32)
    struct _finddata_t file_find;
    intptr_t handle;

    handle = _findfirst(path, &file_find);
    if (handle == -1)
        return directory;

    directory = (file_find.attrib == _A_SUBDIR);
#else
    struct stat path_stat;
    if (stat(path, &path_stat) != 0)
        return directory;

    directory = (S_ISDIR(path_stat.st_mode) == 1);
#endif

    return directory;
}

int traverse_directory(const char *dirName,
    usb_file_transfer_func callback,
    Transport *transport,
    const char *swVersion,
    bool fUpdate,
    bool fSync,
    bool fForced,
    int buffer_size,
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
                                            buffer_size,
                                            totalCount);
            } else {
#if DEBUG_PRINT
                printf("[File]:\t%s\\%s\n", dirName, file_find.name);
#endif
                (*totalCount)++;

                if ((ret = callback(transport, pattern, swVersion, buffer_size, fUpdate, fSync, fForced, progress_show)) == 0) {
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
                                        buffer_size,
                                        totalCount);
        } else if (de->d_type == DT_REG) {
#if DEBUG_PRINT
            printf("[File]:\t%s/%s\n", dirName, de->d_name);
#endif
            (*totalCount)++;

            if ((ret = callback(transport, pattern, swVersion, buffer_size, fUpdate, fSync, fForced, progress_show)) == 0) {
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

void close_transport(Transport *transport)
{
    if (transport) {
        transport->Close();
        delete transport;
    }
}

int main(int argc, char *argv[])
{
    Transport *t = usb_open(on_plcm_dfu_device_found);

    if (t == NULL) {
        fprintf(stderr, "Failed to find the available device\n");
        return -1;
    }

    std::unique_ptr<Transport, decltype(close_transport)*> transport(t, close_transport);

#if 1
    char const *base_dir = "D:\\vsg_image";
    //char const *base_dir = "/Users/jiezhang/patches";
    //char const *base_dir = "/Volumes/Untitled/image";

    int buffer_size = 16;   // KB
    bool fSync = true;
    bool fUpdate = false;
    bool fForced = true;

    char devinfo[MAX_DEV_INFO_LENGTH];

    char const *version = "1.3.0-110230";

    if (argc < 2) {
        fprintf(stderr, "Invaild argument!\n");
        fprintf(stderr, "Usage: usb_win_update.exe [DIRECTORY|FILENAME] [BufferSize(16)] [ForceFlag(1)]"
                " [UpdateFlag(0)] [SyncFlag(1)] [VersionNumber(1.3.0-110230)]\n");
        fprintf(stderr, "Use the default directory: %s\n", base_dir);
    } else {
        base_dir = argv[1];
    }

    if (strcmp(argv[1], "-d") == 0) {
        if (!polyGetDeviceInfo(transport.get(), devinfo))
            printf("Got Device Info:\n%s\n", devinfo);
        return 0;
    }

    if (argc >= 3) {
        buffer_size = atoi(argv[2]);
    }

    if (argc >= 4) {
        fForced = (atoi(argv[3]) != 0);
    }

    if (argc >= 5) {
        fUpdate = (atoi(argv[4]) != 0);
    }

    if (argc >= 6) {
        fSync = (atoi(argv[5]) != 0);
    }

    if (argc >= 7) {
        version = argv[6];
    }

    if (buffer_size > 1024)
        buffer_size = 1024;

    printf("Buffer size: %d KB, Sync mode: %d, Update mode: %d, Forced: %d, Version: %s\n",
           buffer_size, fSync, fUpdate, fForced, version);

    int i = 0;

    int pass_count = 0;
    int failure_count = 0;

    while (i++ < 1) {
        int total_file_count = 0;
        int file_count = 0;

        if (is_directory(base_dir)) {
            file_count = traverse_directory(base_dir,
                polySendImageFile,
                transport.get(),
                version,
                fUpdate,
                fSync,
                fForced,
                buffer_size,
                &total_file_count);
        } else {
            total_file_count = 1;
            if (!polySendImageFile(transport.get(), base_dir, version, buffer_size, fUpdate, fSync, fForced, progress_show))
                file_count = 1;
        }

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

    return 0;
}
