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
#include <fstream>
#include <string>
#include <pigpio.h>
#include "../preview/preview.hpp"

#define IMAGE_WIDTH 1920
#define PIN_SWITCH 21
#define DEBOUNCE 100
#define BETWEEN_PRESSES 1000

#include <chrono>
int lastSwitchState=1;
auto lastSwitchTime= std::chrono::system_clock::now();//duration_cast < milliseconds> ( chrono.system_clock::now().time_since_epoch() );
int numPresses = 0;

// settings
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
//static GLint compile_shader(GLenum target, const char *source);
//static GLint link_program(GLint vs, GLint fs);
static void gl_setup(int width, int height, int window_width, int window_height);
GLint prog ;
/*EGLDisplay eglDisp;

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
*/
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


void freezeGraph(){
	std::cout<< "freezeGraph()\n";
	doShadow=true;
	shadowTime = std::chrono::system_clock::now();
}

void calibrateMercury(){
	doMercury=true;
}

void calibrateIncandescent(){

}

void calibrateSlope(){
	doSlope=true;
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

      /*		libcamera::Stream *stream = app.GetMainStream();
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

*/

		app.EncodeBuffer(completed_request, app.VideoStream());
		app.ShowPreview(completed_request, app.VideoStream());
		int switchState = gpioRead(PIN_SWITCH);
		auto timeNow = std::chrono::system_clock::now();//duration_cast < milliseconds> ( chrono.system_clock::now().time_since_epoch() );
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(timeNow-lastSwitchTime);
		if(switchState != lastSwitchState && switchState==0){
			std::cout << "S";
	                std::cout << switchState <<"_";
			std::cout << duration.count() << " " << BETWEEN_PRESSES<< " \n";
			if(duration < std::chrono::milliseconds(DEBOUNCE)){
				std::cout << "DEBOUNCE";
			}else if(switchState ==0 && numPresses==0){
				numPresses++;
				std::cout << "press_"<< numPresses<< " ";
				freezeGraph();
				lastSwitchTime = timeNow;
			}else if(switchState ==0 && duration < std::chrono::milliseconds(BETWEEN_PRESSES)){
				numPresses++;
				std::cout << "press_"<< numPresses<< " ";
				lastSwitchTime = timeNow;
			}else{
				lastSwitchTime = timeNow;
				std::cout<< "Else";
			}
		}else if(switchState==1 && (duration > std::chrono::milliseconds(BETWEEN_PRESSES) && numPresses>0)){
			std::cout << "Pressed_"<< numPresses;
			switch(numPresses){
				case 3:
					calibrateMercury();
					break;
				case 4:
					calibrateIncandescent();
				break;
				case 5:
					calibrateSlope();
					break;
			}
			numPresses =0;
			lastSwitchTime = timeNow;
		}else{

		}
		lastSwitchState = switchState;
		if(!prog){
			gl_setup(1920,1080,1920,1080);
		}
	}
}
/*
static GLint compile_shader(GLenum target, const char *source)
{
        GLuint s = glCreateShader(target);
        glShaderSource(s, 1, (const GLchar **)&source, NULL);
        glCompileShader(s);

        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

        if (!ok)
        {
                GLchar *info;
                GLint size;

                glGetShaderiv(s, GL_INFO_LOG_LENGTH, &size);
                info = (GLchar *)malloc(size);

                glGetShaderInfoLog(s, size, NULL, info);
                throw std::runtime_error("failed to compile shader: " + std::string(info) + "\nsource:\n" +
                                                                 std::string(source));
        }

        return s;
}
static GLint link_program(GLint vs, GLint fs)
{
        GLint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);

        GLint ok;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
                // Some drivers return a size of 1 for an empty log.  This is the size
                 // of a log that contains only a terminating NUL character.
                 //
                GLint size;
                GLchar *info = NULL;
                glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
                if (size > 1)
                {
                        info = (GLchar *)malloc(size);
                        glGetProgramInfoLog(prog, size, NULL, info);
                }

                throw std::runtime_error("failed to link: " + std::string(info ? info : "<empty log>"));
        }

        return prog;
}
*/
static void gl_setup(int width, int height, int window_width, int window_height)
{
/*        float w_factor = width / (float)window_width;
        float h_factor = (height-100) / (float)window_height;
        float max_dimension = std::max(w_factor, h_factor);
        w_factor /= max_dimension;
        h_factor /= max_dimension;
        char vs[256];
        snprintf(vs, sizeof(vs),
                         "attribute vec4 pos;\n"
                         "varying vec2 texcoord;\n"
                         "\n"
                         "void main() {\n"
                         "  gl_Position = pos;\n"
                         "  texcoord.x = pos.x / %f + 0.5;\n"
                         "  texcoord.y = 0.5 - pos.y / %f;\n"
                         "}\n",
                         2.0 * w_factor, 2.0 * h_factor);
        vs[sizeof(vs) - 1] = 0;
        GLint vs_s = compile_shader(GL_VERTEX_SHADER, vs);
        const char *fs = "#extension GL_OES_EGL_image_external : enable\n"
                                         "precision mediump float;\n"
                                         "uniform samplerExternalOES s;\n"
                                         "varying vec2 texcoord;\n"
                                         "void main() {\n"
                                         "  gl_FragColor = texture2D(s, texcoord);\n"
                                         "}\n";
        GLint fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
        prog = link_program(vs_s, fs_s);

        glUseProgram(prog);

        static const float verts[] = { -w_factor, -h_factor, w_factor, -h_factor, w_factor, 0, -w_factor, 0 };
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
        glEnableVertexAttribArray(0);

*/	
}





int main(int argc, char *argv[])
{

	if(gpioInitialise()<0){
		std::cout<< "gpioInitialise() failure ";
		exit(0);
	}
	gpioSetMode(PIN_SWITCH, PI_INPUT);
	gpioSetPullUpDown(PIN_SWITCH, PI_PUD_UP);


	try
	{
		LibcameraEncoder app;
		VideoOptions *options = app.GetOptions();
		//eglDisp = get_egl_display_or_skip();
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
