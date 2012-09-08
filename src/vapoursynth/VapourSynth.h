#ifndef VAPOURSYNTH_H
#define VAPOURSYNTH_H

#include <stdint.h>

#define VAPOURSYNTH_API_VERSION 1

typedef struct VSFrameRef VSFrameRef;
typedef struct VSNodeRef VSNodeRef;
typedef struct VSCore VSCore;
typedef struct VSPlugin VSPlugin;
typedef struct VSNode VSNode;
typedef struct VSMap VSMap;
typedef struct VSAPI VSAPI;
typedef struct VSFrameContext VSFrameContext;

enum VSColorFamily {
	// all planar formats
    cmGray		= 1000000,
    cmRgb		= 2000000,
    cmYuv		= 3000000,
    cmYCoCg		= 4000000,
	// special for compatibility, if you implement these in new filters I'll personally kill you
	cmCompat	= 9000000
};

enum VSSampleType {
	stInteger = 0,
	stFloat = 1
};

// The +10 is so people won't be using the constants interchangably "by accident"
enum VSPresetFormat {
    pfNone = 0,

    pfGray8 = cmGray + 10,
    pfGray16,

    pfYUV420P8 = cmYuv + 10,
    pfYUV422P8,
    pfYUV444P8,
    pfYUV410P8,
    pfYUV411P8,
    pfYUV440P8,

    pfYUV420P9,
    pfYUV422P9,
    pfYUV444P9,

    pfYUV420P10,
    pfYUV422P10,
    pfYUV444P10,

    pfYUV420P16,
    pfYUV422P16,
    pfYUV444P16,

    pfRGB24 = cmRgb + 10,
    pfRGB27,
    pfRGB30,
    pfRGB48,

	// special for compatibility, if you implement these in any filter I'll personally kill you
	// I'll also change their ids around to break your stuff regularly
	pfCompatBgr32 = cmCompat + 10,
	pfCompayYuy2
};

enum VSFilterMode {
    fmParallel = 100, // completely parallel execution
    fmParallelRequests = 200, // for filters that are serial in nature but can request one or more frames they need in advance
    fmUnordered = 300, // for filters that modify their internal state every request
    fmSerial = 400 // for source filters and compatibility with other filtering architectures
};

// possibly add d3d storage on gpu, since the gpu memory can be mapped into normal address space
typedef struct VSFormat {
    char name[32];
    int id;
    int colorFamily; // gray/rgb/yuv/ycocg
    int sampleType; // int/float
    int bitsPerSample; // number of significant bits
    int bytesPerSample; // actual storage is always in a power of 2 and the smallest possible that can fit the number of bits used per sample

    int subSamplingW; // log2 subsampling factor, applied to second and third plane
    int subSamplingH;

    int numPlanes; // implicit from colorFamily, 1 or 3 in the currently specified ones
} VSFormat;

enum NodeFlags {
    nfNoCache		= 1,
	nfCompatPacked	= 2
};

enum GetPropErrors {
    peUnset	= 1,
    peType	= 2,
    peIndex	= 4
};

typedef struct VSVideoInfo {
	// add node name?
    const VSFormat *format;
    int64_t fpsNum;
    int64_t fpsDen;
    int width;
    int height;
    int numFrames;
    int flags; // expose in some other way?
} VSVideoInfo;

enum ActivationReason {
    arInitial = 0,
    arFrameReady = 1,
    arAllFramesReady = 2,
    arError = -1
};

// function typedefs
typedef	VSCore *(__stdcall *VSCreateVSCore)(int threads);
typedef	void (__stdcall *VSFreeVSCore)(VSCore *core);

// function/filter typedefs
typedef void (__stdcall *VSPublicFunction)(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi);
typedef void (__stdcall *VSFilterInit)(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi);
typedef const VSFrameRef *(__stdcall *VSFilterGetFrame)(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
typedef void (__stdcall *VSFilterFree)(void *instanceData, VSCore *core, const VSAPI *vsapi);
typedef void (__stdcall *VSRegisterFunction)(const char *name, const char *args, VSPublicFunction argsFunc, void *functionData, VSPlugin *plugin);
typedef const VSNodeRef *(__stdcall *VSCreateFilter)(const VSMap *in, VSMap *out, const char *name, VSFilterInit init, VSFilterGetFrame getFrame, VSFilterFree free, int filterMode, int flags, void *instanceData, VSCore *core);
typedef VSMap *(__stdcall *VSInvoke)(VSPlugin *plugin, const char *name, const VSMap *args);
typedef void (__stdcall *VSSetError)(VSMap *map, const char *errorMessage);
typedef const char *(__stdcall *VSGetError)(const VSMap *map);
typedef void (__stdcall *VSSetFilterError)(const char *errorMessage, VSFrameContext *frameCtx);

typedef const VSFormat *(__stdcall *VSGetFormatPreset)(int id, VSCore *core);
typedef const VSFormat *(__stdcall *VSRegisterFormat)(int colorFamily, int sampleType, int bitsPerSample, int subSamplingW, int subSamplingH, VSCore *core);

// frame and clip handling
typedef void (__stdcall *VSFrameDoneCallback)(void *userData, const VSFrameRef *f, int n, const VSNodeRef *, const char *errorMsg);
typedef void (__stdcall *VSGetFrameAsync)(int n, const VSNodeRef *node, VSFrameDoneCallback callback, void *userData);
typedef const VSFrameRef *(__stdcall *VSGetFrame)(int n, const VSNodeRef *node, char *errorMsg, int bufSize);
typedef void (__stdcall *VSRequestFrameFilter)(int n, const VSNodeRef *node, VSFrameContext *frameCtx);
typedef const VSFrameRef * (__stdcall *VSGetFrameFilter)(int n, const VSNodeRef *node, VSFrameContext *frameCtx);
typedef const VSFrameRef *(__stdcall *VSCloneFrameRef)(const VSFrameRef *f);
typedef const VSNodeRef *(__stdcall *VSCloneNodeRef)(const VSNodeRef *node);
typedef void (__stdcall *VSFreeFrame)(const VSFrameRef *f);
typedef void (__stdcall *VSFreeNode)(const VSNodeRef *node);
typedef VSFrameRef *(__stdcall *VSNewVideoFrame)(const VSFormat *format, int width, int height, const VSFrameRef *propSrc, VSCore *core);
typedef VSFrameRef *(__stdcall *VSCopyFrame)(const VSFrameRef *f, VSCore *core);
typedef void (__stdcall *VSCopyFrameProps)(const VSFrameRef *src, VSFrameRef *dst, VSCore *core);

typedef int (__stdcall *VSGetStride)(const VSFrameRef *f, int plane);
typedef const uint8_t *(__stdcall *VSGetReadPtr)(const VSFrameRef *f, int plane);
typedef uint8_t *(__stdcall *VSGetWritePtr)(VSFrameRef *f, int plane);

// property access
typedef const VSVideoInfo *(__stdcall *VSGetVideoInfo)(const VSNodeRef *node);
typedef void (__stdcall *VSSetVideoInfo)(const VSVideoInfo *vi, VSNode *node);
typedef const VSFormat *(__stdcall *VSGetFrameFormat)(const VSFrameRef *f);
typedef int (__stdcall *VSGetFrameWidth)(const VSFrameRef *f, int plane);
typedef int (__stdcall *VSGetFrameHeight)(const VSFrameRef *f, int plane);
typedef const VSMap *(__stdcall *VSGetFramePropsRO)(const VSFrameRef *f);
typedef VSMap *(__stdcall *VSGetFramePropsRW)(VSFrameRef *f);
typedef int (__stdcall *VSPropNumKeys)(const VSMap *map);
typedef const char *(__stdcall *VSPropGetKey)(const VSMap *map, int index);
typedef int (__stdcall *VSPropNumElements)(const VSMap *map, const char *key);
typedef char (__stdcall *VSPropGetType)(const VSMap *map, const char *key);

typedef VSMap *(__stdcall *VSNewMap)();
typedef void (__stdcall *VSFreeMap)(VSMap *map);
typedef void (__stdcall *VSClearMap)(VSMap *map);

typedef int64_t (__stdcall *VSPropGetInt)(const VSMap *map, const char *key, int index, int *error);
typedef double (__stdcall *VSPropGetFloat)(const VSMap *map, const char *key, int index, int *error);
typedef const char *(__stdcall *VSPropGetData)(const VSMap *map, const char *key, int index, int *error);
typedef int (__stdcall *VSPropGetDataSize)(const VSMap *map, const char *key, int index, int *error);
typedef const VSNodeRef *(__stdcall *VSPropGetNode)(const VSMap *map, const char *key, int index, int *error);
typedef const VSFrameRef *(__stdcall *VSPropGetFrame)(const VSMap *map, const char *key, int index, int *error);

typedef int (__stdcall *VSPropDeleteKey)(VSMap *map, const char *key);
typedef int (__stdcall *VSPropSetInt)(VSMap *map, const char *key, int64_t i, int append);
typedef int (__stdcall *VSPropSetFloat)(VSMap *map, const char *key, double d, int append);
typedef int (__stdcall *VSPropSetData)(VSMap *map, const char *key, const char *data, int size, int append);
typedef int (__stdcall *VSPropSetNode)(VSMap *map, const char *key, const VSNodeRef *node, int append);
typedef int (__stdcall *VSPropSetFrame)(VSMap *map, const char *key, const VSFrameRef *f, int append);

typedef void (__stdcall *VSConfigPlugin)(const char *identifier, const char *defaultNamespace, const char *name, int apiVersion, int readonly, VSPlugin *plugin);
typedef void (__stdcall *VSInitPlugin)(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin);

typedef VSPlugin *(__stdcall *VSGetPluginId)(const char *identifier, VSCore *core);
typedef VSPlugin *(__stdcall *VSGetPluginNs)(const char *ns, VSCore *core);

typedef VSMap *(__stdcall *VSGetPlugins)(VSCore *core);
typedef VSMap *(__stdcall *VSGetFunctions)(VSPlugin *plugin);

//void filterFreeFrameEarly(const VSFrameRef *f);
// fixme, add a special function to unref frames early for filters that need to examine
// a large number of frames to generate one output frame (25+)
// also warn against using it

//typedef void (__stdcall *VSFreeWithFilter)(const VSNodeRef *node, VSNode *filter);
// make cleanup easier by freeing all stored clips on filter exit

// fixme, keep this? expand?
// VSCopyFrame equivalent to VSCopyFrame2({f, f, f}, f.vi.format, 0, core); VSCopyFrameProps()
//typedef VSFrameRef *(__stdcall *VSCopyFrame2)(const VSFrameRef *f[], const VSFormat *format, int clobber, VSCore *core);

struct VSAPI {
	VSCreateVSCore createVSCore;
	VSFreeVSCore freeVSCore;
    VSCloneFrameRef cloneFrameRef;
    VSCloneNodeRef cloneNodeRef;
    VSFreeFrame freeFrame;
    VSFreeNode freeNode;
    VSNewVideoFrame newVideoFrame;
    VSCopyFrame copyFrame;
    VSCopyFrameProps copyFrameProps;
    VSRegisterFunction registerFunction;
    VSGetPluginId getPluginId;
    VSGetPluginNs getPluginNs;
	VSGetPlugins getPlugins;
    VSGetFunctions getFunctions;
    VSCreateFilter createFilter;
    VSSetError setError;
	VSGetError getError;
    VSSetFilterError setFilterError; //use to signal errors in the filter getframe function
    VSInvoke invoke;
    VSGetFormatPreset getFormatPreset;
    VSRegisterFormat registerFormat;
    VSGetFrame getFrame; // for external applications using the core as a library/for exceptional use if frame requests are necessary during filter initialization
    VSGetFrameAsync getFrameAsync; // for external applications using the core as a library
    VSGetFrameFilter getFrameFilter; // only use inside a filter's getframe function
    VSRequestFrameFilter requestFrameFilter; // only use inside a filter's getframe function
    VSGetStride getStride;
    VSGetReadPtr getReadPtr;
    VSGetWritePtr getWritePtr;

    //property access functions
    VSNewMap newMap;
    VSFreeMap freeMap;
    VSClearMap clearMap;

    VSGetVideoInfo getVideoInfo;
    VSSetVideoInfo setVideoInfo;
    VSGetFrameFormat getFrameFormat;
    VSGetFrameWidth getFrameWidth;
    VSGetFrameHeight getFrameHeight;
    VSGetFramePropsRO getFramePropsRO;
    VSGetFramePropsRW getFramePropsRW;

    VSPropNumKeys propNumKeys;
    VSPropGetKey propGetKey;
    VSPropNumElements propNumElements;
    VSPropGetType propGetType;
    VSPropGetInt propGetInt;
    VSPropGetFloat propGetFloat;
    VSPropGetData propGetData;
    VSPropGetDataSize propGetDataSize;
    VSPropGetNode propGetNode;
    VSPropGetFrame propGetFrame;

    VSPropDeleteKey propDeleteKey;
    VSPropSetInt propSetInt;
    VSPropSetFloat propSetFloat;
    VSPropSetData propSetData;
    VSPropSetNode propSetNode;
    VSPropSetFrame propSetFrame;
};
#if defined(VSCORE_EXPORTS) && (__cplusplus)
extern "C" __declspec(dllexport) const VSAPI *__stdcall getVapourSynthAPI(int version);
#else
#ifdef __cplusplus
extern "C" const VSAPI *__stdcall getVapourSynthAPI(int version);
#else
const VSAPI *__stdcall getVapourSynthAPI(int version);
#endif
#endif

#endif // VAPOURSYNTH_H
