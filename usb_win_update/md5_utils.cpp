#include "stdafx.h"
#include <stdlib.h>
#include <stdio.h>

#include <openssl/md5.h>

#include "md5_utils.h"

#ifndef MD5_LENGTH
#define MD5_LENGTH	16
#endif // !MD5_LENGTH

#define FILE_READ_BUFFER_SIZE	(1024 * 1024)

int polyGenerateMD5Sum(const char *fileName, char *md5sum)
{
    size_t retValue = 0;
    FILE *file_fp;
    unsigned char md5[17] = { 0 };
    MD5_CTX md5_ctx;

    file_fp = fopen(fileName, "rb");

    if (file_fp == NULL)
        return -1;

    unsigned char* pData = (unsigned char*)malloc(FILE_READ_BUFFER_SIZE);

    if (pData == NULL) {
        fprintf(stderr, "Failed to allocate the buffer\n");
        return -2;
    }

    MD5_Init(&md5_ctx);

    while ((retValue = fread(pData, sizeof(unsigned char), FILE_READ_BUFFER_SIZE, file_fp)) > 0)
        MD5_Update(&md5_ctx, pData, retValue);

    MD5_Final(md5, &md5_ctx);

    free(pData);
    fclose(file_fp);

    int i = 0;

    for (; i < MD5_LENGTH; i++)
        snprintf(md5sum + 2 * i, 4, "%02x", md5[i]);

    return 0;
}
