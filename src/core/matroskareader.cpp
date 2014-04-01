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

#include "matroskareader.h"

#include "utils.h"

#include <algorithm>
#include <cassert>

#ifdef _WIN32
#define WIN32_EXTRA_LEAN
#include <windows.h>

namespace {
class HandleCloser {
	HANDLE h;
public:
	HandleCloser(HANDLE h = INVALID_HANDLE_VALUE) : h(h) { }
	~HandleCloser() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
	operator HANDLE() const { return h; }
	HandleCloser& operator=(HANDLE new_h) {
		this->~HandleCloser();
		h = new_h;
		return *this;
	}
};

std::string errmsg() {
	LPWSTR lpstr = NULL;

	if(FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, reinterpret_cast<LPWSTR>(&lpstr), 0, NULL) == NULL)
		return "Unknown Error";

	int len = WideCharToMultiByte(CP_UTF8, 0, lpstr, -1, NULL, 0, NULL, NULL);
	if (len == 0) {
		LocalFree(lpstr);
		return "Unknown Error";
	}

	std::string ret(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, lpstr, -1, &ret[0], len, NULL, NULL);
	LocalFree(lpstr);
	return ret;
}
}

class FileMapping {
	HandleCloser file_mapping;
	longlong file_size;

	ulonglong mapping_start;
	ulonglong mapping_length;
	const uint8_t *buffer;

public:
	FileMapping(const char *path)
	: mapping_start(0)
	, mapping_length(0)
	, buffer(NULL)
	{
		HandleCloser file = CreateFileW(widen_path(path).c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
		if (file == INVALID_HANDLE_VALUE)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Can't open '") + path + "': " + errmsg());

		LARGE_INTEGER li;
		if (!GetFileSizeEx(file, &li))
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Can't get size of file '") + path + "': " + errmsg());
		file_size = li.QuadPart;

		file_mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
		if (file_mapping == INVALID_HANDLE_VALUE)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Can't create mapping for '") + path + "': " + errmsg());
	}

	~FileMapping() {
		if (buffer)
			UnmapViewOfFile(buffer);
	}

	void Map(ulonglong start, size_t length) {
		if (buffer)
			UnmapViewOfFile(buffer);

		LARGE_INTEGER li;
		li.QuadPart = mapping_start;
		buffer = (uint8_t *)MapViewOfFile(file_mapping, FILE_MAP_READ, li.HighPart, li.LowPart, static_cast<size_t>(length));
		if (!buffer)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
				"MapViewOfFile failed: " + errmsg());
		mapping_length = length;
	}

	ulonglong Size() const { return file_size; }
	const uint8_t *Read(ulonglong start, ulonglong length);
};
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
class FileDescriptor {
	int fd;
public:
	FileDescriptor(const char *path)
	: fd(open(path, O_RDONLY))
	{
		if (fd < 0)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
			std::string("Can't open '") + path + "': " + strerror(errno));
	}

	~FileDescriptor() { close(fd); }
	operator int() const { return fd; }
};
}

class FileMapping {
	FileDescriptor fd;
	longlong file_size;

	ulonglong mapping_start;
	ulonglong mapping_length;
	const uint8_t *buffer;

public:
	FileMapping(const char *path)
	: fd(path)
	, mapping_start(0)
	, mapping_length(0)
	, buffer(NULL)
	{
		struct stat st;
		if (fstat(fd, &st) < 0)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				std::string("Can't get size of file '") + path + "': " + strerror(errno));
		file_size = st.st_size;
	}

	~FileMapping() {
		if (buffer && buffer != MAP_FAILED)
			munmap(const_cast<uint8_t *>(buffer), mapping_length);
	}

	void Map(ulonglong start, size_t length) {
		if (buffer && buffer != MAP_FAILED) {
			munmap(const_cast<uint8_t *>(buffer), mapping_length);
			buffer = NULL;
		}

		buffer = static_cast<uint8_t*>(mmap(NULL, static_cast<size_t>(length), PROT_READ, MAP_PRIVATE, fd, start));
		if (buffer == MAP_FAILED)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED,
				std::string("mmap failed: ") + strerror(errno));
		mapping_length = length;
	}

	ulonglong Size() const { return file_size; }
	const uint8_t *Read(ulonglong start, ulonglong length);
};
#endif

const uint8_t *FileMapping::Read(ulonglong start, ulonglong length) {
	assert(start + length <= static_cast<ulonglong>(file_size));

	// Check if we can just use the current mapping
	if (buffer && start >= mapping_start && start + length <= mapping_start + mapping_length)
		return buffer + start - mapping_start;

	if (sizeof(size_t) == 4) {
		mapping_start = start & ~0xFFFFFULL; // Align to 1 MB boundary
		length += static_cast<size_t>(start - mapping_start);
		// Map 16 MB or length rounded up to the next MB
		length = std::min<uint64_t>(std::max<uint64_t>(0x1000000U, (length + 0xFFFFF) & ~0xFFFFF), file_size - mapping_start);
	}
	else {
		// Just map the whole file
		mapping_start = 0;
		length = file_size;
	}

	Map(mapping_start, static_cast<size_t>(length));
	return buffer + start - mapping_start;
}

namespace {
unsigned GetCacheSize(InputStream *) { return 16 * 1024 * 1024; }
void *Malloc(InputStream *, size_t size) { return malloc(size); }
void *Realloc(InputStream *, void *mem, size_t size) { return realloc(mem, size); }
void Free(InputStream *, void *mem) { free(mem); }
int Progress(InputStream *, ulonglong, ulonglong) { return 1; }
}

MatroskaReader::MatroskaReader(const char *path)
: file(new FileMapping(path))
{
	read = (int (*)(InputStream *, ulonglong, void *, int))ISRead;
	scan = (longlong (*)(InputStream *, ulonglong, unsigned))Scan;
	getcachesize = GetCacheSize;
	geterror = (const char *(*)(InputStream *))GetLastError;
	memalloc = Malloc;
	memrealloc = Realloc;
	memfree = Free;
	progress = Progress;
	getfilesize = (longlong (*)(InputStream *))GetFileSize;
}

MatroskaReader::~MatroskaReader() { }

const void *MatroskaReader::Read(ulonglong pos, size_t count) {
	return file->Read(pos, count);
}

int MatroskaReader::ISRead(MatroskaReader *self, ulonglong pos, void *buffer, int count) {
	if (pos == self->file->Size())
		return 0;

	ulonglong remaining = self->file->Size() - pos;
	if (remaining < INT_MAX)
		count = (std::min)(static_cast<int>(remaining), count);

	try {
		memcpy(buffer, self->Read(pos, count), count);
	}
	catch (FFMS_Exception const& e) {
		self->error = e.GetErrorMessage();
		return -1;
	}

	return count;
}

longlong MatroskaReader::Scan(MatroskaReader *self, ulonglong start, unsigned signature) {
	unsigned cmp = 0;
	for (ulonglong i = start; i < self->file->Size(); ++i) {
		int c = *self->file->Read(i, 1);
		cmp = ((cmp << 8) | c) & 0xffffffff;
		if (cmp == signature)
			return static_cast<longlong>(i) - 4;
	}

	return -1;
}

const char *MatroskaReader::GetLastError(MatroskaReader *self) {
	return self->error.c_str();
}

longlong MatroskaReader::GetFileSize(MatroskaReader *self) {
	return self->file->Size();
}

ulonglong MatroskaReader::Size() const {
	return file->Size();
}

MatroskaReaderContext::MatroskaReaderContext(const char *filename)
: BufferSize(0)
, Reader(filename)
, Buffer(NULL)
, FrameSize(0)
{
}

MatroskaReaderContext::~MatroskaReaderContext() {
	if (Buffer)
		av_free(Buffer);
}

template<class T>
static void safe_aligned_reallocz(T *&ptr, size_t old_size, size_t new_size) {
	void *newalloc = av_mallocz(new_size);
	if (newalloc)
		memcpy(newalloc, ptr, FFMIN(old_size, new_size));
	av_free(ptr);
	ptr = static_cast<T*>(newalloc);
	if (!ptr)
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_ALLOCATION_FAILED, "Out of memory");
}

void MatroskaReaderContext::Append(const void *Data, size_t Length) {
	if (BufferSize < Length + FrameSize + FF_INPUT_BUFFER_PADDING_SIZE) {
		size_t NewSize = (FrameSize + Length) * 2;
		safe_aligned_reallocz(Buffer, BufferSize, NewSize);
		BufferSize = NewSize;
	}

	memcpy(Buffer + FrameSize, Data, Length);
	FrameSize += Length;
}

void MatroskaReaderContext::ReadFrame(uint64_t FilePos, size_t InputFrameSize, TrackCompressionContext *TCC) {
	FrameSize = 0;
	if (TCC && TCC->CompressionMethod == COMP_ZLIB) {
		CompressedStream *CS = TCC->CS;
		cs_NextFrame(CS, FilePos, FrameSize);

		char CSBuffer[4096];
		for (;;) {
			int ReadBytes = cs_ReadData(CS, CSBuffer, sizeof(CSBuffer));
			if (ReadBytes == 0) break;
			if (ReadBytes < 0) {
				std::ostringstream buf;
				buf << "Error decompressing data: " << cs_GetLastError(CS);
				throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ, buf.str());
			}

			Append(CSBuffer, ReadBytes);
		}
	}
	else {
		if (TCC && TCC->CompressionMethod == COMP_PREPEND)
			Append(TCC->CompressedPrivateData, TCC->CompressedPrivateDataSize);

		if (Reader.Size() < FilePos + InputFrameSize)
			throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_FILE_READ,
				"Truncated input file");

		Append(Reader.Read(FilePos, InputFrameSize), InputFrameSize);
	}

	if (FrameSize)
		memset(Buffer + FrameSize, 0, FF_INPUT_BUFFER_PADDING_SIZE);
}
