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

#include "zipfile.h"

#include "utils.h"

ZipFile::ZipFile(const char *filename, const char *mode)
: file(filename, mode, FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ)
, state(Initial)
{
	buffer.resize(65536);

	memset(&z, 0, sizeof(z));
}

ZipFile::~ZipFile() {
	if (state == Inflate)
		inflateEnd(&z);
	if (state == Deflate)
		deflateEnd(&z);
}

void ZipFile::Read(void *data, size_t size) {
	if (state == Deflate) {
		deflateEnd(&z);
		state = Initial;
	}
	if (state != Inflate) {
		if (inflateInit(&z) != Z_OK)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to initialize zlib");
		state = Inflate;
	}

	z.next_out = static_cast<unsigned char *>(data);
	z.avail_out = size;
	do {
		if (!z.avail_in) {
			z.next_in = reinterpret_cast<Bytef*>(&buffer[0]);
			z.avail_in = file.Read(&buffer[0], buffer.size());
		}
		if (!z.avail_in && !file.Tell())
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: File is empty");

		switch (inflate(&z, Z_SYNC_FLUSH)) {
		case Z_NEED_DICT:
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Dictionary error.");
		case Z_DATA_ERROR:
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Data error.");
		case Z_MEM_ERROR:
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Memory error.");
		case Z_STREAM_END:
			if (z.avail_out > 0)
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to read data: Stream ended early");
		}
	} while (z.avail_out);
}

int ZipFile::Write(const void *data, size_t size) {
	if (state == Inflate) {
		inflateEnd(&z);
		state = Initial;
	}
	if (state != Deflate) {
		if (deflateInit(&z, 5) != Z_OK)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, "Failed to initialize zlib");
		state = Deflate;
	}

	z.next_in = static_cast<unsigned char *>(const_cast<void *>(data));
	z.avail_in = size;
	int ret = 0;
	do {
		z.avail_out = buffer.size();
		z.next_out = reinterpret_cast<Bytef*>(&buffer[0]);
		ret = deflate(&z, size > 0 ? Z_NO_FLUSH : Z_FINISH);
		uInt written = buffer.size() - z.avail_out;
		if (written)
			file.Write(&buffer[0], written);
	} while (z.avail_out == 0);
	return ret;
}

void ZipFile::Finish() {
	while (Write(nullptr, 0) != Z_STREAM_END) ;
	deflateEnd(&z);
	state = Initial;
}
