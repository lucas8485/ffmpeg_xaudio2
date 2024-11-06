#include "audio_play_interface.hpp"

namespace audio
{
	extern AVFormatContext* format_context;
	extern AVCodec* codec;
	extern AVCodecContext* codec_context;
	extern AVPacket* packet;
	extern AVFrame* frame;
	extern unsigned audio_stream_index;
	extern AVIOContext* avio_context;
	extern unsigned char* buffer;
	extern bool file_stream_end;
}