//  Copyright (c) 2010 Thomas Goyne <tgoyne@gmail.com>
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

#include "audiosource.h"

#include "indexing.h"

#include <algorithm>
#include <cassert>

extern "C" {
#if VERSION_CHECK(LIBAVUTIL_VERSION_INT, >=, 52, 2, 0, 52, 6, 100)
#include <libavutil/channel_layout.h>
#endif
}

namespace {
#define MAPPER(m, n) OptionMapper<FFMS_ResampleOptions>(n, &FFMS_ResampleOptions::m)
OptionMapper<FFMS_ResampleOptions> resample_options[] = {
	MAPPER(ChannelLayout,          "out_channel_layout"),
	MAPPER(SampleFormat,           "out_sample_fmt"),
	MAPPER(SampleRate,             "out_sample_rate"),
	MAPPER(MixingCoefficientType,  "mix_coeff_type"),
	MAPPER(CenterMixLevel,         "center_mix_level"),
	MAPPER(SurroundMixLevel,       "surround_mix_level"),
	MAPPER(LFEMixLevel,            "lfe_mix_level"),
	MAPPER(Normalize,              "normalize_mix_level"),
	MAPPER(ForceResample,          "force_resampling"),
	MAPPER(ResampleFilterSize,     "filter_size"),
	MAPPER(ResamplePhaseShift,     "phase_shift"),
	MAPPER(LinearInterpolation,    "linear_interp"),
	MAPPER(CutoffFrequencyRatio,   "cutoff"),
	MAPPER(MatrixedStereoEncoding, "matrix_encoding"),
	MAPPER(FilterType,             "filter_type"),
	MAPPER(KaiserBeta,             "kaiser_beta"),
	MAPPER(DitherMethod,           "dither_method")
};
#undef MAPPER
}

FFMS_AudioSource::FFMS_AudioSource(const char *SourceFile, FFMS_Index &Index, int Track)
: TrackNumber(Track)
{
	if (Track < 0 || Track >= static_cast<int>(Index.size()))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Out of bounds track index selected");

	if (Index[Track].TT != FFMS_TYPE_AUDIO)
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Not an audio track");

	if (Index[Track].empty())
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Audio track contains no audio frames");

	if (!Index.CompareFileSignature(SourceFile))
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_FILE_MISMATCH,
			"The index does not match the source file");

	Frames = Index[Track];
}

#define EXCESSIVE_CACHE_SIZE 400

void FFMS_AudioSource::Init(const FFMS_Index &Index, int DelayMode) {
	// Decode the first packet to ensure all properties are initialized
	// Don't cache it since it might be in the wrong format
	while (DecodeFrame->nb_samples == 0)
		DecodeNextBlock();

	// Read properties of the audio which may not be available until the first
	// frame has been decoded
	FillAP(AP, CodecContext, Frames);

	if (AP.SampleRate <= 0 || AP.BitsPerSample <= 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_CODEC,
			"Codec returned zero size audio");

	auto opt = CreateResampleOptions();
	SetOutputFormat(*opt);

	if (DelayMode < FFMS_DELAY_NO_SHIFT)
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Bad audio delay compensation mode");

	if (DelayMode == FFMS_DELAY_NO_SHIFT) return;

	if (DelayMode > (signed)Index.size())
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Out of bounds track index selected for audio delay compensation");

	if (DelayMode >= 0 && Index[DelayMode].TT != FFMS_TYPE_VIDEO)
		throw FFMS_Exception(FFMS_ERROR_INDEX, FFMS_ERROR_INVALID_ARGUMENT,
			"Audio delay compensation must be relative to a video track");

	if (DelayMode == FFMS_DELAY_FIRST_VIDEO_TRACK) {
		for (size_t i = 0; i < Index.size(); ++i) {
			if (Index[i].TT == FFMS_TYPE_VIDEO && !Index[i].empty()) {
				DelayMode = static_cast<int>(i);
				break;
			}
		}
	}

	if (DelayMode >= 0) {
		const FFMS_Track &VTrack = Index[DelayMode];
		Delay = -(VTrack[0].PTS * VTrack.TB.Num * AP.SampleRate / (VTrack.TB.Den * 1000));
	}

	if (Frames.HasTS) {
		int i = 0;
		while (Frames[i].PTS == ffms_av_nopts_value) ++i;
		Delay += Frames[i].PTS * Frames.TB.Num * AP.SampleRate / (Frames.TB.Den * 1000);
		for (; i > 0; --i)
			Delay -= Frames[i].SampleCount;
	}

	AP.NumSamples += Delay;
}

void FFMS_AudioSource::CacheBeginning() {
	// Nothing to do if the cache is already populated
	if (!Cache.empty()) return;

	// The first packet after a seek is often decoded incorrectly, which
	// makes it impossible to ever correctly seek back to the beginning, so
	// store the first block now

	// In addition, anything with the same PTS as the first packet can't be
	// distinguished from the first packet and so can't be seeked to, so
	// store those as well

	// Some of LAVF's splitters don't like to seek to the beginning of the
	// file (ts and?), so cache a few blocks even if PTSes are unique
	// Packet 7 is the last packet I've had be unseekable to, so cache up to
	// 10 for a bit of an extra buffer
	auto end = Cache.end();
	while (PacketNumber < Frames.size() &&
		((Frames[0].PTS != ffms_av_nopts_value && Frames[PacketNumber].PTS == Frames[0].PTS) ||
		 Cache.size() < 10)) {

		// Vorbis in particular seems to like having 60+ packets at the start
		// of the file with a PTS of 0, so we might need to expand the search
		// range to account for that.
		// Expanding slightly before it's strictly needed to ensure there's a
		// bit of space for an actual cache
		if (Cache.size() >= MaxCacheBlocks - 5) {
			 if (MaxCacheBlocks >= EXCESSIVE_CACHE_SIZE)
				throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_ALLOCATION_FAILED,
					"Exceeded the search range for an initial valid audio PTS");
			MaxCacheBlocks *= 2;
		}

		DecodeNextBlock(&end);
	}
	// Store the iterator to the last element of the cache which is used for
	// correctness rather than speed, so that when looking for one to delete
	// we know how much to skip
	CacheNoDelete = Cache.end();
	--CacheNoDelete;
}

void FFMS_AudioSource::SetOutputFormat(FFMS_ResampleOptions const& opt) {
	if (opt.SampleRate != AP.SampleRate)
		throw FFMS_Exception(FFMS_ERROR_RESAMPLING, FFMS_ERROR_UNSUPPORTED,
			"Sample rate changes are currently unsupported.");

	// Cache stores audio in the output format, so clear it and reopen the file
	Cache.clear();
	PacketNumber = 0;
	ReopenFile();
	FlushBuffers(CodecContext);

	BytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(opt.SampleFormat)) * av_get_channel_layout_nb_channels(opt.ChannelLayout);
	NeedsResample =
		opt.SampleFormat != (int)CodecContext->sample_fmt ||
		opt.SampleRate != AP.SampleRate ||
		opt.ChannelLayout != AP.ChannelLayout ||
		opt.ForceResample;

	if (!NeedsResample) return;

	FFResampleContext newContext;
	SetOptions(opt, newContext, resample_options);
	av_opt_set_int(newContext, "in_sample_rate", AP.SampleRate, 0);
	av_opt_set_int(newContext, "in_sample_fmt", CodecContext->sample_fmt, 0);
	av_opt_set_int(newContext, "in_channel_layout", AP.ChannelLayout, 0);

	av_opt_set_int(newContext, "out_sample_rate", opt.SampleRate, 0);

#ifdef WITH_SWRESAMPLE
	av_opt_set_channel_layout(newContext, "out_channel_layout", opt.ChannelLayout, 0);
	av_opt_set_sample_fmt(newContext, "out_sample_fmt", (AVSampleFormat)opt.SampleFormat, 0);
#endif

	if (ffms_open_resampler(newContext))
		throw FFMS_Exception(FFMS_ERROR_RESAMPLING, FFMS_ERROR_UNKNOWN,
			"Could not open avresample context");
	newContext.swap(ResampleContext);
}

std::unique_ptr<FFMS_ResampleOptions> FFMS_AudioSource::CreateResampleOptions() const {
	auto ret = ReadOptions(ResampleContext, resample_options);
	ret->SampleRate = AP.SampleRate;
	ret->SampleFormat = static_cast<FFMS_SampleFormat>(AP.SampleFormat);
	ret->ChannelLayout = AP.ChannelLayout;
	return ret;
}

void FFMS_AudioSource::ResampleAndCache(CacheIterator pos) {
	AudioBlock& block = *pos;
	size_t old_size = block.Data.size();
	size_t new_req = DecodeFrame->nb_samples * BytesPerSample;
	block.Data.resize(old_size + new_req);

	uint8_t *OutPlanes[1] = { static_cast<uint8_t *>(&block.Data[old_size]) };
	ffms_convert(ResampleContext,
		OutPlanes, DecodeFrame->nb_samples, BytesPerSample, DecodeFrame->nb_samples,
		DecodeFrame->extended_data, DecodeFrame->nb_samples, av_get_bytes_per_sample(CodecContext->sample_fmt), DecodeFrame->nb_samples);
}

void FFMS_AudioSource::CacheBlock(CacheIterator &pos) {
	// If the previous block has the same Start sample as this one, then
	// we got multiple frames of audio out of a single package and should
	// combine them
	auto block = pos;
	if (pos == Cache.begin() || (--block)->Start != CurrentSample)
		block = Cache.insert(pos, AudioBlock(CurrentSample));

	block->Samples += DecodeFrame->nb_samples;

	if (NeedsResample)
		ResampleAndCache(block);
	else {
		const uint8_t *data = DecodeFrame->extended_data[0];
		block->Data.insert(block->Data.end(), data, data + DecodeFrame->nb_samples * BytesPerSample);
	}

	if (Cache.size() >= MaxCacheBlocks) {
		// Kill the oldest one
		auto min = CacheNoDelete;
		// Never drop the first one as the first packet decoded after a seek
		// is often decoded incorrectly and we can't seek to before the first one
		++min;
		for (auto it = min; it != Cache.end(); ++it)
			if (it->Age < min->Age) min = it;
		if (min == pos) ++pos;
		Cache.erase(min);
	}
}

void FFMS_AudioSource::DecodeNextBlock(CacheIterator *pos) {
	CurrentFrame = &Frames[PacketNumber];

	AVPacket Packet;
	if (!ReadPacket(&Packet))
		throw FFMS_Exception(FFMS_ERROR_PARSER, FFMS_ERROR_UNKNOWN,
			"ReadPacket unexpectedly failed to read a packet");

	// ReadPacket may have changed the packet number
	CurrentFrame = &Frames[PacketNumber];
	CurrentSample = CurrentFrame->SampleStart;

	bool GotSamples = false;
	uint8_t *Data = Packet.data;
	while (Packet.size > 0) {
		DecodeFrame.reset();
		int GotFrame = 0;
		int Ret = avcodec_decode_audio4(CodecContext, DecodeFrame, &GotFrame, &Packet);

		// Should only ever happen if the user chose to ignore decoding errors
		// during indexing, so continue to just ignore decoding errors
		if (Ret < 0) break;

		if (Ret > 0) {
			Packet.size -= Ret;
			Packet.data += Ret;
			if (GotFrame && DecodeFrame->nb_samples > 0) {
				GotSamples = true;
				if (pos)
					CacheBlock(*pos);
			}
		}
	}
	Packet.data = Data;
	FreePacket(&Packet);

	// Zero sample packets aren't included in the index
	if (GotSamples)
		++PacketNumber;
}

static bool SampleStartComp(const FrameInfo &a, const FrameInfo &b) {
	return a.SampleStart < b.SampleStart;
}

void FFMS_AudioSource::GetAudio(void *Buf, int64_t Start, int64_t Count) {
	if (Start < 0 || Start + Count > AP.NumSamples || Count < 0)
		throw FFMS_Exception(FFMS_ERROR_DECODING, FFMS_ERROR_INVALID_ARGUMENT,
			"Out of bounds audio samples requested");

	CacheBeginning();

	uint8_t *Dst = static_cast<uint8_t*>(Buf);

	// Apply audio delay (if any) and fill any samples before the start time with zero
	Start -= Delay;
	if (Start < 0) {
		size_t Bytes = static_cast<size_t>(BytesPerSample * FFMIN(-Start, Count));
		memset(Dst, 0, Bytes);

		Count += Start;
		// Entire request was before the start of the audio
		if (Count <= 0) return;

		Start = 0;
		Dst += Bytes;
	}

	auto it = Cache.begin();

	while (Count > 0) {
		// Find first useful cache block
		while (it != Cache.end() && it->Start + it->Samples <= Start) ++it;

		// Cache has the next block we want
		if (it != Cache.end() && it->Start <= Start) {
			int64_t SrcOffset = FFMAX(0, Start - it->Start);
			int64_t DstOffset = FFMAX(0, it->Start - Start);
			int64_t CopySamples = FFMIN(it->Samples - SrcOffset, Count - DstOffset);
			size_t Bytes = static_cast<size_t>(CopySamples * BytesPerSample);

			memcpy(Dst + DstOffset * BytesPerSample, &it->Data[SrcOffset * BytesPerSample], Bytes);
			Start += CopySamples;
			Count -= CopySamples;
			Dst += Bytes;
			++it;
		}
		// Decode another block
		else {
			if (Start < CurrentSample && SeekOffset == -1)
				throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_CODEC, "Audio stream is not seekable");

			if (SeekOffset >= 0 && (Start < CurrentSample || Start > CurrentSample + DecodeFrame->nb_samples * 5)) {
				FrameInfo f;
				f.SampleStart = Start;
				size_t NewPacketNumber = std::distance(
					Frames.begin(),
					std::lower_bound(Frames.begin(), Frames.end(), f, SampleStartComp));
				NewPacketNumber = NewPacketNumber > static_cast<size_t>(SeekOffset + 15)
				                ? NewPacketNumber - SeekOffset - 15
				                : 0;
				while (NewPacketNumber > 0 && !Frames[NewPacketNumber].KeyFrame) --NewPacketNumber;

				// Only seek forward if it'll actually result in moving forward
				if (Start < CurrentSample || static_cast<size_t>(NewPacketNumber) > PacketNumber) {
					PacketNumber = NewPacketNumber;
					CurrentSample = -1;
					DecodeFrame.reset();
					avcodec_flush_buffers(CodecContext);
					Seek();
				}
			}

			// Decode until we hit the block we want
			if (PacketNumber >= Frames.size())
				throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_CODEC, "Seeking is severely broken");
			while (CurrentSample + DecodeFrame->nb_samples <= Start && PacketNumber < Frames.size())
				DecodeNextBlock(&it);
			if (CurrentSample > Start)
				throw FFMS_Exception(FFMS_ERROR_SEEKING, FFMS_ERROR_CODEC, "Seeking is severely broken");

			// The block we want is now in the cache immediately before it
			--it;
		}
	}
}

size_t GetSeekablePacketNumber(FFMS_Track const& Frames, size_t PacketNumber) {
	// Packets don't always have unique PTSes, so we may not be able to
	// uniquely identify the packet we want. This function attempts to find
	// a PTS we can seek to which will let us figure out which packet we're
	// on before we get to the packet we actually wanted

	// MatroskaAudioSource doesn't need this, as it seeks by byte offset
	// rather than PTS. LAVF theoretically can seek by byte offset, but we
	// don't use it as not all demuxers support it and it's broken in some of
	// those that claim to support it

	// However much we might wish to, we can't seek to before packet zero
	if (PacketNumber == 0) return PacketNumber;

	// Desired packet's PTS is unique, so don't do anything
	if (Frames[PacketNumber].PTS != Frames[PacketNumber - 1].PTS &&
		(PacketNumber + 1 == Frames.size() || Frames[PacketNumber].PTS != Frames[PacketNumber + 1].PTS))
		return PacketNumber;

	// When decoding, we only reliably know what packet we're at when the
	// newly parsed packet has a different PTS from the previous one. As such,
	// we walk backwards until we hit a different PTS and then seek to there,
	// so that we can then decode until we hit the PTS group we actually wanted
	// (and thereby know that we're at the first packet in the group rather
	// than whatever the splitter happened to choose)

	// This doesn't work if our desired packet has the same PTS as the first
	// packet, but this scenario should never come up anyway; we permanently
	// cache the decoded results from those packets, so there's no need to ever
	// seek to them
	int64_t PTS = Frames[PacketNumber].PTS;
	while (PacketNumber > 0 && PTS == Frames[PacketNumber].PTS)
		--PacketNumber;
	return PacketNumber;
}
