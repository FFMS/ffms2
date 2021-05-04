//  Copyright (c) 2007-2011 Fredrik Mellbin
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

#ifndef UTILS_H
#define UTILS_H

#include "ffms.h"

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// must be included after ffmpeg headers
#include "ffmscompat.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

class FFMS_Exception {
    std::string _Message;
    int _ErrorType;
    int _SubType;

public:
    FFMS_Exception(int ErrorType, int SubType, const char *Message = "");
    FFMS_Exception(int ErrorType, int SubType, const std::string &Message);
    const std::string &GetErrorMessage() const { return _Message; }
    int CopyOut(FFMS_ErrorInfo *ErrorInfo) const;
};

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

void ClearErrorInfo(FFMS_ErrorInfo *ErrorInfo);
void FillAP(FFMS_AudioProperties &AP, AVCodecContext *CTX, FFMS_Track &Frames);

void LAVFOpenFile(const char *SourceFile, AVFormatContext *&FormatContext, int Track);

namespace optdetail {
    template<typename T>
    T get_av_opt(void *v, const char *name) {
        int64_t value = 0;
        av_opt_get_int(v, name, 0, &value);
        return static_cast<T>(value);
    }

    template<>
    inline double get_av_opt<double>(void *v, const char *name) {
        double value = 0.0;
        av_opt_get_double(v, name, 0, &value);
        return value;
    }

    template<typename T>
    void set_av_opt(void *v, const char *name, T value) {
        av_opt_set_int(v, name, value, 0);
    }

    template<>
    inline void set_av_opt<double>(void *v, const char *name, double value) {
        av_opt_set_double(v, name, value, 0);
    }
}

template<typename FFMS_Struct>
class OptionMapper {
    struct OptionMapperBase {
        virtual void ToOpt(FFMS_Struct const& src, void *dst) const = 0;
        virtual void FromOpt(FFMS_Struct &dst, void *src) const = 0;
        virtual ~OptionMapperBase() = default;
    };

    template<typename T>
    class OptionMapperImpl : public OptionMapperBase {
        T(FFMS_Struct::*ptr);
        const char *name;

    public:
        OptionMapperImpl(T(FFMS_Struct::*ptr), const char *name) : ptr(ptr), name(name) {}
        void ToOpt(FFMS_Struct const& src, void *dst) const { optdetail::set_av_opt(dst, name, src.*ptr); }
        void FromOpt(FFMS_Struct &dst, void *src) const { dst.*ptr = optdetail::get_av_opt<T>(src, name); }
    };

    std::unique_ptr<OptionMapperBase> impl;

public:
    template<typename T>
    OptionMapper(const char *opt_name, T(FFMS_Struct::*member)) : impl(new OptionMapperImpl<T>(member, opt_name)) {}

    void ToOpt(FFMS_Struct const& src, void *dst) const { impl->ToOpt(src, dst); }
    void FromOpt(FFMS_Struct &dst, void *src) const { impl->FromOpt(dst, src); }
};

template<typename T, int N>
std::unique_ptr<T> ReadOptions(void *opt, OptionMapper<T>(&options)[N]) {
    auto ret = make_unique<T>();
    for (int i = 0; i < N; ++i)
        options[i].FromOpt(*ret, opt);
    return ret;
}

template<typename T, int N>
void SetOptions(T const& src, void *opt, OptionMapper<T>(&options)[N]) {
    for (int i = 0; i < N; ++i)
        options[i].ToOpt(src, opt);
}

int ResizerNameToSWSResizer(const char *ResizerName);
bool IsSamePath(const char *p1, const char *p2);

#endif
