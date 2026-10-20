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

 Dequantizer for SBC decoder; reconstructs quantized representation of subband
 samples.

 @ingroup codec_internal
 */

/**
@addtogroup codec_internal
@{
*/

/**
 This function is a fixed-point approximation of a modification of the following
 dequantization operation defined in the spec, as inferred from section 12.6.4:

 @code
 dequant = 2^(scale_factor+1) * ((raw * 2.0 + 1.0) / ((2^bits) - 1) - 1)

 2 <= bits <= 16
 0 <= raw < (2^bits)-1   (the -1 is because quantized values with all 1's are
 forbidden)

 -65535 < dequant < 65535
 @endcode

 The code below computes the dequantized value divided by a scaling constant
 equal to about 1.38. This constant is chosen to ensure that the entry in the
 dequant_long_scaled table for 16 bits is as accurate as possible, since it has
 the least relative precision available to it due to its small magnitude.

 This routine outputs in Q16.15 format.

 The helper array dequant_long is defined as follows:

 @code
 dequant_long_long[bits] = round(2^31 * 1/((2^bits - 1) / 1.38...)  for 2 <=
 bits <= 16
 @endcode


 Additionally, the table entries have the following property:

 @code
 dequant_long_scaled[bits] <= 2^31 / ((2^bits - 1))  for 2 <= bits <= 16
 @endcode

 Therefore

 @code
 d = 2 * raw + 1              1 <= d <= 2^bits - 2

 d' = d * dequant_long[bits]

                  d * dequant_long_scaled[bits] <= (2^bits - 2) * (2^31 /
 (2^bits - 1))
                  d * dequant_long_scaled[bits] <= 2^31 * (2^bits - 2)/(2^bits -
 1) < 2^31
 @endcode

 Therefore, d' doesn't overflow a signed 32-bit value.

 @code

 d' =~ 2^31 * (raw * 2.0 + 1.0) / (2^bits - 1) / 1.38...

 result = d' - 2^31/1.38... =~ 2^31 * ((raw * 2.0 + 1.0) / (2^bits - 1) - 1) /
 1.38...

 result is therefore a scaled approximation to dequant. It remains only to
 turn 2^31 into 2^(scale_factor+1). Since we're aiming for Q16.15 format,
 this is achieved by shifting right by (15-scale_factor):

  (2^31 * x) >> (15-scale_factor) =~ 2^(31-15+scale_factor) * x = 2^15 *
 2^(1+scale_factor) * x
 @endcode

 */

#include <oi_codec_sbc_private.h>

#ifndef SBC_DEQUANT_LONG_SCALED_OFFSET
#define SBC_DEQUANT_LONG_SCALED_OFFSET 1555931970
#endif

#ifndef SBC_DEQUANT_LONG_UNSCALED_OFFSET
#define SBC_DEQUANT_LONG_UNSCALED_OFFSET 2147483648
#endif

#ifndef SBC_DEQUANT_SCALING_FACTOR
#define SBC_DEQUANT_SCALING_FACTOR 1.38019122262781f
#endif

const uint32_t dequant_long_scaled[17] = {
        0,          0,
        0x1ee9e116, /* bits=2  0.24151243  1/3      * (1/1.38019122262781)
                       (0x00000008)*/
        0x0d3fa99c, /* bits=3  0.10350533  1/7      * (1/1.38019122262781)
                       (0x00000013)*/
        0x062ec69e, /* bits=4  0.04830249  1/15     * (1/1.38019122262781)
                       (0x00000029)*/
        0x02fddbfa, /* bits=5  0.02337217  1/31     * (1/1.38019122262781)
                       (0x00000055)*/
        0x0178d9f5, /* bits=6  0.01150059  1/63     * (1/1.38019122262781)
                       (0x000000ad)*/
        0x00baf129, /* bits=7  0.00570502  1/127    * (1/1.38019122262781)
                       (0x0000015e)*/
        0x005d1abe, /* bits=8  0.00284132  1/255    * (1/1.38019122262781)
                       (0x000002bf)*/
        0x002e760d, /* bits=9  0.00141788  1/511    * (1/1.38019122262781)
                       (0x00000582)*/
        0x00173536, /* bits=10 0.00070825  1/1023   * (1/1.38019122262781)
                       (0x00000b07)*/
        0x000b9928, /* bits=11 0.00035395  1/2047   * (1/1.38019122262781)
                       (0x00001612)*/
        0x0005cc37, /* bits=12 0.00017693  1/4095   * (1/1.38019122262781)
                       (0x00002c27)*/
        0x0002e604, /* bits=13 0.00008846  1/8191   * (1/1.38019122262781)
                       (0x00005852)*/
        0x000172fc, /* bits=14 0.00004422  1/16383  * (1/1.38019122262781)
                       (0x0000b0a7)*/
        0x0000b97d, /* bits=15 0.00002211  1/32767  * (1/1.38019122262781)
                       (0x00016150)*/
        0x00005cbe, /* bits=16 0.00001106  1/65535  * (1/1.38019122262781)
                       (0x0002c2a5)*/
};

const uint32_t dequant_long_unscaled[17] = {
        0,          0, 0x2aaaaaab, /* bits=2  0.33333333  1/3      (0x00000005)*/
        0x12492492,                /* bits=3  0.14285714  1/7      (0x0000000e)*/
        0x08888889,                /* bits=4  0.06666667  1/15     (0x0000001d)*/
        0x04210842,                /* bits=5  0.03225806  1/31     (0x0000003e)*/
        0x02082082,                /* bits=6  0.01587302  1/63     (0x0000007e)*/
        0x01020408,                /* bits=7  0.00787402  1/127    (0x000000fe)*/
        0x00808081,                /* bits=8  0.00392157  1/255    (0x000001fd)*/
        0x00402010,                /* bits=9  0.00195695  1/511    (0x000003fe)*/
        0x00200802,                /* bits=10 0.00097752  1/1023   (0x000007fe)*/
        0x00100200,                /* bits=11 0.00048852  1/2047   (0x00000ffe)*/
        0x00080080,                /* bits=12 0.00024420  1/4095   (0x00001ffe)*/
        0x00040020,                /* bits=13 0.00012209  1/8191   (0x00003ffe)*/
        0x00020008,                /* bits=14 0.00006104  1/16383  (0x00007ffe)*/
        0x00010002,                /* bits=15 0.00003052  1/32767  (0x0000fffe)*/
        0x00008001,                /* bits=16 0.00001526  1/65535  (0x0001fffc)*/
};

/** Scales x by y bits to the right, adding a rounding factor.
 */
#ifndef SCALE
#define SCALE(x, y) (((x) + (1 << ((y) - 1))) >> (y))
#endif

#ifdef DEBUG_DEQUANTIZATION

#include <math.h>

INLINE float dequant_float(uint32_t raw, OI_UINT scale_factor, OI_UINT bits) {
  float result = (1 << (scale_factor + 1)) * ((raw * 2.0f + 1.0f) / ((1 << bits) - 1.0f) - 1.0f);

  result /= SBC_DEQUANT_SCALING_FACTOR;

  /* Unless the encoder screwed up, all correct dequantized values should
   * satisfy this inequality. Non-compliant encoders which generate quantized
   * values with all 1-bits set can, theoretically, trigger this assert. This
   * is unlikely, however, and only an issue in debug mode.
   */
  OI_ASSERT(fabs(result) < 32768 * 1.6);

  return result;
}

#endif

INLINE int32_t OI_SBC_Dequant(uint32_t raw, OI_UINT scale_factor, OI_UINT bits) {
  uint32_t d;
  int32_t result;

  OI_ASSERT(scale_factor <= 15);
  OI_ASSERT(bits <= 16);

  if (bits <= 1) {
    return 0;
  }

  d = (raw * 2) + 1;
  d *= dequant_long_scaled[bits];
  result = d - SBC_DEQUANT_LONG_SCALED_OFFSET;

#ifdef DEBUG_DEQUANTIZATION
  {
    int32_t integerized_float_result;
    float float_result;

    float_result = dequant_float(raw, scale_factor, bits);
    integerized_float_result = (int32_t)floor(0.5f + float_result * (1 << 15));

    /* This detects overflow */
    OI_ASSERT(((result >= 0) && (integerized_float_result >= 0)) ||
              ((result <= 0) && (integerized_float_result <= 0)));
  }
#endif
  return result >> (15 - scale_factor);
}

/* This version of Dequant does not incorporate the scaling factor of 1.38. It
 * is intended for use with implementations of the filterbank which are
 * hard-coded into a DSP. Output is Q16.4 format, so that after joint stereo
 * processing (which leaves the most significant bit equal to the sign bit if
 * the encoder is conformant) the result will fit a 24 bit fixed point signed
 * value.*/

INLINE int32_t OI_SBC_Dequant_Unscaled(uint32_t raw, OI_UINT scale_factor, OI_UINT bits) {
  uint32_t d;
  int32_t result;

  OI_ASSERT(scale_factor <= 15);
  OI_ASSERT(bits <= 16);

  if (bits <= 1) {
    return 0;
  }
  if (bits == 16) {
    result = (raw << 16) + raw - 0x7fff7fff;
    return SCALE(result, 24 - scale_factor);
  }

  d = (raw * 2) + 1;
  d *= dequant_long_unscaled[bits];
  result = d - 0x80000000;

  return SCALE(result, 24 - scale_factor);
}

/**
@}
*/
