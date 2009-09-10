/*
 *  svga_apple_header.h
 *  VMsvga2
 *
 *  Created by Zenith432 on August 10th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
 *
 *  Note: this header file should not have #include
 *    guards so it can be #included multiple times.
 *
 */

#ifdef __APPLE__
#define PACKED
#endif /* __APPLE__*/

#define uint32 UInt32
#define int32 UInt32
#define uint16 UInt16
#define uint8 UInt8
#define Bool bool
#define INLINE inline
