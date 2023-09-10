/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * egl_preview.cpp - X/EGL-based preview window.
 */

#include <map>
#include <string>

// Include libcamera stuff before X11, as X11 #defines both Status and None
// which upsets the libcamera headers.

#include "core/options.hpp"

#include "preview.hpp"

#include <libdrm/drm_fourcc.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
// We don't use Status below, so we could consider #undefining it here.
// We do use None, so if we had to #undefine it we could replace it by zero
// in what follows below.

#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <iostream>
#include <thread>
class EglPreview : public Preview
{
public:
	EglPreview(Options const *options);
	~EglPreview();
	virtual void SetInfoText(const std::string &text) override;
	// Display the buffer. You get given the fd back in the BufferDoneCallback
	// once its available for re-use.
	virtual void Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const &info) override;
	// Reset the preview window, clearing the current buffers and being ready to
	// show new ones.
	virtual void Reset() override;
	// Check if the window manager has closed the preview.
	virtual bool Quit() override;
	// Return the maximum image size allowed.
	virtual void MaxImageSize(unsigned int &w, unsigned int &h) const override
	{
		w = max_image_width_;
		h = max_image_height_;
	}

private:
	struct Buffer
	{
		Buffer() : fd(-1) {}
		int fd;
		size_t size;
		StreamInfo info;
		GLuint texture;
	};
	void makeWindow(char const *name);
	void makeBuffer(int fd, size_t size, StreamInfo const &info, Buffer &buffer);
	::Display *display_;
	EGLDisplay egl_display_;
	Window window_;
	EGLContext egl_context_;
	EGLSurface egl_surface_;
	std::map<int, Buffer> buffers_; // map the DMABUF's fd to the Buffer
	int last_fd_;
	bool first_time_;
	Atom wm_delete_window_;
	// size of preview window
	int x_;
	int y_;
	int width_;
	int height_;
	unsigned int max_image_width_;
	unsigned int max_image_height_;
	GLint prog;
	GLint progShrink;
	GLint progGraph;
	GLuint renderFramebufferName[1];
	GLuint renderedTexture[1];
	uint32_t *shrunk;
	GLubyte * graphData;
	GLubyte* pixels;
};

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
		/* Some drivers return a size of 1 for an empty log.  This is the size
		 * of a log that contains only a terminating NUL character.
		 */
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
static GLint gl_setupGraph(int width, int height, int window_width, int window_height)
{
	float w_factor = width / (float)window_width;
	float h_factor = height / (float)window_height;
	float max_dimension = std::max(w_factor, h_factor);
	w_factor /= max_dimension;
	h_factor /= max_dimension;
	char vs[256];
	std::cout << "GL setup \n";
	snprintf(vs, sizeof(vs),
			 "attribute vec4 pos;\n"
			 "varying vec2 texcoord;\n"
			 "\n"
			 "void main() {\n"
			 "  gl_Position = pos;\n"
			 "  texcoord.x = pos.x;\n"
			 "  texcoord.y = pos.y;\n"
			 "}\n"
			 );
	vs[sizeof(vs) - 1] = 0;
       	//+ 0.00390625*p.y;\n"
	//"float x=float(texcoord.x)*0.5*0.25 + 0.5*0.25;\n"
	GLint vs_s = compile_shader(GL_VERTEX_SHADER, vs);
	char fs[1024];

	/*
					 "	if( y < x){\n"
					 "		gl_FragColor = vec4(1.0,0.0,0.0,1.0);\n"
					 "	}else if( y-1.0 < x){\n"
					 "		if( y < 0.5){\n"
					 "			gl_FragColor = vec4(1.0,1.0,0.0,1.0);\n"
					 "		} else if( y > 0.9){\n"
					 "			gl_FragColor = vec4(1.0,1.0,1.0,1.0);\n"
					 "		}else{\n"
					 "			gl_FragColor = vec4(0.0,0.0,1.0,1.0);\n"
					 "		}\n"
					 "	}else{\n"
					 "		gl_FragColor = vec4(0.0,0.0,0.0,0.0);\n"
					 "	}\n"

	 
*/
	/*
	 * x and y varying from 0 to 1 
	 */
	snprintf(fs, sizeof(fs),
			 		 "precision mediump float;\n"
					 "uniform sampler2D s;\n"
					 "uniform float scale;\n"
					 "varying vec2 texcoord;\n"
					 "void main() {\n"
					 "	float x = float(texcoord.x)*0.5 + 0.5;\n"
					 "	vec4 p = texture2D(s, vec2(x,0));\n"
					 "	float v = p.x;\n"
					 "	vec4 pl = texture2D(s, vec2(x-%f,0));\n"
					 "	float vl = pl.x;\n"
					 "	vec4 pr = texture2D(s, vec2(x+%f,0));\n"
					 "	float vr = pr.x;\n"
					 "	float y = texcoord.y * 0.5 + 0.5;\n"
					 "	if( y < v && y<vl && y<vr){\n"
					 "	  	gl_FragColor = vec4(1,0,1,1);\n"
					 "	}else{\n"
					 "	 	if(y>v && y>vl && y>vr){\n"
					 "			gl_FragColor = vec4(0,0,0,1);\n"
					 "		}else if(y>vl && y>vr){\n"
					 "			gl_FragColor = vec4(0.5,0.0,0.5,1);\n"
					 "		}else {\n"
					 "			gl_FragColor = vec4(0.25,0.0,0.25,1);\n"
					 "		}\n"
					 "	}\n"
					 "}\n",
					 1.0/width,
					 1.0/width
		);
	fs[sizeof(fs) - 1] = 0;
	std::cout << "compile graph shader\n"; 
	GLint fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
	std::cout << "link graph shader\n"; 
	GLint prog = link_program(vs_s, fs_s);

	glUseProgram(prog);

	//static const float verts[] = { -w_factor, -h_factor, w_factor, -h_factor, w_factor, h_factor, -w_factor, h_factor };
	return prog;
}

static void setupRenderFrameBuffer(GLint prog, int width, GLuint *renderFramebufferName, GLuint *renderedTexture){

	glUseProgram(prog);
	//glGenFramebuffers(1, renderFramebufferName);
	//glBindFramebuffer(GL_FRAMEBUFFER, *renderFramebufferName);
	//glGenTextures(1, renderedTexture);
//	glDisable(GL_DEPTH_TEST);
//	glGenFramebuffers(1, renderFramebufferName);
//	glGenTextures(1, renderedTexture);
//	glBindTexture(GL_TEXTURE_2D, renderedTexture[0]);
//	glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB32F, 1024, 64, 0,GL_RGB, GL_FLOAT, 0);
//	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	//glTexImage2D(GL_TEXTURE_2D, 0, GLES20.GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT, null);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//	glBindFramebuffer(GL_FRAMEBUFFER, renderFramebufferName[0]);
//	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderedTexture[0], 0);
//	glBindTexture(GL_TEXTURE_2D, 0);
//	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// "Bind" the newly created texture : all future texture functions will modify this texture
	//glBindTexture(GL_TEXTURE_2D, *renderedTexture);
	glActiveTexture(GL_TEXTURE1);
	// Give an empty image to OpenGL ( the last "0" )
	glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, width, 2, 1,GL_RGB, GL_UNSIGNED_BYTE, 0);

	// Poor filtering. Needed !
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// Set "renderedTexture" as our colour attachement #0
	//glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *renderedTexture, 0);

	// Set the list of draw buffers.
	//GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
	//glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers
	// Always check that our framebuffer is ok
//	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
//		std::cout << "FrameBuffer issue\n";
//	std::cout << "RenderFrameBufferName First = " << renderFramebufferName << "\n";
}


static GLint gl_setup(int width, int height, int window_width, int window_height)
{
	float w_factor = width / (float)window_width;
	float h_factor = height / ( (float)window_height/2 );
	float max_dimension = std::max(w_factor, h_factor);
	w_factor /= max_dimension;
	h_factor /= max_dimension;
	char vs[256];
	std::cout << "GL setup \n";
	snprintf(vs, sizeof(vs),
			 "attribute vec4 pos;\n"
			 "varying vec2 texcoord;\n"
			 "\n"
			 "void main() {\n"
			 "  gl_Position = pos;\n"
			 "  texcoord.x = pos.x * %f + 0.5;\n"
			 "  texcoord.y = 0.5 - pos.y * %f;\n"
			 "}\n",
			 //1.0 / (2.0 * w_factor), 1.0 /( 2.0 * h_factor));
			 0.5 / ( w_factor), 0.5 /(  h_factor));
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
	GLint prog = link_program(vs_s, fs_s);

	glUseProgram(prog);
	static const float vertsVid[] = { -w_factor, -h_factor, w_factor, -h_factor, w_factor, h_factor, -w_factor, h_factor,
	//static const float vertsShrink[] = { 
		-1.0, -1.0,  1.0, -1.0,  1.0, 1.0,  -1, 1.0,// };
//	static const float vertsGraph[] = { 
		-1, -0.8, 1, -0.8, 1, 1, -1, 1 };

	//static const float verts[] = { -w_factor, -h_factor, w_factor, -h_factor, w_factor, h_factor, -w_factor, h_factor };
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertsVid);
	return prog;
}

EglPreview::EglPreview(Options const *options) : Preview(options), last_fd_(-1), first_time_(true)
{
	display_ = XOpenDisplay(NULL);
	if (!display_)
		throw std::runtime_error("Couldn't open X display");

	egl_display_ = eglGetDisplay(display_);
	if (!egl_display_)
		throw std::runtime_error("eglGetDisplay() failed");

	EGLint egl_major, egl_minor;

	if (!eglInitialize(egl_display_, &egl_major, &egl_minor))
		throw std::runtime_error("eglInitialize() failed");

	x_ = options_->preview_x;
	y_ = options_->preview_y;
	width_ = options_->preview_width;
	height_ = options_->preview_height;
	makeWindow("libcamera-app");

	// gl_setup() has to happen later, once we're sure we're in the display thread.
}

EglPreview::~EglPreview()
{
}

static void no_border(Display *display, Window window)
{
	static const unsigned MWM_HINTS_DECORATIONS = (1 << 1);
	static const int PROP_MOTIF_WM_HINTS_ELEMENTS = 5;

	typedef struct
	{
		unsigned long flags;
		unsigned long functions;
		unsigned long decorations;
		long inputMode;
		unsigned long status;
	} PropMotifWmHints;

	PropMotifWmHints motif_hints;
	Atom prop, proptype;
	unsigned long flags = 0;

	/* setup the property */
	motif_hints.flags = MWM_HINTS_DECORATIONS;
	motif_hints.decorations = flags;

	/* get the atom for the property */
	prop = XInternAtom(display, "_MOTIF_WM_HINTS", True);
	if (!prop)
	{
		/* something went wrong! */
		return;
	}

	/* not sure this is correct, seems to work, XA_WM_HINTS didn't work */
	proptype = prop;

	XChangeProperty(display, window, /* display, window */
					prop, proptype, /* property, type */
					32, /* format: 32-bit datums */
					PropModeReplace, /* mode */
					(unsigned char *)&motif_hints, /* data */
					PROP_MOTIF_WM_HINTS_ELEMENTS /* nelements */
	);
}

void EglPreview::makeWindow(char const *name)
{
	int screen_num = DefaultScreen(display_);
	XSetWindowAttributes attr;
	unsigned long mask;
	Window root = RootWindow(display_, screen_num);
	int screen_width = DisplayWidth(display_, screen_num);
	int screen_height = DisplayHeight(display_, screen_num);
	std::cout <<"makeWindow\n";
	// Default behaviour here is to use a 1024x768 window.
	if (width_ == 0 || height_ == 0)
	{
		width_ = 1920;
		height_ = 1080;
	}

	if (options_->fullscreen || x_ + width_ > screen_width || y_ + height_ > screen_height)
	{
		x_ = y_ = 0;
		width_ = DisplayWidth(display_, screen_num);
		height_ = DisplayHeight(display_, screen_num);
	}

	static const EGLint attribs[] =
		{
			EGL_RED_SIZE, 1,
			EGL_GREEN_SIZE, 1,
			EGL_BLUE_SIZE, 1,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE
		};
	EGLConfig config;
	EGLint num_configs;
	if (!eglChooseConfig(egl_display_, attribs, &config, 1, &num_configs))
		throw std::runtime_error("couldn't get an EGL visual config");

	EGLint vid;
	if (!eglGetConfigAttrib(egl_display_, config, EGL_NATIVE_VISUAL_ID, &vid))
		throw std::runtime_error("eglGetConfigAttrib() failed\n");

	XVisualInfo visTemplate = {};
	visTemplate.visualid = (VisualID)vid;
	int num_visuals;
	XVisualInfo *visinfo = XGetVisualInfo(display_, VisualIDMask, &visTemplate, &num_visuals);

	/* window attributes */
	attr.background_pixel = 0;
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap(display_, root, visinfo->visual, AllocNone);
	attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
	/* XXX this is a bad way to get a borderless window! */
	mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

	window_ = XCreateWindow(display_, root, x_, y_, width_, height_, 0, visinfo->depth, InputOutput, visinfo->visual,
							mask, &attr);

	if (options_->fullscreen)
		no_border(display_, window_);

	/* set hints and properties */
	{
		XSizeHints sizehints;
		sizehints.x = x_;
		sizehints.y = y_;
		sizehints.width = width_;
		sizehints.height = height_;
		sizehints.flags = USSize | USPosition;
		XSetNormalHints(display_, window_, &sizehints);
		XSetStandardProperties(display_, window_, name, name, None, (char **)NULL, 0, &sizehints);
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	static const EGLint ctx_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, ctx_attribs);
	if (!egl_context_)
		throw std::runtime_error("eglCreateContext failed");

	XFree(visinfo);

	XMapWindow(display_, window_);

	// This stops the window manager from closing the window, so we get an event instead.
	wm_delete_window_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display_, window_, &wm_delete_window_, 1);

	egl_surface_ = eglCreateWindowSurface(egl_display_, config, reinterpret_cast<EGLNativeWindowType>(window_), NULL);
	if (!egl_surface_)
		throw std::runtime_error("eglCreateWindowSurface failed");

	// We have to do eglMakeCurrent in the thread where it will run, but we must do it
	// here temporarily so as to get the maximum texture size.
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context_);
	int max_texture_size = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
	max_image_width_ = max_image_height_ = max_texture_size;
	// This "undoes" the previous eglMakeCurrent.
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

static void get_colour_space_info(std::optional<libcamera::ColorSpace> const &cs, EGLint &encoding, EGLint &range)
{
	encoding = EGL_ITU_REC601_EXT;
	range = EGL_YUV_NARROW_RANGE_EXT;

	if (cs == libcamera::ColorSpace::Sycc)
		range = EGL_YUV_FULL_RANGE_EXT;
	else if (cs == libcamera::ColorSpace::Smpte170m)
		/* all good */;
	else if (cs == libcamera::ColorSpace::Rec709)
		encoding = EGL_ITU_REC709_EXT;
	else
		LOG(1, "EglPreview: unexpected colour space " << libcamera::ColorSpace::toString(cs));
}

void EglPreview::makeBuffer(int fd, size_t size, StreamInfo const &info, Buffer &buffer)
{
	if (first_time_)
	{
		// This stuff has to be delayed until we know we're in the thread doing the display.
		if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_))
			throw std::runtime_error("eglMakeCurrent failed");
//		progShrink=gl_setupShrinkData(info.width, info.height, width_, height_);
		std::cout <<"makeBuffer\n";
		progGraph=gl_setupGraph(info.width, info.height, width_, height_);
		prog=gl_setup(info.width, info.height, width_, height_);
		shrunk = new uint32_t[info.width+2];
       		pixels = new GLubyte[info.width * info.height * 4+20];
//	first_time_ = false;
	}

	buffer.fd = fd;
	buffer.size = size;
	buffer.info = info;

	EGLint encoding, range;
	get_colour_space_info(info.colour_space, encoding, range);

	EGLint attribs[] = {
		EGL_WIDTH, static_cast<EGLint>(info.width),
		EGL_HEIGHT, static_cast<EGLint>(info.height),
		EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_YUV420,
		EGL_DMA_BUF_PLANE0_FD_EXT, fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(info.stride),
		EGL_DMA_BUF_PLANE1_FD_EXT, fd,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, static_cast<EGLint>(info.stride * info.height),
		EGL_DMA_BUF_PLANE1_PITCH_EXT, static_cast<EGLint>(info.stride / 2),
		EGL_DMA_BUF_PLANE2_FD_EXT, fd,
		EGL_DMA_BUF_PLANE2_OFFSET_EXT, static_cast<EGLint>(info.stride * info.height + (info.stride / 2) * (info.height / 2)),
		EGL_DMA_BUF_PLANE2_PITCH_EXT, static_cast<EGLint>(info.stride / 2),
		EGL_YUV_COLOR_SPACE_HINT_EXT, encoding,
		EGL_SAMPLE_RANGE_HINT_EXT, range,
		EGL_NONE
	};

	EGLImage image = eglCreateImageKHR(egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
	if (!image)
		throw std::runtime_error("failed to import fd " + std::to_string(fd));

	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &buffer.texture);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, buffer.texture);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);

	eglDestroyImageKHR(egl_display_, image);
}

void EglPreview::SetInfoText(const std::string &text)
{
	if (!text.empty())
		XStoreName(display_, window_, text.c_str());
}

#define IMAGE_WIDTH 1920


void Shrink(GLubyte * data, uint16_t x0, uint16_t x1, uint16_t width, uint16_t height, uint16_t stride, uint32_t * output, uint32_t * maxVal, float slope){
	*maxVal = 0;
	for(uint16_t x=x0; x<x1; x++){
		output[x]=0;
		for(uint16_t y=0; y<height; y++){
			int x2 = x+slope * y;
			if (x2<0) x2=0;
			if (x2>width-1) x2=width-1;
//			std::cout << "y"<< y<<" h"<<height<<"s"<< (y*stride+x2) << "\n";
			output[x] += data[(y*stride+x2)]*3
				+ data[(int)((y/4+height)*stride+(x2)/4)]*0.781
			       	+ data[(int)((y/4+1.25*height)*stride+(x2)/4)]*1.63;
//			output[x] += data[4*(y*width+x+x2) + 1];
//			output[x] += data[4*(y*width+x+x2) + 2];
		}
		//output[x]/=height;
		if(output[x]>*maxVal){
			*maxVal=output[x];
		}
	}
	
}

void EglPreview::Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const &info)
{
	float w_factor = info.width / (float)width_;
	float h_factor = info.height / (float)height_;
	float max_dimension = std::max(w_factor, h_factor);
	w_factor /= max_dimension;
	h_factor /= max_dimension;

	std::cout << "info.width=" << info.width << " info.height=" << info.height << " info.stride="<< info.stride << " width_=" << width_ << "height_=" << height_ << "\n";
//	static const float vertsVid[] = { -w_factor, -h_factor, w_factor, -h_factor, w_factor, h_factor, -w_factor, h_factor };
//	static const float vertsShrink[] = { -1.0, -1.0,  1.0, -1.0,  1.0, 1.0,  -1, 1.0 };
//	static const float vertsGraph[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
	Buffer &buffer = buffers_[fd];
	if (buffer.fd == -1){
		makeBuffer(fd, span.size(), info, buffer);
		if(first_time_){
		setupRenderFrameBuffer(progGraph, info.width, &renderFramebufferName[0], &renderedTexture[0]);
		graphData = new GLubyte[info.width*4*8+1];
		first_time_=false;
		}
	}
	//std::cout << "width=" << width_ << " height=" << height_ << "\n";
	glClearColor(0, 0, 0, 0);
	// ***********************
	// Display normal video
	// ***********************
	glUseProgram(prog);
	glActiveTexture(GL_TEXTURE0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glClear(GL_COLOR_BUFFER_BIT);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, buffer.texture);
	glViewport(0,height_/2,width_,height_/2);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	//std::cout << "RenderFrameBufferName = " << renderFramebufferName << "\n";
	uint32_t max1, max2, max3, max4;
	//libcamera::Span<uint8_t> buffer = app.Mmap(buffers_[fd])[0];
        pixels = (GLubyte *)span.data();

//	glReadPixels(0, 0, info.width, info.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
//	Shrink(pixels, 0, info.width/4, info.width, info.height, shrunk, &max1,0 );
	std::thread t1(&Shrink, pixels, 0, info.width/4, info.width, info.height, info.stride, shrunk, &max1,0 );
	std::thread t2(&Shrink, pixels, info.width/4, info.width/2, info.width, info.height, info.stride, shrunk, &max2,0 );
	std::thread t3(&Shrink, pixels, info.width/2, 3*info.width/4, info.width, info.height, info.stride, shrunk, &max3,0 );
	std::thread t4(&Shrink, pixels, 3*info.width/4, info.width, info.width, info.height, info.stride, shrunk, &max4,0 );
	t1.join();
	t2.join();
	t3.join();
	t4.join();
	if(max2>max1) max1=max2;
	if(max3>max1) max1=max3;
	if(max4>max1) max1=max4;

//	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void*)(0));
	float scale=(256.0*256-1)/max1;
	// map pixel buffer
	for(uint16_t i=0; i<info.width*4*8; i+=4){
		uint16_t value = scale * shrunk[i/8];
		graphData[i] =   value / 256;
		graphData[i+1] =  value % 256; 
		graphData[i+2] =  0;//value/256;//value % 256; 
		graphData[i+3] =  0;//value/256;//value % 256; 
	}	
	
	// ************************
	// Draw Graph
	// ************************
	glUseProgram(progGraph);
//	glEnableVertexAttribArray(1);
//	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, vertsGraph);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "FrameBuffer Graph issue\n";
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D,renderedTexture[0]);
	glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA, info.width, 1, 0,GL_RGBA, GL_UNSIGNED_BYTE, graphData);
	glUniform1i(glGetUniformLocation(progGraph,"s"), 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// Give an empty image to OpenGL ( the last "0" )
	//glUniform1f( glGetUniformLocation(progGraph, "scale"), 1.0);
	glViewport( (1.0-w_factor/2)/2*width_,0,width_*w_factor/2,height_/2);
	//glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_FAN, 8, 4);
//	GLuint vbo;
//	glGenBuffers(1, &vbo);
//	glBindBuffer(GL_ARRAY_BUFFER, vbo);
//	glBufferData(GL_ARRAY_BUFFER, sizeof(output[0])*info.width, output, GL_DYNAMIC_DRAW);
//	glDrawElements(GL_LINE_LOOP, info.width*2+4, GL_UNSIGNED_BYTE, 0);
//	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);       // Vertex attributes stay the same
  //  	glEnableVertexAttribArray(0);

	EGLBoolean success [[maybe_unused]] = eglSwapBuffers(egl_display_, egl_surface_);
	if (last_fd_ >= 0)
		done_callback_(last_fd_);
	last_fd_ = fd;


}

void EglPreview::Reset()
{
	for (auto &it : buffers_)
		glDeleteTextures(1, &it.second.texture);
	buffers_.clear();
	last_fd_ = -1;
	eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	first_time_ = true;
}

bool EglPreview::Quit()
{
	XEvent event;
	while (XCheckTypedWindowEvent(display_, window_, ClientMessage, &event))
	{
		if (static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_window_)
			return true;
	}
	return false;
}

Preview *make_egl_preview(Options const *options)
{
	return new EglPreview(options);
}
