/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * libcamera_hello.cpp - libcamera "hello world" app.
 */

#include <chrono>

#include "core/libcamera_app.hpp"
#include "core/options.hpp"
#define IMAGE_WIDTH 1920
using namespace std::placeholders;

int renderDownData(CompletedRequestPtr *completed_request, float slope);
// The main event loop for the application.
LibcameraApp app;

static void event_loop(LibcameraApp &app)
{
	Options const *options = app.GetOptions();

	app.OpenCamera();
	app.ConfigureViewfinder();
	app.StartCamera();

	auto start_time = std::chrono::high_resolution_clock::now();

	for (unsigned int count = 0; ; count++)
	{
		LibcameraApp::Msg msg = app.Wait();
		if (msg.type == LibcameraApp::MsgType::Timeout)
		{
			LOG_ERROR("ERROR: Device timeout detected, attempting a restart!!!");
			app.StopCamera();
			app.StartCamera();
			continue;
		}
		if (msg.type == LibcameraApp::MsgType::Quit)
			return;
		else if (msg.type != LibcameraApp::MsgType::RequestComplete)
			throw std::runtime_error("unrecognised message!");

		LOG(2, "Viewfinder frame " << count);
		auto now = std::chrono::high_resolution_clock::now();
		if (options->timeout && now - start_time > std::chrono::milliseconds(options->timeout))
			return;

		CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);
		renderDownData(&completed_request, 0);

		app.ShowPreview(completed_request, app.ViewfinderStream());
	}
}


int renderDownData(CompletedRequestPtr *completed_request, float slope=0){
/*	libcamera::Stream *stream = app.GetMainStream();
	StreamInfo info = app.GetStreamInfo(stream);
	libcamera::Span<uint8_t> buffer = app.Mmap(completed_request->buffers[stream])[0];
	uint32_t *ptr = (uint32_t *)buffer.data();
	float output[IMAGE_WIDTH];
	const float yFac = 1.0;
	const float uFac = 1.0;
	const float vFac = 1.0;

	for(uint16_t x =0; x<info.width; x++){
		output[x]=0;
		for(uint16_t y=0; y<info.height;y++){
			int16_t x2 = x + slope*y;
			if(x2>=0 && x2<(uint16_t)info.width){
				uint32_t total = info.width * info.height;
				uint16_t y = ptr[y * info.width + x2];
				uint16_t u = ptr[( y / 2) * (info.width / 2) + (x / 2) + total];
				uint16_t v = ptr[( y / 2) * (info.width / 2) + (x / 2) + total + (total / 4)];
				output[x] += y*yFac + u*uFac + v*vFac;
			}
		}
		std::cout << "red x=" << x << " = " << output[x];
	}
*/	return 1;
}

int main(int argc, char *argv[])
{
	try
	{
		Options *options = app.GetOptions();
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
