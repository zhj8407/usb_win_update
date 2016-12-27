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

#if defined(_WIN32)
	fopen_s(&file_fp, fileName, "rb");
#else
    file_fp = fopen(fileName, "rb");
#endif

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

int polyGenerateMD5SumExt(const char *fileName, char *md5sum)
{
	size_t retValue = 0;
	char cmd_buf[512];
	char const *tmp_results = "md5_result_tmp.txt";
	FILE *md5_fp;

#if defined(_WIN32)
	snprintf(cmd_buf, sizeof(cmd_buf), "C:\\bin\\md5sums -u %s > %s", fileName, tmp_results);
#else
	snprintf(cmd_buf, sizeof(cmd_buf), "/usr/bin/md5sums %s > %s", fileName, tmp_results);
#endif

	system(cmd_buf);

#if defined(_WIN32)
	fopen_s(&md5_fp, tmp_results, "r");
#else
    md5_fp = fopen(tmp_results, "r");
#endif

	if (md5_fp == NULL)
		return -1;

	retValue = fread(md5sum, sizeof(char), 32, md5_fp);
	if (retValue != 32) {
		fclose(md5_fp);
		return -2;
	}

	md5sum[retValue] = '\0';

	fclose(md5_fp);

#if defined(_WIN32)
	_unlink(tmp_results);
#else
	unlink(tmp_results);
#endif

	return 0;
}
