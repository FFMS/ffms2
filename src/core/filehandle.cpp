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

#include "filehandle.h"

#include "utils.h"

#include <cstdarg>

#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#	ifdef __MINGW32__
#		define fseeko fseeko64
#		define ftello ftello64
#	else
#		define fseeko _fseeki64
#		define ftello _ftelli64
#	endif
#endif

#ifdef _WIN32
static FILE *ffms_fopen(const char *filename, const char *mode) {
	std::wstring filename_wide = widen_path(filename);
	if (filename_wide.empty())
		return fopen(filename, mode);

	std::wstring mode_wide = widen_path(mode);
	if (mode_wide.empty())
		return fopen(filename, mode);

	return _wfopen(filename_wide.c_str(), mode_wide.c_str());
}
#else
#define ffms_fopen fopen
#endif // _WIN32

FileHandle::FileHandle(const char *filename, const char *mode, int error_source, int error_cause)
: f(ffms_fopen(filename, mode))
, filename(filename)
, error_source(error_source)
, error_cause(error_cause)
{
	if (!f)
		throw FFMS_Exception(error_source, FFMS_ERROR_NO_FILE,
			"Failed to open '" + this->filename + "'");
}

void FileHandle::Seek(int64_t offset, int origin) {
	int ret = fseeko(f, offset, origin);
	if (ret < 0 || ferror(f))
		throw FFMS_Exception(error_source, error_cause,
			"Failed to seek in '" + filename + "'");
}

int64_t FileHandle::Tell() {
	int64_t ret = ftello(f);
	if (ret < 0 || ferror(f))
		throw FFMS_Exception(error_source, error_cause,
			"Failed to read position in '" + filename + "'");
	return ret;
}

size_t FileHandle::Read(char *buffer, size_t size) {
	size_t count = fread(buffer, 1, size, f);
	if (ferror(f) && !feof(f))
		throw FFMS_Exception(error_source, FFMS_ERROR_FILE_READ,
			"Failed to read from '" + filename + "'");
	return count;
}

size_t FileHandle::Write(const char *buffer, size_t size) {
	size_t count = fwrite(buffer, 1, size, f);
	if (ferror(f))
		throw FFMS_Exception(error_source, FFMS_ERROR_FILE_WRITE,
			"Failed to write to '" + filename + "'");
	return count;
}

int FileHandle::Printf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int ret = vfprintf(f, fmt, args);
	va_end(args);
	return ret;
}