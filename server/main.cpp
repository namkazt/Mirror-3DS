#include <iostream>
#include "thirdparty/screen_capture_lite/include/ScreenCapture.h"


extern "C" {
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "libavutil/audio_fifo.h"
#include <libavutil/channel_layout.h>
}

typedef struct FPSCounter {
	uint32_t counter = 0;
	uint32_t fps = 0;
	uint32_t leftover = 0;
	std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::high_resolution_clock::now();
};
FPSCounter fps_counter;

void session(int frameRate, std::string file_name) {
	// only init file output if specific
	FILE* output_file;
	if (file_name.length() > 0) {
		fopen_s(&output_file, file_name.c_str(), "wb");
		if (!output_file) {
			fprintf(stderr, "Could not open %s\n", file_name);
			return;
		}
	}
	fps_counter.fps = frameRate;
	bool stop_recording = false;
	int ret = 0;
	// select capturing monitor
	std::vector<SL::Screen_Capture::Monitor> monitor = std::vector<SL::Screen_Capture::Monitor>();
	monitor.push_back(SL::Screen_Capture::GetMonitors()[0]);

	// init ffmpeg video encoder
	auto mv_codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
	if (!mv_codec) {
		fprintf(stderr, "Codec not found\n");
		return;
	}
	auto mv_parser = av_parser_init(mv_codec->id);
	if (!mv_parser) {
		fprintf(stderr, "parser not found\n");
		return;
	}
	auto mv_context = avcodec_alloc_context3(mv_codec);
	if (!mv_context) {
		fprintf(stderr, "Could not allocate video codec context\n");
		return;
	}
	/* For some codecs, such as msmpeg4 and mpeg4, width and height
	  MUST be initialized there because this information is not
	  available in the bitstream. */
	mv_context->bit_rate = 1600 * 1000;
	mv_context->width = 800;
	mv_context->height = 240;
	mv_context->time_base = AVRational{ 1, frameRate };
	mv_context->framerate = AVRational{ frameRate, 1 };
	// GOP size ( longer can provide better compression )
	// PAL: 15
	// NTSC: 18
	mv_context->gop_size = 18;
	mv_context->max_b_frames = 2;
	mv_context->block_align = 8;
	mv_context->pix_fmt = AVPixelFormat::AV_PIX_FMT_YUV420P;
	// open 
	if (avcodec_open2(mv_context, mv_codec, NULL) < 0) {
		fprintf(stderr, "Could not open codec\n");
		return;
	}
	auto mv_frame = av_frame_alloc();
	if (!mv_frame) {
		fprintf(stderr, "Could not alloc frame\n");
		return;
	}
	mv_frame->format = mv_context->pix_fmt;
	mv_frame->width = mv_context->width;
	mv_frame->height = mv_context->height;
	ret = av_frame_get_buffer(mv_frame, 0);
	if (ret > 0) {
		fprintf(stderr, "Could not get frame buffer\n");
		return;
	}
	auto mv_packet = av_packet_alloc();
	if (!mv_packet) {
		fprintf(stderr, "Could not alloc packet\n");
		return;
	}
	auto mv_scaler = sws_getContext(monitor[0].Width, monitor[0].Height, AVPixelFormat::AV_PIX_FMT_BGRA, mv_context->width, mv_context->height, mv_context->pix_fmt, SWS_LANCZOS, NULL, NULL, NULL);
	if (!mv_scaler) {
		fprintf(stderr, "Could not get scaler\n");
		return;
	}
	int mv_frame_stride[1] = { monitor[0].Width * 4 };
	size_t frame_size = monitor[0].Width * monitor[0].Height * sizeof(SL::Screen_Capture::ImageBGRA);
	auto frame_buffer(std::make_unique<unsigned char[]>(frame_size));
	uint64_t frame_idx = 0;
	uint32_t frame_ticks = 1000 / frameRate;
	auto last_frametick = std::chrono::high_resolution_clock::now();
	// encode func
	auto encode_func = [&](uint8_t *frame_buffer, AVFrame* frame) {
		if (frame) {
			frame->pts = frame_idx;
			frame_idx += 1;
			// scale frame
			const unsigned char* src_frame_data[1] = { frame_buffer };
			sws_scale(mv_scaler, src_frame_data, mv_frame_stride, 0, monitor[0].Height, frame->data, frame->linesize);
		}
		// encode frame
		ret = avcodec_send_frame(mv_context, frame);
		if (ret > 0) {
			fprintf(stderr, "Could not send frame to context\n");
			return;
		}
		// receive packet
		while (ret >= 0) {
			ret = avcodec_receive_packet(mv_context, mv_packet);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			}
			else if (ret < 0) {
				fprintf(stderr, "Error during encoding\n");
				return;
			}
			// write to file
			if (file_name.length() > 0) {
				fwrite(mv_packet->data, 1, mv_packet->size, output_file);
			}
			// free packet
			av_packet_unref(mv_packet);
		}
	};

	// start it
	auto screen_capture = SL::Screen_Capture::CreateCaptureConfiguration([monitor]() {
		return monitor;
	})->onNewFrame([&](const SL::Screen_Capture::Image& img, const SL::Screen_Capture::Monitor& monitor) {
		if (stop_recording) return;
		SL::Screen_Capture::Extract(img, frame_buffer.get(), frame_size);
		//mark frame is writeable
		ret = av_frame_make_writable(mv_frame);
		if (ret < 0) {
			fprintf(stderr, "Could not mark frame was writeable\n");
			return;
		}
		encode_func(frame_buffer.get(), mv_frame);
	})->start_capturing();
	// set fps
	screen_capture->setFrameChangeInterval(std::chrono::milliseconds(frame_ticks));



	// loop until session is complete
	std::this_thread::sleep_for(std::chrono::duration<long long, std::milli>(6000));
	screen_capture->pause();
	stop_recording = true;
	// stop
	encode_func(NULL, NULL);
	// free
	av_parser_close(mv_parser);
	avcodec_free_context(&mv_context);
	av_frame_free(&mv_frame);
	av_packet_free(&mv_packet);
	sws_freeContext(mv_scaler);
}

int main()
{
	std::cout << "15 FPS\n";
	session(15, "capture_15fps.mp4");
}