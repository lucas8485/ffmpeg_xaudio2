#if !defined(AUDIO_PLAY_INTERFACE_HPP_)
#define AUDIO_PLAY_INTERFACE_HPP_
#if defined(_MSC_VER) // if uses msvc...
#define _CRT_SECURE_NO_WARNINGS
#pragma warning (disable: 4819) // avoid msvc utf-8 warning
#endif

#define _CRTDBG_MAP_ALLOC
#ifdef _DEBUG
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
// Replace _NORMAL_BLOCK with _CLIENT_BLOCK if you want the
// allocations to be of _CLIENT_BLOCK type
#else
#define DBG_NEW new
#endif
#include <cstdlib>
#include <crtdbg.h>

#if defined(_WIN32) || defined(WIN32) || defined(__cplusplus)
extern "C" {
#endif
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#if defined(_WIN32) || defined(WIN32) || defined(__cplusplus)
}
#endif

namespace audio
{
	enum class audio_playback_state
	{
		init, playing, paused, stopped
	};
	int load_audio_context(const char*);
	void release_audio_context();

	int initialize_audio_engine();
	void uninitialize_audio_engine();
	void audio_playback_worker_thread();
	void start_audio_playback(); 
	const char* get_backend_implement_version();
}

#endif // AUDIO_PLAY_INTERFACE_HPP_