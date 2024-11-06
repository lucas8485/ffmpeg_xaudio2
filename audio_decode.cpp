#include <cstdlib>
#include <cstdio>
#include <fstream>
#include "ffmpeg_xaudio2_internal.hpp"

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")

namespace audio
{
	// 流文件解析上下文
	AVFormatContext* format_context = nullptr;
	// 针对该文件，找到的解码器类型
	AVCodec* codec = nullptr;
	// 使用的解码器实例
	AVCodecContext* codec_context = nullptr;
	// 解码前的数据（流中的一个packet）
	AVPacket* packet = nullptr;
	// 解码后的数据（一帧数据）
	AVFrame* frame = nullptr;
	// 音频流编号
	unsigned audio_stream_index = static_cast<unsigned>(-1); // inf
	AVIOContext* avio_context = nullptr;
	unsigned char* buffer = nullptr;
	std::ifstream* file_stream = nullptr;
	bool file_stream_end = false;

	int64_t get_stream_rest_len(std::istream& stream)
	{
		auto pos = stream.tellg();
		stream.seekg(0, std::ios::end);
		size_t rest_len = stream.tellg() - pos;
		stream.seekg(pos);
		return rest_len;
	}

	static int read_func(void* opaque, uint8_t* buf, int buf_size) {
		auto& fin = *(std::ifstream*)opaque;
		auto rest_len = get_stream_rest_len(fin);
		std::printf("info: read buf_size=%d, rest=%lld\n", buf_size, rest_len);
		if (rest_len == 0)
		{
			file_stream_end = true; return -1; // read operation failed
		}
		fin.read(reinterpret_cast<char*>(buf), buf_size);
		return static_cast<int>(fin.gcount());
	}

	int64_t seek_func(void* opaque, int64_t offset, int whence)
	{
		std::printf("info: seek offset=%lld, whence=%d\n", offset, whence);
		auto& me = *reinterpret_cast<std::istream*>(opaque);
		auto pos = me.tellg();
		me.seekg(0, std::ios::beg);
		auto start_pos = me.tellg();
		me.seekg(0, std::ios::end);
		auto end_pos = me.tellg();
		size_t file_len = end_pos - start_pos;
		me.seekg(pos);
		switch (whence)
		{
		case AVSEEK_SIZE:
		{
			// std::printf("info: AVSEEK_SIZE triggered\n");
			return file_len;
		}
		default:
			me.seekg(offset, whence);
			return me.tellg();
		}
	}

	int load_audio_context(const char* audio_filename)
	{
		// 打开文件流
		// std::ios::sync_with_stdio(false);
		file_stream = DBG_NEW std::ifstream(audio_filename, std::ios::binary);
		auto& fin = *file_stream;
		if (!fin.good())
		{
			std::printf("err: file not exists!\n");
			delete file_stream;
			return -1;
		}

		char* buf = DBG_NEW char[1024];
		memset(buf, 0, sizeof(buf));

		// 取得文件大小
		format_context = avformat_alloc_context();
		auto pos = fin.tellg();
		fin.seekg(0, std::ios::end);
		size_t file_len = fin.tellg() - pos;
		fin.seekg(pos);
		std::printf("info: file loaded, size = %zu\n", file_len);

		constexpr size_t avio_buf_size = 8192;


		buffer = reinterpret_cast<unsigned char*>(av_malloc(avio_buf_size));
		avio_context =
			avio_alloc_context(buffer, avio_buf_size, 0,
				reinterpret_cast<void*>(static_cast<std::istream*>(&fin)), &read_func, nullptr, &seek_func);

		format_context->pb = avio_context;

		// 打开音频文件
		int res = avformat_open_input(&format_context, 
			nullptr, // dummy parameter, read from memory stream
			nullptr, // let ffmpeg auto detect format
			nullptr  // no parateter specified
		);
		if (!format_context)
		{
			av_strerror(res, buf, 1024);
			std::printf("err: avformat_open_input failed, reason = %s(%d)\n", buf, res);
			release_audio_context();
			delete[] buf;
			return -1;
		}

		res = avformat_find_stream_info(format_context, nullptr);
		if (res == AVERROR_EOF)
		{
			std::printf("err: no stream found in file\n");
			release_audio_context();
			delete[] buf;
			return -1;
		}

		for (audio_stream_index = 0; audio_stream_index < format_context->nb_streams; ++audio_stream_index)
		{
			// 枚举当前文件中所有流
			AVStream* current_stream = format_context->streams[audio_stream_index];
			if (current_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				std::printf("info: scan stream %d\n", audio_stream_index);
				// 找到音频流，进入音频流解码
				if (current_stream->codecpar->sample_rate <= 0
					|| current_stream->codecpar->ch_layout.nb_channels <= 0)
				{
					std::printf("warn: invalid audio resolution, skipped stream id %d\n", audio_stream_index);
					continue;
				}

				// 侦测解码器
				codec = const_cast<AVCodec*>(avcodec_find_decoder(current_stream->codecpar->codec_id));
				if (!codec)
				{
					std::printf("warn: no valid decoder found, skipped stream id %d\n", audio_stream_index);
					continue;
				}

				// 选中解码器后，输出此通道信息，并停止音频解析
				std::printf("info: open stream id %d, format=%d, sample_rate=%d, channels=%d, channel_layout=%d\n",
					audio_stream_index,
					current_stream->codecpar->format,
					current_stream->codecpar->sample_rate,
					current_stream->codecpar->ch_layout.nb_channels,
					current_stream->codecpar->ch_layout.order);
				break;
			}
		}

		if (audio_stream_index == format_context->nb_streams
			|| !codec)
		{
			// 未选中任何解码器
			std::printf("err: no codec selected, aborting...\n");
			release_audio_context();
			delete[] buf;
			return -1;
		}

		// 从0ms开始读取
		avformat_seek_file(format_context, -1, INT64_MIN, 0, INT64_MAX, 0);
		// codec is not null
		// 建立解码器上下文
		codec_context = avcodec_alloc_context3(codec);
		if (codec_context == nullptr)
		{
			std::printf("err: avcodec_alloc_context3 failed\n");
			release_audio_context();
			delete[] buf;
			return -1;
		}
		avcodec_parameters_to_context(codec_context, format_context->streams[audio_stream_index]->codecpar);

		// 解码文件
		res = avcodec_open2(codec_context, nullptr, nullptr);
		if (res)
		{
			av_strerror(res, buf, 1024);
			std::printf("err: avcodec_open2 failed, reason = %s\n", buf);
			release_audio_context();
			delete[] buf;
			return -1;
		}

		// avoid ffmpeg warning
		codec_context->pkt_timebase = format_context->streams[audio_stream_index]->time_base;

		delete[] buf;
		return 0;
	}

	void release_audio_context()
	{
		if (avio_context)
		{
			// 释放缓冲区上下文
			avio_context_free(&avio_context);
			avio_context = nullptr;
		}
		if (format_context)
		{
			// 释放文件解析上下文
			avformat_close_input(&format_context);
			format_context = nullptr;
		}

		if (codec_context)
		{
			// 释放解码器上下文
			avcodec_free_context(&codec_context);
			codec_context = nullptr;
		}
		if (file_stream)
		{
			delete file_stream;
			file_stream = nullptr;
		}
	}
}