#include "ffmpeg_xaudio2_internal.hpp"
#include <xaudio2.h>
#include <thread>
#include <mutex>
#include <list>
#include <algorithm>
#include <cassert>
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "swresample.lib")

namespace audio
{
	std::mutex audio_playback_mutex;

	IXAudio2* xaudio2 = nullptr;
	IXAudio2MasteringVoice* mastering_voice = nullptr;
	IXAudio2SourceVoice* source_voice = nullptr;
	SwrContext* swr_ctx = nullptr;
	WAVEFORMATEX wfx = {};
	std::atomic<bool> xaudio2_buffer_ended = false;
	std::atomic<audio_playback_state> playback_state;
	std::thread* audio_player_worker_thread = nullptr;

	std::list<XAUDIO2_BUFFER*> xaudio2_playing_buffers = {};
	std::list<XAUDIO2_BUFFER*> xaudio2_free_buffers = {};
	size_t xaudio2_played_buffers = 0, xaudio2_allocated_buffers = 0, xaudio2_played_samples = 0;
	uint8_t* out_buffer = nullptr;
	size_t out_buffer_size = 0;
	// sample size = wfx.nBlockAlign

	void xaudio2_init_buffer(XAUDIO2_BUFFER* dest_buffer, int size = 8192)
	{
		if (size < 8192) size = 8192;
		int& buffer_size = *(int*)dest_buffer->pContext;
		if (size > buffer_size)
		{
			std::printf("info: xaudio2 reallocate_buffer, reallocate_size=%d, original_size=%d\n", size, buffer_size);
			delete dest_buffer->pAudioData;
			dest_buffer->pAudioData = new BYTE[size];
			buffer_size = size;
		}
		memset(const_cast<BYTE*>(dest_buffer->pAudioData), 0, size);
	}

	XAUDIO2_BUFFER* xaudio2_allocate_buffer(int size = 8192)
	{
		if (size < 8192) size = 8192;
		std::printf("info: xaudio2_allocate_buffer, allocate_size=%d\n", size);
		XAUDIO2_BUFFER* dest_buffer = new XAUDIO2_BUFFER{};
		dest_buffer->pAudioData = new BYTE[size];
		dest_buffer->pContext = new int(size);
		xaudio2_init_buffer(dest_buffer);
		return dest_buffer;
	}

	XAUDIO2_BUFFER* xaudio2_get_available_buffer(int size = 8192)
	{
		// std::printf("info: new xaudio2_buffer request, allocated=%lld, played=%lld\n", xaudio2_allocated_buffers, xaudio2_played_buffers);
		if (xaudio2_free_buffers.size() > 0)
		{
			// std::printf("info: free buffer recycled\n");
			auto dest_buffer = xaudio2_free_buffers.front();
			xaudio2_free_buffers.pop_front();
			xaudio2_init_buffer(dest_buffer, size);
			xaudio2_playing_buffers.push_back(dest_buffer);
			return dest_buffer;
		}
		// Allocate a new XAudio2 buffer.
		xaudio2_playing_buffers.push_back(xaudio2_allocate_buffer(size));
		xaudio2_allocated_buffers++;
		// std::printf("info: new xaudio2 buffer allocated, current allocate: %lld\n", xaudio2_allocated_buffers);
		return xaudio2_playing_buffers.back();
	}

	// void xaudio2_init_buffers()
	// {
	// 	constexpr int buffer_initial_size = 512;
	// 	for (int i = 0; i < buffer_initial_size; ++i)
	// 	{
	// 		auto new_buffer = new XAUDIO2_BUFFER;
	// 		new_buffer->pAudioData = new BYTE[8192];
	// 		xaudio2_playing_buffers.push_back(new_buffer);
	// 	}
	// 	xaudio2_allocated_buffers = buffer_initial_size;
	// }

	void xaudio2_free_buffer()
	{
		for (auto& i : xaudio2_playing_buffers)
		{
			assert(i);
			delete[] i->pAudioData;
			delete i;
			i = nullptr;
		}
		xaudio2_allocated_buffers = 0; xaudio2_played_buffers = 0;
		xaudio2_playing_buffers.clear();
	}

	int initialize_audio_engine()
	{
		// 初始化swscale
		auto stereo_layout = AVChannelLayout(AV_CHANNEL_LAYOUT_STEREO);

		swr_alloc_set_opts2(
			&swr_ctx,
			&stereo_layout,              // 输出立体声
			AV_SAMPLE_FMT_S16,
			44100,
			&codec_context->ch_layout, 
			codec_context->sample_fmt,
			codec_context->sample_rate,
			0, nullptr
		);
		out_buffer = new uint8_t[8192];
		auto res = swr_init(swr_ctx);
		if (res < 0) {
			char* buf = new char[1024];
			memset(buf, 0, sizeof(buf));
			av_strerror(res, buf, 1024);
			std::printf("err: swr_init failed, reason=%s\n", buf);
			delete[] buf;
			uninitialize_audio_engine();
			return -1;
		}

		// 初始化xaudio2
		HRESULT hr = CoInitialize(nullptr);
		if (FAILED(hr))
		{
			std::printf("err: CoInitialize failed\n");
			uninitialize_audio_engine();
			return -1;
		}

		// 创建xaudio2组件
		hr = XAudio2Create(&xaudio2);
		if (FAILED(hr))
		{
			std::printf("err: create xaudio2 com object failed\n");
			uninitialize_audio_engine();
			return -1;
		}

		// 掌控声音（确信）
		hr = xaudio2->CreateMasteringVoice(&mastering_voice);
		if (FAILED(hr)) {
			std::printf("err: creating mastering voice failed\n");
			uninitialize_audio_engine();
			return -1;
		}

		// 创建source voice
		wfx.wFormatTag = WAVE_FORMAT_PCM;                     // pcm格式
		wfx.nChannels = 2;                                    // 音频通道数
		wfx.nSamplesPerSec = 44100;                           // 采样率
		wfx.wBitsPerSample = 16;  // xaudio2支持16-bit pcm，如果不符合格式的音频，使用swscale进行转码
		wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels; // 样本大小：样本大小(16-bit)*通道数
		wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign; // 每秒钟解码多少字节，样本大小*采样率
		wfx.cbSize = sizeof(wfx);
		hr = xaudio2->CreateSourceVoice(&source_voice, &wfx);
		if (FAILED(hr))
		{
			std::printf("err: create source voice failed\n");
			uninitialize_audio_engine();
			return -1;
		}
		frame = av_frame_alloc();
		packet = av_packet_alloc();

		return 0;
	}

	void uninitialize_audio_engine()
	{
		// 等待xaudio线程执行完成
		if (audio_player_worker_thread)
		{
			playback_state =
				audio_playback_state::stopped;
			audio_player_worker_thread->join();
			delete audio_player_worker_thread;
		}
		if (swr_ctx)
		{
			swr_close(swr_ctx);
			swr_free(&swr_ctx);
		}
		if (out_buffer)
		{
			delete[] out_buffer;
			out_buffer = nullptr;
		}
		if (source_voice) {
			source_voice->Stop(0);
			source_voice->FlushSourceBuffers();
			source_voice->DestroyVoice();
			source_voice = nullptr;
		}
		if (mastering_voice) {
			mastering_voice->DestroyVoice();
			mastering_voice = nullptr;
		}
		if (xaudio2) {
			xaudio2->Release();
			xaudio2 = nullptr;
		}
		if (frame) {
			av_frame_free(&frame);
			frame = nullptr;
		}
		if (packet) {
			av_packet_free(&packet);
			packet = nullptr;
		}
		// 释放com库
		xaudio2_free_buffer();
	}

	void audio_playback_worker_thread()
	{
		HRESULT hr;
		XAUDIO2_VOICE_STATE state;
		while (true) {
			// 创建线程同步锁，防止线程竞速
			std::lock_guard<std::mutex> audio_playback_lock_guard(audio_playback_mutex);
			if (file_stream_end)
				playback_state =
					audio_playback_state::stopped;


			source_voice->GetState(&state);
			if (playback_state ==
				audio_playback_state::stopped)
			{
				if (file_stream_end && state.BuffersQueued > 0)
				{
					std::printf("info: file stream ended, waiting for xaudio2 flush buffer\n");
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				else
				{
					std::printf("info: playback finished\n"); \
						break; // 读取结束
				}
			}
			// 从输入文件中读取数据并解码
			if (av_read_frame(format_context, packet) < 0) {
				playback_state =
					audio_playback_state::stopped;
				continue;
			}

			if (packet->stream_index == audio_stream_index) {
				int res = avcodec_send_packet(codec_context, packet);
				if (res < 0) {
					av_packet_unref(packet);
					continue; // 错误处理
				}

				while (true) {
					res = avcodec_receive_frame(codec_context, frame);
					if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
						break; // 没有更多帧
					}
					else if (res < 0) {
						std::printf("err: avcodec_receive_frame failed\n");
						playback_state =
							audio_playback_state::stopped;
						break;
					}

					// 创建输出缓冲区
					out_buffer_size = sizeof(uint8_t) * frame->nb_samples * wfx.nBlockAlign;
					if (out_buffer)
						delete[] out_buffer;
					out_buffer = new uint8_t[out_buffer_size];
					memset(out_buffer, 0, out_buffer_size);
					int out_samples = swr_convert(swr_ctx, &out_buffer, frame->nb_samples,
						(const uint8_t**)frame->data, frame->nb_samples);

					if (out_samples < 0) {
						std::printf("err: swr_convert failed\n");
						av_frame_unref(frame);
						break;
					}

					while (state.BuffersQueued >= 64)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
						source_voice->GetState(&state);
					}

					// 将转换后的音频数据输出到xaudio2
					XAUDIO2_BUFFER* buffer = xaudio2_get_available_buffer(out_samples * wfx.nBlockAlign);
					buffer->AudioBytes = out_samples * wfx.nBlockAlign; // 每样本2字节，每声道2字节
					memcpy(const_cast<BYTE*>(buffer->pAudioData), out_buffer, buffer->AudioBytes);

					hr = source_voice->SubmitSourceBuffer(buffer);
					if (FAILED(hr)) {
						std::printf("err: submit source buffer failed, reason=0x%x\n", hr);
						playback_state =
							audio_playback_state::stopped;
						break;
					}

					source_voice->GetState(&state);
					// std::printf("info: submitted source buffer, buffers queued=%d\n", state.BuffersQueued);

					// 播放音频
					// source_voice->GetState(&state);
					if (playback_state == audio_playback_state::init)
					{
						// if (state.BuffersQueued == 32)
						// {
							playback_state = audio_playback_state::playing;
							source_voice->Start();
						// }
					}
					else
					{
						auto samples_played_before = state.SamplesPlayed;
						auto samples_sum = xaudio2_played_samples;
						auto played_buffers = xaudio2_played_buffers; auto it = xaudio2_playing_buffers.begin();
						bool flag = false;
						while (it != xaudio2_playing_buffers.end())
						{
							XAUDIO2_BUFFER*& played_buffer = *it;
							played_buffers++;
							samples_sum += played_buffer->AudioBytes / wfx.nBlockAlign;
							if (samples_sum >= state.SamplesPlayed)
							{
								flag = true;
								break;
							}
							else
								++it;
						}

						if (it != xaudio2_playing_buffers.begin())
						{
							// --it;
							xaudio2_free_buffers.insert(xaudio2_free_buffers.end(),
								xaudio2_playing_buffers.begin(), it);
							xaudio2_playing_buffers.erase(xaudio2_playing_buffers.begin(), it);
							xaudio2_played_buffers = played_buffers - 1;
							xaudio2_played_samples = samples_sum - (*it)->AudioBytes / 4;
							printf("info: samples played=%lld, cur played_buffers=%lld, cur samples=%lld, xaudio2 buffer arr size=%lld\n",
								state.SamplesPlayed, played_buffers, samples_sum, xaudio2_playing_buffers.size());
							// std::printf("info: buffer played=%zd\n", played_buffers);
						}
						// else
						{
							// std::printf("info: buffer played=%zd\n", xaudio2_played_buffers);
						}
						//  (wfx.wBitsPerSample / 8) * wfx.nChannels
						av_frame_unref(frame);
					}
				}
			}
			av_packet_unref(packet);
		}
	}

	void start_audio_playback()
	{
		playback_state = audio_playback_state::init;
		audio_player_worker_thread = new std::thread(audio_playback_worker_thread);
	}

	const char* get_backend_implement_version()
	{
		static char xaudio2_implement_version[] = XAUDIO2_DLL_A;
		return xaudio2_implement_version;
	}
}