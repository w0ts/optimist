/******************************************************************************
 *
 *  Copyright 2014 The Android Open Source Project
 *  Copyright 2003 - 2004 Open Interface North America, Inc. All rights
 *                        reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/*******************************************************************************
  $Revision: #1 $
 ******************************************************************************/

/**
@file
Checksum and header-related functions.

@ingroup codec_internal
*/

/**
@addtogroup codec_internal
@{
*/

#include "oi_assert.h"
#include "oi_codec_sbc_private.h"

/* asdasd */

#define USE_NIBBLEWISE_CRC

/* #define PRINT_SAMPLES */
/* #define PRINT_SCALEFACTORS */
/* #define DEBUG_CRC */

/*
 * CRC-8 table for X^8 + X^4 + X^3 + X^2 + 1; byte-wise lookup
 */
#ifdef USE_WIDE_CRC
/* Save space if a char is 16 bits, such as on the C54x */
const OI_BYTE crc8_wide[128] = {
        0x001d, 0x3a27, 0x7469, 0x4e53, 0xe8f5, 0xd2cf, 0x9c81, 0xa6bb, 0xcdd0, 0xf7ea, 0xb9a4,
        0x839e, 0x2538, 0x1f02, 0x514c, 0x6b76, 0x879a, 0xbda0, 0xf3ee, 0xc9d4, 0x6f72, 0x5548,
        0x1b06, 0x213c, 0x4a57, 0x706d, 0x3e23, 0x0419, 0xa2bf, 0x9885, 0xd6cb, 0xecf1, 0x130e,
        0x2934, 0x677a, 0x5d40, 0xfbe6, 0xc1dc, 0x8f92, 0xb5a8, 0xdec3, 0xe4f9, 0xaab7, 0x908d,
        0x362b, 0x0c11, 0x425f, 0x7865, 0x9489, 0xaeb3, 0xe0fd, 0xdac7, 0x7c61, 0x465b, 0x0815,
        0x322f, 0x5944, 0x637e, 0x2d30, 0x170a, 0xb1ac, 0x8b96, 0xc5d8, 0xffe2, 0x263b, 0x1c01,
        0x524f, 0x6875, 0xced3, 0xf4e9, 0xbaa7, 0x809d, 0xebf6, 0xd1cc, 0x9f82, 0xa5b8, 0x031e,
        0x3924, 0x776a, 0x4d50, 0xa1bc, 0x9b86, 0xd5c8, 0xeff2, 0x4954, 0x736e, 0x3d20, 0x071a,
        0x6c71, 0x564b, 0x1805, 0x223f, 0x8499, 0xbea3, 0xf0ed, 0xcad7, 0x3528, 0x0f12, 0x415c,
        0x7b66, 0xddc0, 0xe7fa, 0xa9b4, 0x938e, 0xf8e5, 0xc2df, 0x8c91, 0xb6ab, 0x100d, 0x2a37,
        0x6479, 0x5e43, 0xb2af, 0x8895, 0xc6db, 0xfce1, 0x5a47, 0x607d, 0x2e33, 0x1409, 0x7f62,
        0x4558, 0x0b16, 0x312c, 0x978a, 0xadb0, 0xe3fe, 0xd9c4,
};
#elif defined(USE_NIBBLEWISE_CRC)
const OI_BYTE crc8_narrow[16] = {0x00, 0x1d, 0x3a, 0x27, 0x74, 0x69, 0x4e, 0x53,
                                 0xe8, 0xf5, 0xd2, 0xcf, 0x9c, 0x81, 0xa6, 0xbb};
#else
const OI_BYTE crc8_narrow[256] = {
        0x00, 0x1d, 0x3a, 0x27, 0x74, 0x69, 0x4e, 0x53, 0xe8, 0xf5, 0xd2, 0xcf, 0x9c, 0x81, 0xa6,
        0xbb, 0xcd, 0xd0, 0xf7, 0xea, 0xb9, 0xa4, 0x83, 0x9e, 0x25, 0x38, 0x1f, 0x02, 0x51, 0x4c,
        0x6b, 0x76, 0x87, 0x9a, 0xbd, 0xa0, 0xf3, 0xee, 0xc9, 0xd4, 0x6f, 0x72, 0x55, 0x48, 0x1b,
        0x06, 0x21, 0x3c, 0x4a, 0x57, 0x70, 0x6d, 0x3e, 0x23, 0x04, 0x19, 0xa2, 0xbf, 0x98, 0x85,
        0xd6, 0xcb, 0xec, 0xf1, 0x13, 0x0e, 0x29, 0x34, 0x67, 0x7a, 0x5d, 0x40, 0xfb, 0xe6, 0xc1,
        0xdc, 0x8f, 0x92, 0xb5, 0xa8, 0xde, 0xc3, 0xe4, 0xf9, 0xaa, 0xb7, 0x90, 0x8d, 0x36, 0x2b,
        0x0c, 0x11, 0x42, 0x5f, 0x78, 0x65, 0x94, 0x89, 0xae, 0xb3, 0xe0, 0xfd, 0xda, 0xc7, 0x7c,
        0x61, 0x46, 0x5b, 0x08, 0x15, 0x32, 0x2f, 0x59, 0x44, 0x63, 0x7e, 0x2d, 0x30, 0x17, 0x0a,
        0xb1, 0xac, 0x8b, 0x96, 0xc5, 0xd8, 0xff, 0xe2, 0x26, 0x3b, 0x1c, 0x01, 0x52, 0x4f, 0x68,
        0x75, 0xce, 0xd3, 0xf4, 0xe9, 0xba, 0xa7, 0x80, 0x9d, 0xeb, 0xf6, 0xd1, 0xcc, 0x9f, 0x82,
        0xa5, 0xb8, 0x03, 0x1e, 0x39, 0x24, 0x77, 0x6a, 0x4d, 0x50, 0xa1, 0xbc, 0x9b, 0x86, 0xd5,
        0xc8, 0xef, 0xf2, 0x49, 0x54, 0x73, 0x6e, 0x3d, 0x20, 0x07, 0x1a, 0x6c, 0x71, 0x56, 0x4b,
        0x18, 0x05, 0x22, 0x3f, 0x84, 0x99, 0xbe, 0xa3, 0xf0, 0xed, 0xca, 0xd7, 0x35, 0x28, 0x0f,
        0x12, 0x41, 0x5c, 0x7b, 0x66, 0xdd, 0xc0, 0xe7, 0xfa, 0xa9, 0xb4, 0x93, 0x8e, 0xf8, 0xe5,
        0xc2, 0xdf, 0x8c, 0x91, 0xb6, 0xab, 0x10, 0x0d, 0x2a, 0x37, 0x64, 0x79, 0x5e, 0x43, 0xb2,
        0xaf, 0x88, 0x95, 0xc6, 0xdb, 0xfc, 0xe1, 0x5a, 0x47, 0x60, 0x7d, 0x2e, 0x33, 0x14, 0x09,
        0x7f, 0x62, 0x45, 0x58, 0x0b, 0x16, 0x31, 0x2c, 0x97, 0x8a, 0xad, 0xb0, 0xe3, 0xfe, 0xd9,
        0xc4};
#endif

#if defined(OI_DEBUG) || defined(PRINT_SAMPLES) || defined(PRINT_SCALEFACTORS)
#endif

#ifdef USE_WIDE_CRC
INLINE OI_CHAR crc_iterate(uint8_t oldcrc, uint8_t next) {
  OI_UINT crc;
  OI_UINT idx;
  idx = oldcrc ^ next;
  crc = crc8_wide[idx >> 1];
  if (idx % 2) {
    crc &= 0xff;
  } else {
    crc >>= 8;
  }

  return crc;
}

INLINE OI_CHAR crc_iterate_top4(uint8_t oldcrc, uint8_t next) {
  OI_UINT crc;
  OI_UINT idx;
  idx = (oldcrc ^ next) >> 4;
  crc = crc8_wide[idx >> 1];
  if (idx % 2) {
    crc &= 0xff;
  } else {
    crc >>= 8;
  }

  return (oldcrc << 4) ^ crc;
}

#else  // USE_WIDE_CRC

INLINE uint8_t crc_iterate_top4(uint8_t oldcrc, uint8_t next) {
  return (oldcrc << 4) ^ crc8_narrow[(oldcrc ^ next) >> 4];
}

#ifdef USE_NIBBLEWISE_CRC
INLINE uint8_t crc_iterate(uint8_t crc, uint8_t next) {
  crc = (crc << 4) ^ crc8_narrow[(crc ^ next) >> 4];
  crc = (crc << 4) ^ crc8_narrow[((crc >> 4) ^ next) & 0xf];

  return crc;
}

#else  // USE_NIBBLEWISE_CRC
INLINE uint8_t crc_iterate(uint8_t crc, uint8_t next) { return crc8_narrow[crc ^ next]; }

#endif  // USE_NIBBLEWISE_CRC

#endif  // USE_WIDE_CRC

PRIVATE uint8_t OI_SBC_CalculateChecksum(OI_CODEC_SBC_FRAME_INFO* frame, OI_BYTE const* data) {
  OI_UINT i;
  uint8_t crc = 0x0f;
  /* Count is the number of whole bytes subject to CRC. Actually, it's one
   * more than this number, because data[3] is the CRC field itself, which is
   * explicitly skipped. Since crc_iterate (should be) inlined, it's cheaper
   * spacewise to include the check in the loop. This shouldn't be much of a
   * bottleneck routine in the first place. */
  OI_UINT count = (frame->nrof_subbands * frame->nrof_channels / 2u) + 4;

  if (frame->mode == SBC_JOINT_STEREO && frame->nrof_subbands == 8) {
    count++;
  }

  for (i = 1; i < count; i++) {
    if (i != 3) {
      crc = crc_iterate(crc, data[i]);
    }
  }

  if (frame->mode == SBC_JOINT_STEREO && frame->nrof_subbands == 4) {
    crc = crc_iterate_top4(crc, data[i]);
  }

  return crc;
}

void OI_SBC_ExpandFrameFields(OI_CODEC_SBC_FRAME_INFO* frame) {
  frame->nrof_blocks = block_values[frame->blocks];
  frame->nrof_subbands = band_values[frame->subbands];

  frame->frequency = freq_values[frame->freqIndex];
  frame->nrof_channels = channel_values[frame->mode];
}

/**
 * Unrolled macro to copy 4 32-bit aligned 32-bit values backward in memory
 */
#define COPY4WORDS_BACK(_dest, _src) \
  do {                               \
    int32_t _a, _b, _c, _d;          \
    _a = *--(_src);                  \
    _b = *--(_src);                  \
    _c = *--(_src);                  \
    _d = *--(_src);                  \
    *--(_dest) = _a;                 \
    *--(_dest) = _b;                 \
    *--(_dest) = _c;                 \
    *--(_dest) = _d;                 \
  } while (0)

#if defined(USE_PLATFORM_MEMMOVE) || defined(USE_PLATFORM_MEMCPY)
#include <string.h>
#endif
PRIVATE void shift_buffer(SBC_BUFFER_T* dest, SBC_BUFFER_T* src, OI_UINT wordCount) {
#ifdef USE_PLATFORM_MEMMOVE
  memmove(dest, src, wordCount * sizeof(SBC_BUFFER_T));
#elif defined(USE_PLATFORM_MEMCPY)
  OI_ASSERT(((OI_CHAR*)(dest) - (OI_CHAR*)(src)) >= wordCount * sizeof(*dest));
  memcpy(dest, src, wordCount * sizeof(SBC_BUFFER_T));
#else
  OI_UINT n;
  int32_t* d;
  int32_t* s;
  n = wordCount / 4 / (sizeof(int32_t) / sizeof(*dest));
  OI_ASSERT((n * 4 * (sizeof(int32_t) / sizeof(*dest))) == wordCount);

  d = (void*)(dest + wordCount);
  s = (void*)(src + wordCount);

  do {
    COPY4WORDS_BACK(d, s);
  } while (--n);
#endif
}
/**
@}
*/
