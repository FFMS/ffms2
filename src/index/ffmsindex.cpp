//  Copyright (c) 2008-2009 Karl Blomster
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

#include "ffms.h"
#include "ffmscompat.h"

extern "C" {
#include <libavutil/log.h>
}

#ifdef _WIN32
#include <Windows.h>
#endif

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace {

long long IndexMask = 0;
long long DumpMask = 0;
int Verbose = 0;
int IgnoreErrors = 0;
int Demuxer = FFMS_SOURCE_DEFAULT;
bool Overwrite = false;
bool PrintProgress = true;
bool WriteTC = false;
bool WriteKF = false;
const char *InputFile = nullptr;
std::string CacheFile;
std::string AudioFile;

struct Error {
	std::string msg;
	Error(const char *msg) : msg(msg) { }
	Error(const char *msg, FFMS_ErrorInfo const& e) : msg(msg) {
		this->msg.append(e.Buffer);
	}
};

void PrintUsage() {
	std::cout <<
		"FFmpegSource2 indexing app\n"
		"Usage: ffmsindex [options] inputfile [outputfile]\n"
		"If no output filename is specified, inputfile.ffindex will be used.\n"
		"\n"
		"Options:\n"
		"-f        Force overwriting of existing index file, if any (default: no)\n"
		"-v        Set FFmpeg verbosity level. Can be repeated for more verbosity. (default: no messages printed)\n"
		"-p        Disable progress reporting. (default: progress reporting on)\n"
		"-c        Write timecodes for all video tracks to outputfile_track00.tc.txt (default: no)\n"
		"-k        Write keyframes for all video tracks to outputfile_track00.kf.txt (default: no)\n"
		"-t N      Set the audio indexing mask to N (-1 means index all tracks, 0 means index none, default: 0)\n"
		"-d N      Set the audio decoding mask to N (mask syntax same as -t, default: 0)\n"
		"-a NAME   Set the audio output base filename to NAME (default: input filename)\n"
		"-s N      Set audio decoding error handling. See the documentation for details. (default: 0)\n"
		<< std::endl;
}

void ParseCMDLine(int argc, char *argv[]) {
	for (int i = 1; i < argc; ++i) {
		const char *Option = argv[i];
#define OPTION_ARG(flag) i + 1 < argc ? argv[i+1] : throw Error("Error: missing argument for -" flag)

		if (!strcmp(Option, "-f")) {
			Overwrite = true;
		} else if (!strcmp(Option, "-v")) {
			Verbose++;
		} else if (!strcmp(Option, "-p")) {
			PrintProgress = false;
		} else if (!strcmp(Option, "-c")) {
			WriteTC = true;
		} else if (!strcmp(Option, "-k")) {
			WriteKF = true;
		} else if (!strcmp(Option, "-t")) {
			IndexMask = atoll(OPTION_ARG("t"));
			i++;
		} else if (!strcmp(Option, "-d")) {
			DumpMask = atoll(OPTION_ARG("d"));
			i++;
		} else if (!strcmp(Option, "-a")) {
			AudioFile = OPTION_ARG("a");
			i++;
		} else if (!strcmp(Option, "-s")) {
			IgnoreErrors = atoi(OPTION_ARG("s"));
			i++;
		} else if (!InputFile) {
			InputFile = Option;
		} else if (CacheFile.empty()) {
			CacheFile = Option;
		} else {
			std::cout << "Warning: ignoring unknown option " << Option << std::endl;
		}
	}

	if (IgnoreErrors < 0 || IgnoreErrors > 3)
		throw Error("Error: invalid error handling mode");
	if (!InputFile)
		throw Error("Error: no input file specified");

	if (CacheFile.empty()) {
		CacheFile = InputFile;
		CacheFile.append(".ffindex");
	}
	AudioFile.append("%s.%02d.w64");
}

int FFMS_CC UpdateProgress(int64_t Current, int64_t Total, void *Private) {
	if (!PrintProgress)
		return 0;

	int Percentage = int((double(Current)/double(Total)) * 100);

	if (Private) {
		int *LastPercentage = (int *)Private;
		if (Percentage <= *LastPercentage)
			return 0;
		*LastPercentage = Percentage;
	}

	std::cout << "Indexing, please wait... " << Percentage << "% \r" << std::flush;

	return 0;
}

int FFMS_CC GenAudioFilename(const char *SourceFile, int Track, const FFMS_AudioProperties *, char *FileName, int FNSize, void *) {
	return snprintf(FileName, FileName ? FNSize : 0, AudioFile.c_str(), SourceFile, Track) + 1;
}

std::string DumpFilename(FFMS_Track *Track, int TrackNum, const char *Suffix) {
	if (FFMS_GetTrackType(Track) != FFMS_TYPE_VIDEO || !FFMS_GetNumFrames(Track))
		return "";

	char tn[3];
	snprintf(tn, 3, "%02d", TrackNum);
	return CacheFile + "_track" + tn + Suffix;
}

void DoIndexing() {
	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	int Progress = 0;

	FFMS_Index *Index = FFMS_ReadIndex(CacheFile.c_str(), &E);
	if (!Overwrite && Index) {
		FFMS_DestroyIndex(Index);
		throw Error("Error: index file already exists, use -f if you are sure you want to overwrite it.");
	}

	UpdateProgress(0, 100, nullptr);
	FFMS_Indexer *Indexer = FFMS_CreateIndexerWithDemuxer(InputFile, Demuxer, &E);
	if (Indexer == nullptr)
		throw Error("\nFailed to initialize indexing: ", E);

	FFMS_SetAudioNameCallback(Indexer, GenAudioFilename, nullptr);
	FFMS_SetProgressCallback(Indexer, UpdateProgress, &Progress);

	// Treat -1 as meaning track numbers above sizeof(long long) * 8 too, dumping implies indexing
	if (DumpMask == -1) {
		FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_AUDIO, 1, 1);
	} else if (IndexMask == -1) {
		FFMS_TrackTypeIndexSettings(Indexer, FFMS_TYPE_AUDIO, 1, 0);
	}

	// Apply attributes to remaining tracks (will set the attributes again on some tracks)
	for (int i = 0; i < sizeof(IndexMask) * 8; i++) {
		int Temp = (((IndexMask >> i) & 1) | ((DumpMask >> i) & 1));
		if (Temp)
			FFMS_TrackIndexSettings(Indexer, i, Temp, (DumpMask >> i) & 1);
	}

	Index = FFMS_DoIndexing2(Indexer, IgnoreErrors, &E);

	// The indexer is always freed
	Indexer = nullptr;

	if (Index == nullptr)
		throw Error("\nIndexing error: ", E);

	UpdateProgress(100, 100, nullptr);

	if (WriteTC) {
		if (PrintProgress)
			std::cout << "Writing timecodes... ";
		int NumTracks = FFMS_GetNumTracks(Index);
		for (int t = 0; t < NumTracks; t++) {
			FFMS_Track *Track = FFMS_GetTrackFromIndex(Index, t);
			std::string Filename = DumpFilename(Track, t, ".tc.txt");
			if (!Filename.empty()) {
				if (FFMS_WriteTimecodes(Track, Filename.c_str(), &E))
					std::cout << std::endl << "Failed to write timecodes file "
						<< Filename << ": " << E.Buffer << std::endl;
			}
		}
		if (PrintProgress)
			std::cout << "done." << std::endl;
	}

	if (WriteKF) {
		if (PrintProgress)
			std::cout << "Writing keyframes... ";
		int NumTracks = FFMS_GetNumTracks(Index);
		for (int t = 0; t < NumTracks; t++) {
			FFMS_Track *Track = FFMS_GetTrackFromIndex(Index, t);
			std::string Filename = DumpFilename(Track, t, ".kf.txt");
			if (!Filename.empty()) {
				std::ofstream kf(Filename.c_str());
				kf << "# keyframe format v1\n"
					"fps 0\n";

				int FrameCount = FFMS_GetNumFrames(Track);
				for (int CurFrameNum = 0; CurFrameNum < FrameCount; CurFrameNum++) {
					if (FFMS_GetFrameInfo(Track, CurFrameNum)->KeyFrame)
						kf << CurFrameNum << "\n";
				}
			}
		}
		if (PrintProgress)
			std::cout << "done.    " << std::endl;
	}

	if (PrintProgress)
		std::cout << "Writing index... ";

	int error = FFMS_WriteIndex(CacheFile.c_str(), Index, &E);
	FFMS_DestroyIndex(Index);
	if (error)
		throw Error("Error writing index: ", E);

	if (PrintProgress)
		std::cout << "done." << std::endl;
}

} // namespace {

#ifdef _WIN32
#ifdef __MINGW32__
// mingw doesn't support wmain
extern int _CRT_glob;
extern "C" void __wgetmainargs(int*, wchar_t***, wchar_t***, int, int*);
int main() {
	int argc;
	wchar_t **_argv;
	wchar_t **env;
	int si = 0;
	__wgetmainargs(&argc, &_argv, &env, _CRT_glob, &si);
#else
int wmain(int argc, wchar_t *_argv[]) {
#endif
	char **argv = (char**)malloc(argc*sizeof(char*));
	for (int i=0; i<argc; i++) {
		int len = WideCharToMultiByte(CP_UTF8, 0, _argv[i], -1, NULL, 0, NULL, NULL);
		if (!len) {
			std::cout << "Failed to translate commandline to Unicode" << std::endl;
			return 1;
		}
		char *temp = (char*)malloc(len*sizeof(char));
		len = WideCharToMultiByte(CP_UTF8, 0, _argv[i], -1, temp, len, NULL, NULL);
		if (!len) {
			std::cout << "Failed to translate commandline to Unicode" << std::endl;
			return 1;
		}
		argv[i] = temp;
	}
#else /* defined(_WIN32) && !defined(__MINGW32__) */
int main(int argc, char *argv[]) {
#endif /* defined(_WIN32) && !defined(__MINGW32__) */
	try {
		if (argc <= 1) {
			PrintUsage();
			return 0;
		}

		ParseCMDLine(argc, argv);
	}
	catch (Error const& e) {
		std::cout << e.msg << std::endl;
		return 1;
	}

	FFMS_Init(0, 1);

	switch (Verbose) {
		case 0: FFMS_SetLogLevel(AV_LOG_QUIET); break;
		case 1: FFMS_SetLogLevel(AV_LOG_WARNING); break;
		case 2: FFMS_SetLogLevel(AV_LOG_INFO); break;
		case 3:	FFMS_SetLogLevel(AV_LOG_VERBOSE); break;
		default: FFMS_SetLogLevel(AV_LOG_DEBUG); // if user used -v 4 or more times, he deserves the spam
	}

	try {
		DoIndexing();
	}
	catch (Error const& e) {
		std::cout << e.msg << std::endl;
		return 1;
	}

	return 0;
}
