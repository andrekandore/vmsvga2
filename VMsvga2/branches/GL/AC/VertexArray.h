/*
 *  VertexArray.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on January 24th 2011.
 *  Copyright 2011 Zenith432. All rights reserved.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef __VERTEXARRAY_H__
#define __VERTEXARRAY_H__

class VertexArray
{
	uint8_t* kernel_ptr;
	size_t size_bytes;
	size_t offset_in_gmr;
	size_t next_avail;
	uint32_t sid;
	uint32_t gmr_id;
	uint32_t fence;

public:
	void init(void);
	void purge(class VMsvga2Accel* provider);
	IOReturn alloc(class VMsvga2Accel* provider, size_t num_bytes, uint8_t** ptr);
	IOReturn upload(class VMsvga2Accel* provider, uint8_t const* ptr, size_t num_bytes, uint32_t* sid);
};

#endif /* __VERTEXARRAY_H__ */
