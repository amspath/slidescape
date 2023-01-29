#pragma once
#ifndef CRC32_H
#define CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

unsigned int crc32(unsigned char *buffer, int len);

unsigned int crc32_skip_carriage_return(unsigned char *buffer, int len);

#ifdef __cplusplus
}
#endif

#endif //CRC32_H
