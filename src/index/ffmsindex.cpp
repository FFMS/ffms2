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

extern "C" {
#include <libavutil/log.h>
}

#ifdef _WIN32
#include <objbase.h>
#endif

#include <iostream>
#include <string>
#include <stdlib.h>
#include "ffms.h"
#include "ffmscompat.h"

int TrackMask;
int DumpMask;
int Verbose;
int IgnoreErrors;
int Demuxer;
bool Overwrite;
bool PrintProgress;
bool WriteTC;
std::string InputFile;
std::string CacheFile;
std::string AudioFile;

FFMS_Index *Index;


static void PrintUsage () {
	using namespace std;
	cout << "FFmpegSource2 indexing app" << endl
	     << "Usage: ffmsindex [options] inputfile [outputfile]" << endl
	     << "If no output filename is specified, inputfile.ffindex will be used." << endl << endl
	     << "Options:" << endl
	     << "-f        Force overwriting of existing index file, if any (default: no)" << endl
	     << "-v        Set FFmpeg verbosity level. Can be repeated for more verbosity. (default: no messages printed)" << endl
	     << "-p        Disable progress reporting. (default: progress reporting on)" << endl
	     << "-c        Write timecodes for all video tracks to outputfile_track00.tc.txt (default: no)" << endl
	     << "-t N      Set the audio indexing mask to N (-1 means index all tracks, 0 means index none, default: 0)" << endl
	     << "-d N      Set the audio decoding mask to N (mask syntax same as -t, default: 0)" << endl
	     << "-a NAME   Set the audio output base filename to NAME (default: input filename)" << endl
	     << "-s N      Set audio decoding error handling. See the documentation for details. (default: 0)" << endl
		 << "-m NAME   Force the use of demuxer NAME (default, lavf, matroska, haalimpeg, haaliogg)" << endl;
}


static void ParseCMDLine (int argc, char *argv[]) {
	if (argc <= 1) {
		PrintUsage();
		throw "";
	}

	// defaults
	InputFile = "";
	CacheFile = "";
	AudioFile = "";
	TrackMask = 0;
	DumpMask  = 0;
	Verbose = 0;
	Demuxer = FFMS_SOURCE_DEFAULT;
	Overwrite = false;
	IgnoreErrors = false;
	PrintProgress = true;

	// argv[0] = name of program
	int i = 1;

	while (i < argc) {
		std::string Option = argv[i];
		std::string OptionArg = "";
		if (i+1 < argc)
			OptionArg = argv[i+1];

		if (!Option.compare("-f")) {
			Overwrite = true;
		} else if (!Option.compare("-v")) {
			Verbose++;
		} else if (!Option.compare("-p")) {
			PrintProgress = false;
		} else if (!Option.compare("-c")) {
			WriteTC = true;
		} else if (!Option.compare("-t")) {
			TrackMask = atoi(OptionArg.c_str());
			i++;
		} else if (!Option.compare("-d")) {
			DumpMask = atoi(OptionArg.c_str());
			i++;
		} else if (!Option.compare("-a")) {
			AudioFile = OptionArg;
			i++;
		} else if (!Option.compare("-s")) {
			IgnoreErrors = atoi(OptionArg.c_str());
			i++;
		} else if (!Option.compare("-m")) {
			if (!OptionArg.compare("default"))
				Demuxer = FFMS_SOURCE_DEFAULT;
			else if (!OptionArg.compare("lavf"))
				Demuxer = FFMS_SOURCE_LAVF;
			else if (!OptionArg.compare("matroska"))
				Demuxer = FFMS_SOURCE_MATROSKA;
			else if (!OptionArg.compare("haalimpeg"))
				Demuxer = FFMS_SOURCE_HAALIMPEG;
			else if (!OptionArg.compare("haaliogg"))
				Demuxer = FFMS_SOURCE_HAALIOGG;
			else
				std::cout << "Warning: invalid argument to -m (" << OptionArg << "), using default instead" << std::endl;

			i++;
		} else if (InputFile.empty()) {
			InputFile = argv[i];
		} else if (CacheFile.empty()) {
			CacheFile = argv[i];
		} else {
			std::cout << "Warning: ignoring unknown option " << argv[i] << std::endl;
		}

		i++;
	}

	if (IgnoreErrors < 0 || IgnoreErrors > 3)
		throw "Error: invalid error handling mode";
	if (InputFile.empty())
		throw "Error: no input file specified";

	if (CacheFile.empty()) {
		CacheFile = InputFile;
		CacheFile.append(".ffindex");
	}
	AudioFile.append("%s.%d2.w64");
}


static int FFMS_CC UpdateProgress(int64_t Current, int64_t Total, void *Private) {
	if (!PrintProgress)
		return 0;

	using namespace std;
	int *LastPercentage = (int *)Private;
	int Percentage = int((double(Current)/double(Total)) * 100);

	if (Percentage <= *LastPercentage)
		return 0;

	*LastPercentage = Percentage;

	/*if (Percentage < 10)
		cout << "\b\b";
	else
		cout << "\b\b\b"; */

	cout << "Indexing, please wait... " << Percentage << "% \r" << flush;

	return 0;
}


static int FFMS_CC GenAudioFilename(const char *SourceFile, int Track, const FFMS_AudioProperties *AP, char *FileName, int FNSize, void *Private) {
	const char * FormatString = AudioFile.c_str();
	if (FileName == NULL)
		return snprintf(NULL, 0, FormatString, SourceFile, Track) + 1;
	else
		return snprintf(FileName, FNSize, FormatString, SourceFile, Track) + 1;
}


static void DoIndexing () {
	char ErrorMsg[1024];
	FFMS_ErrorInfo E;
	E.Buffer = ErrorMsg;
	E.BufferSize = sizeof(ErrorMsg);

	int Progress = 0;

	Index = FFMS_ReadIndex(CacheFile.c_str(), &E);
	if (Overwrite || Index == NULL) {
		if (PrintProgress)
			std::cout << "Indexing, please wait... 0% \r" << std::flush;
		FFMS_Indexer *Indexer = FFMS_CreateIndexerWithDemuxer(InputFile.c_str(), Demuxer, &E);
		Index = FFMS_DoIndexing(Indexer, TrackMask, DumpMask, &GenAudioFilename, NULL, IgnoreErrors, UpdateProgress, &Progress, &E);
		if (Index == NULL) {
			std::string Err = "\nIndexing error: ";
			Err.append(E.Buffer);
			throw Err;
		}

		if (Progress != 100 && PrintProgress)
			std::cout << "Indexing, please wait... 100%" << std::endl << std::flush;

		if (WriteTC) {
			if (PrintProgress)
				std::cout << "Writing timecodes... ";
			int NumTracks = FFMS_GetNumTracks(Index);
			for (int t = 0; t < NumTracks; t++) {
				FFMS_Track *Track = FFMS_GetTrackFromIndex(Index, t);
				if (FFMS_GetTrackType(Track) == FFMS_TYPE_VIDEO && FFMS_GetNumFrames(Track)) {
					char tn[3];
					snprintf(tn, 3, "%02d", t);
					std::string TCFilename = CacheFile;
					TCFilename = TCFilename + "_track" + tn + ".tc.txt";
					if (FFMS_WriteTimecodes(Track, TCFilename.c_str(), &E))
						std::cout << std::endl << "Failed to write timecodes file "
							<< TCFilename << ": " << E.Buffer << std::endl << std:: flush;
				}
			}
			if (PrintProgress)
				std::cout << "done." << std::endl << std::flush;
		}

		if (PrintProgress)
			std::cout << "Writing index... ";

		if (FFMS_WriteIndex(CacheFile.c_str(), Index, &E)) {
			std::string Err = "Error writing index: ";
			Err.append(E.Buffer);
			throw Err;
		}

		if (PrintProgress)
			std::cout << "done." << std::endl << std::flush;

	} else {
		throw "Error: index file already exists, use -f if you are sure you want to overwrite it.";
	}
}


#if defined(_WIN32) && !defined(__MINGW32__)
int wmain(int argc, wchar_t *_argv[]) {
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
		ParseCMDLine(argc, argv);
	} catch (const char *Error) {
		std::cout << std::endl << Error << std::endl;
		return 1;
	} catch (std::string Error) {
		std::cout << std::endl << Error << std::endl;
		return 1;
	} catch (...) {
		std::cout << std::endl << "Unknown error" << std::endl;
		return 1;
	}

#ifdef _WIN32
	if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
		std::cout << "COM initialization failure" << std::endl;
		return 1;
	}
#endif /* _WIN32 */

#if defined(_WIN32) && !defined(__MINGW32__)
	FFMS_Init(0, 1);
#else
	FFMS_Init(0, 0);
#endif

	switch (Verbose) {
		case 0: FFMS_SetLogLevel(AV_LOG_QUIET); break;
		case 1: FFMS_SetLogLevel(AV_LOG_WARNING); break;
		case 2: FFMS_SetLogLevel(AV_LOG_INFO); break;
		case 3:	FFMS_SetLogLevel(AV_LOG_VERBOSE); break;
		default: FFMS_SetLogLevel(AV_LOG_DEBUG); // if user used -v 4 or more times, he deserves the spam
	}

	try {
		DoIndexing();
	} catch (const char *Error) {
		std::cout << Error << std::endl;
		FFMS_DestroyIndex(Index);
		return 1;
	} catch (std::string Error) {
		std::cout << std::endl << Error << std::endl;
		FFMS_DestroyIndex(Index);
		return 1;
	} catch (...) {
		std::cout << std::endl << "Unknown error" << std::endl;
		FFMS_DestroyIndex(Index);
		return 1;
	}

	FFMS_DestroyIndex(Index);
#ifdef _WIN32
	CoUninitialize();
#endif
	return 0;
}
