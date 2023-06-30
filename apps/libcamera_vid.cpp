/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * libcamera_vid.cpp - libcamera video record app.
 */

#include <chrono>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/stat.h>

#include "core/libcamera_encoder.hpp"
#include "output/output.hpp"

#include <math.h>
#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include <err.h>
#include <X11/Xlib.h>


#define IMAGE_WIDTH 1920
// settings
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

EGLDisplay get_egl_display_or_skip(void);

EGLDisplay get_egl_display_or_skip(void)
{
    Display *dpy = XOpenDisplay(NULL);
    EGLint major, minor;
    EGLDisplay edpy;
    bool ok;

    if (!dpy)
        errx(77, "couldn't open display\n");

    edpy = eglGetDisplay(dpy);
    if (!edpy)
        errx(1, "Couldn't get EGL display for X11 Display.\n");

    ok = eglInitialize(edpy, &major, &minor);
    if (!ok)
        errx(1, "eglInitialize() failed\n");

    return edpy;
}

using namespace std::placeholders;

// Some keypress/signal handling.

static int signal_received;
static void default_signal_handler(int signal_number)
{
	signal_received = signal_number;
	LOG(1, "Received signal " << signal_number);
}

static int get_key_or_signal(VideoOptions const *options, pollfd p[1])
{
	int key = 0;
	if (signal_received == SIGINT)
		return 'x';
	if (options->keypress)
	{
		poll(p, 1, 0);
		if (p[0].revents & POLLIN)
		{
			char *user_string = nullptr;
			size_t len;
			[[maybe_unused]] size_t r = getline(&user_string, &len, stdin);
			key = user_string[0];
		}
	}
	if (options->signal)
	{
		if (signal_received == SIGUSR1)
			key = '\n';
		else if (signal_received == SIGUSR2)
			key = 'x';
		signal_received = 0;
	}
	return key;
}

static int get_colourspace_flags(std::string const &codec)
{
	if (codec == "mjpeg" || codec == "yuv420")
		return LibcameraEncoder::FLAG_VIDEO_JPEG_COLOURSPACE;
	else
		return LibcameraEncoder::FLAG_VIDEO_NONE;
}



// The main even loop for the application.

static void event_loop(LibcameraEncoder &app)
{
	VideoOptions const *options = app.GetOptions();
	std::unique_ptr<Output> output = std::unique_ptr<Output>(Output::Create(options));
	app.SetEncodeOutputReadyCallback(std::bind(&Output::OutputReady, output.get(), _1, _2, _3, _4));
	app.SetMetadataReadyCallback(std::bind(&Output::MetadataReady, output.get(), _1));

	app.OpenCamera();
	app.ConfigureVideo(get_colourspace_flags(options->codec));
	app.StartEncoder();
	app.StartCamera();
	auto start_time = std::chrono::high_resolution_clock::now();

	// Monitoring for keypresses and signals.
	signal(SIGUSR1, default_signal_handler);
	signal(SIGUSR2, default_signal_handler);
	signal(SIGINT, default_signal_handler);
	pollfd p[1] = { { STDIN_FILENO, POLLIN, 0 } };

	for (unsigned int count = 0; ; count++)
	{
		LibcameraEncoder::Msg msg = app.Wait();
		if (msg.type == LibcameraApp::MsgType::Timeout)
		{
			LOG_ERROR("ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			app.StartCamera();
			continue;
		}
		if (msg.type == LibcameraEncoder::MsgType::Quit)
			return;
		else if (msg.type != LibcameraEncoder::MsgType::RequestComplete)
			throw std::runtime_error("unrecognised message!");
		int key = get_key_or_signal(options, p);
		if (key == '\n')
			output->Signal();

		LOG(2, "Viewfinder frame " << count);
		auto now = std::chrono::high_resolution_clock::now();
		bool timeout = !options->frames && options->timeout &&
					   (now - start_time > std::chrono::milliseconds(options->timeout));
		bool frameout = options->frames && count >= options->frames;
		if (timeout || frameout || key == 'x' || key == 'X')
		{
			if (timeout)
				LOG(1, "Halting: reached timeout of " << options->timeout << " milliseconds.");
			app.StopCamera(); // stop complains if encoder very slow to close
			app.StopEncoder();
			return;
		}

		CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);

      		libcamera::Stream *stream = app.GetMainStream();
        	StreamInfo info = app.GetStreamInfo(stream);
        	libcamera::Span<uint8_t> buffer = app.Mmap(completed_request->buffers[stream])[0];
        	uint8_t *ptr = (uint8_t *)buffer.data();
	        float outputy[IMAGE_WIDTH];
	        float outputu[IMAGE_WIDTH];
	        float outputv[IMAGE_WIDTH];
       //// 	const float yFac = 1.0;
        ///	const float uFac = 0;
        //	const float vFac = 0;
		float slope = 0;
	//	uint16_t cy,cu,cv;
                uint32_t total = info.width * info.height;
//		std::cout << "set buffer\n";
        	for(uint16_t x =0; x<info.width; x++){
//			std::cout << "x="<<x<<"\n";
                	outputy[x]=0;
                	outputu[x]=0;
                	outputv[x]=0;
                	for(uint16_t y=0; y<info.height;y++){
                        	int16_t x2 = x + slope*y;
//				std::cout << "y="<<y<<" x2="<<x2<<"\n";
                        	if(x2>=0 && x2<(uint16_t)info.width){
                                	outputy[x] = ptr[y * info.width + x2];
                                	outputu[x] = ptr[( y / 2) * (info.width / 2) + (x / 2) + total];
                                	outputv[x] = ptr[( y / 2) * (info.width / 2) + (x / 2) + total + (total / 4)];
                              //  	output[x] += cy*yFac + cu*uFac + cv*vFac;
                        	}
                	}
        	}
		int x = 0;
                std::cout << "red x=" << x << " = " << outputy[x]/info.width << ","<< outputu[x]/info.width<<","<<outputv[x]/info.width<<"\n";


		app.EncodeBuffer(completed_request, app.VideoStream());
		app.ShowPreview(completed_request, app.VideoStream());
	}
}

int main(int argc, char *argv[])
{





	try
	{
		LibcameraEncoder app;
		VideoOptions *options = app.GetOptions();
		if (options->Parse(argc, argv))
		{
			if (options->verbose >= 2)
				options->Print();

			event_loop(app);
		}
	}
	catch (std::exception const &e)
	{
		LOG_ERROR("ERROR: *** " << e.what() << " ***");
		return -1;
	}
	return 0;
}
