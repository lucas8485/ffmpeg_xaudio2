// ffmpeg_xaudio2.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include "audio_play_interface.hpp"
#include <cstdio>
#include <cstring>

#if !defined(UNREFERENCED_PARAMETER)
#define UNREFERENCED_PARAMETER(P) (P)
#endif

int main()
{
	char s[3000], s_1[3000]; int dummy_return_value;
	memset(s, 0, sizeof(s));
	memset(s_1, 0, sizeof(s_1));
	std::printf("info: audio decode/playback cli\n");
	std::printf("info: decode frontend: avformat version %d, avcodec version %d, avutil version %d, swresample version %d\n",
		avformat_version(),
		avcodec_version(),
		avutil_version(),
		swresample_version());
	std::printf("info: playback backend: xaudio2, version %s\n", audio::get_backend_implement_version());
	std::printf("info: drag audio file into the console, or enter audio file path.\n");
	::gets_s(s, 3000);
	size_t s_len = std::strlen(s);
	if (s_len < 5)
	{
		std::printf("warn: not valid filename, playing test.flac by default\n");
		strcpy(s, "test.flac");
	}
	else if (s[0] == '\"' && s[s_len - 1] == '\"')
	{
		strcpy(s_1, s + 1);
		s_1[s_len - 2] = '\0';
		strcpy(s, s_1);
	}
	if (audio::load_audio_context(s))
	{
		std::printf("err: load_audio_context failed!\n");
		return -1;
	}
	if (audio::initialize_audio_engine())
	{
		std::printf("err: audio engine initialize failed!\n");
		return -1;
	}

	std::printf("info: press enter to start playback.\n");
	std::printf("info: during playback, press enter to stop playback.\n");
	dummy_return_value = std::getchar();

	audio::start_audio_playback();
	dummy_return_value = std::getchar();
	UNREFERENCED_PARAMETER(dummy_return_value);
	audio::uninitialize_audio_engine();
	audio::release_audio_context();

	// check mem leak
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtDumpMemoryLeaks();
	return 0;
}


// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
