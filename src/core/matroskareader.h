//  Copyright (c) 2014 Thomas Goyne <tgoyne@gmail.com>
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#ifndef MATROSKAREADER_H
#define MATROSKAREADER_H

#include "matroskaparser.h"

#include <memory>
#include <stdint.h>
#include <string>

class FileMapping;
class TrackCompressionContext;

class MatroskaReader : public InputStream {
	std::auto_ptr<FileMapping> file;
	std::string error;

	static int ISRead(MatroskaReader *st, ulonglong pos, void *buffer, int count);
	static longlong Scan(MatroskaReader *st, ulonglong start, unsigned signature);
	static const char *GetLastError(MatroskaReader *st);
	static longlong GetFileSize(MatroskaReader *st);

public:
	MatroskaReader(const char *path);
	~MatroskaReader();

	ulonglong Size() const;
	const void *Read(ulonglong pos, size_t length);
};

class MatroskaReaderContext {
	size_t BufferSize;

	void Append(const void *Data, size_t Length);

public:
	MatroskaReader Reader;
	uint8_t *Buffer;
	size_t FrameSize;

	MatroskaReaderContext(const char *filename);
	~MatroskaReaderContext();

	void ReadFrame(uint64_t FilePos, size_t InputFrameSize, TrackCompressionContext *TCC);
};
#endif
