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
#include <math.h>
#include <gsl/gsl_fit.h>
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <iostream>
#include <thread>
#define STB_IMAGE_IMPLEMENTATION
#define MAX_PEAKS 20
#include <stb/stb_image.h>
#include <../find-peaks/PeakFinder.h>

#define R_PROP 1.0
#define G_PROP 1.0
#define B_PROP 1.0

#define RY 1.0
#define RU 0.0
#define RV 1.4075

#define GY 1.0
#define GU -0.3455
#define GV -0.7169

#define BY 1.0
#define BU 1.779
#define BV 0.0


#define Y_FAC (BY*B_PROP + RY*R_PROP + GY*G_PROP)
#define U_FAC (BU*B_PROP + RU*R_PROP + GU*G_PROP) 
#define V_FAC (BV*B_PROP + RV*R_PROP + GV*G_PROP)


bool doMercury = false;
bool doIncandescent = false;
bool doDark = false;
bool doShadow = true;
bool doSlope = false;
bool doSave = false;
char slopeFileName[] = "calSlope.txt";
char incandescentFileName[] = "calIncandescent.txt";
char darkFileName[] = "calDark.txt";
char wavelengthFileName[] = "calWavelength.txt";
std::chrono::time_point <std::chrono::system_clock>shadowTime;
int labelPositions[]={ 100,250,400,550,700,850,1000,1150};
float labelValues[]={ 300,400,500,600,700,800,900,1000};
int numLabels = 8;
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
	uint32_t ShrinkData(GLubyte *pixels, StreamInfo const *info, uint32_t *shrunk,float *slope );
      //  void findPeaks(uint16_t *data, uint16_t width, uint16_t *peaks);
        void parsePeaks(uint32_t *data, uint16_t width);
        void incandescentCal(uint32_t *shrunk, uint16_t width);
        void darkCal(uint32_t *shrunk, uint16_t width);
	void readCal(unsigned int width);
	void saveCal(unsigned int width);
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
	GLuint renderedTexture[3];
	uint32_t *shrunk;
	double *incandescentCalibration;
	uint32_t *darkCalibration;
	GLubyte *shadowData;
	GLubyte * graphData;
	GLubyte* pixels;
	float slope;
	GLint progText;
	GLuint textTexture;
//	float label_a;
	double label_b;
	double label_c;
	Options const * theOptions;
};

void EglPreview::readCal(unsigned int width){
	std::string line;
	unsigned int w;
	std::ifstream calfile;
	calfile.open(slopeFileName);
	if(calfile){
		std::getline(calfile, line);
		slope = std::stod(line);
		calfile.close();
		std::cout << "Loaded Slope = " << slope << "\n";
	}
	
	calfile.open(darkFileName);
	if(calfile){
		std::getline(calfile, line);
		w=std::stoi(line);
		for(unsigned int i=0;i<std::min(w,width); i++){
			std::getline(calfile, line);
			std::cout << i << " " << w << "-" << width  << "line=" << line <<"\n";
			darkCalibration[i] = std::stod(line);
		}
		calfile.close();
		std::cout << "Loaded Dark" << "\n";
	}

	calfile.open(incandescentFileName);
	if(calfile){
		std::getline(calfile, line);
		w=std::stoi(line);
		for(unsigned int i=0;i<std::min(w,width); i++){
			std::getline(calfile, line);
			incandescentCalibration[i] = std::stod(line);
		}
		calfile.close();
		std::cout << "Loaded Incandescent" << "\n";
	}

	calfile.open(wavelengthFileName);
	if(calfile){
		std::getline(calfile, line);
		label_b = std::stod(line);
		std::getline(calfile, line);
		label_c = std::stod(line);
		calfile.close();
		// put the labels in the right positions
    		for(int i=0; i<numLabels; i++){
			std::cout << "Label " << labelValues[i] << " " << ( label_c+label_b*labelValues[i] ) << "\n";
			labelPositions[i]=label_c+label_b*labelValues[i];
    		}
		std::cout << "Loaded Dark" << "\n";
	}
}

void EglPreview::saveCal(unsigned int width){
	std::cout << "Save Calibration" << "\n";
	std::ofstream calfile;
	calfile.open(slopeFileName);
	calfile << slope << "\n";
	calfile.close();
// save dark cal
	calfile.open(darkFileName);
	calfile << width << "\n";
	for(unsigned int i=0;i<width; i++){
		calfile << darkCalibration[i] << "\n";
	}
	calfile.close();
// save amplitude cal
	calfile.open(incandescentFileName);
	calfile << width << "\n";
	for(unsigned int i=0;i<width; i++){
		calfile << incandescentCalibration[i] << "\n";
	}
	calfile.close();
// save coefficients for wavelength fit
	calfile.open(wavelengthFileName);
	calfile << label_b << "\n";
	calfile << label_c << "\n";
	calfile.close();
}
/*
void EglPreview::findPeaks(uint16_t *data, uint16_t width, int *peaks, uint16_t maxPeaks){
	float smooth[]={-1.0,-2.0, 2.0, 1.0};
	int numPeaks =0;
	float lastd = 0;
	for(int x=0; x<width-7;x++){
		float d=0;
		for(int i=0;i<4;i++){
			d+=smooth[i]*data[x+i];
		}
		if(x>0 && d*lastd<0 && numPeaks<MAX_PEAKS){
			peaks[numPeaks]=x+2;
			numPeaks++;
		}
		lastd=d;
	}
}
*/
void EglPreview::parsePeaks(uint32_t *data, uint16_t width){
	std::vector<float> in(data, data + width);
	std::vector<int> out;
	std::cout << "parsePeaks"<<"\n";
	PeakFinder::findPeaks(in, out, false,1);
	std::cout << "doneFindPeaks"<<"\n";
	std::vector<int> order;
	double realPeaks[]={542.5, 610.4, 435.1, 486.7, 586.2};
	unsigned int numPeaks=0;
	std::cout << "out.size()"<< out.size()<<"\n";
	if(out.size()<3){
		return;
	}
	for(unsigned int i=0; i<out.size();i++){
		std::cout << "i=" << i<<"\n";
		std::cout << "out[i]"<< out[i]<<"\n";
		std::cout << "out[i-1]"<< out[i-1]<<"\n";
		if(i>0 && data[out[i]]!=data[out[i-1]]){
			order.push_back(i);
			std::cout << "Peak"<< out[i]<<","<< data[out[i]] <<"\n";
			numPeaks++;
		}
	}

	// sort the peaks
	std::sort(order.begin(), order.end(), [&out,&data](int i1, int i2){ return data[out[i1]]> data[out[i2]];});
	        // Try to swap any peaks obviously in the wrong order
	for(unsigned int i=0; i<numPeaks;i++){
		std::cout << "sorted Peak"<< out[order[i]]<<","<< data[out[order[i]]] <<" -> " << realPeaks[i] << "\n";
	}
        if(out.size()>1 && out[order[0]] > out[order[1]]){
                int t = order[1];
                order[1] = order[0];
                order[0] = t;
        }
        if(out.size()>3 && out[order[0]] > out[order[4]]){
                int t = order[3];
                order[3] = order[4];
                order[4] = t;
        }

	for(unsigned int i=0; i<numPeaks;i++){
		std::cout << " i=" << i << "\n";
		std::cout << "sorted Peak2 "<< out[order[i]]<<","<<" -> " << realPeaks[i] << "\n";
		labelPositions[order[i]] = out[order[i]];
	}
	int i, n;
	double screenx[5];
//  	double xi, yi, ei, chisq;
//  	gsl_matrix *X, *cov;
//  	gsl_vector *y, *w, *c;
	double cov00, cov01, cov11, sumsq;
  	n = numPeaks>5? 5: numPeaks;
	if( numPeaks < 3){
		return;
	}
	std::cout << " Pre Loop"  << "\n";
	for(i=0; i<n; i++){
		screenx[i] = out[order[i]];
	}
	std::cout << " Pre fit"  << "\n";
	gsl_fit_linear (realPeaks, 1, screenx, 1, n, &label_c, &label_b, &cov00, &cov01, &cov11, &sumsq);
	std::cout << " Post fit"  << "\n";

/*  	X = gsl_matrix_alloc (n, 3);
  	y = gsl_vector_alloc (n);
  	w = gsl_vector_alloc (n);

  	c = gsl_vector_alloc (3);
  	cov = gsl_matrix_alloc (3, 3);
  	for(i=0; i<n;i++){
		yi=out[order[i]];
      		xi=realPeaks[i];
		ei=1.0;
		gsl_matrix_set (X, i, 0, 1.0);
      		gsl_matrix_set (X, i, 1, xi);
      		gsl_matrix_set (X, i, 2, xi*xi);

      		gsl_vector_set (y, i, yi);
      		gsl_vector_set (w, i, 1.0/(ei*ei));
    	}
	gsl_multifit_linear_workspace * work = gsl_multifit_linear_alloc (n, 3);
    	gsl_multifit_wlinear (X, w, y, c, cov, &chisq, work);
    	gsl_multifit_linear_free (work);

#define C(i) (gsl_vector_get(c,(i)))
    	label_c = C(0);
    	label_b = C(1);
    	label_a = C(2);*/
    	for(i=0; i<numLabels; i++){
		std::cout << "Label " << labelValues[i] << " " << ( label_c+label_b*labelValues[i] ) << "\n";
		labelPositions[i]=label_c+label_b*labelValues[i];
    	}
    	std::cout << "Fit b=" << label_b << "x + c=" << label_c << "\n";
}


void EglPreview::incandescentCal(uint32_t *shrunk, uint16_t width){
	const double h = 6.626e-34;
	const double k = 1.38066e-23;
	const double T = 3000;
	const double c = 2.998e8;
	const double kc = c*h / (k* T);
	const double d = 2.0*h*c*c;
	float max = -1;
	const double minS = 200;
	for(unsigned int x=0; x<width;x++){
		double wl = (-label_c + x)/label_b*1e-9;
		double s = double(shrunk[x]) - darkCalibration[x];
		std::cout << wl << "_" << "_ ";
		incandescentCalibration[x] = d*pow(wl,-5)/(exp(kc/wl)-1.0);
		std::cout << incandescentCalibration[x] << "->" << s << "  ";
		incandescentCalibration[x] /= s;
		if(s < 3e-7 ){
			incandescentCalibration[x]=0;
		}
		if(s < minS ){
			incandescentCalibration[x]=0;
		}
		if(incandescentCalibration[x] > x){
			max=incandescentCalibration[x];
		}
		std::cout << incandescentCalibration[x] << "\n";
	}
	for(unsigned int x=0; x<width;x++){
		std::cout << "IncCal x="<< x << " -> " << incandescentCalibration[x] << "   ";
		incandescentCalibration[x] *= 500.0/max;
		std::cout << " -> " << incandescentCalibration[x] << "\n";
	}

}

void EglPreview::darkCal(uint32_t *shrunk, uint16_t width){
	for(unsigned int x=0; x<width;x++){
		darkCalibration[x] = shrunk[x];
	}
}

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
	 * x and y varying from 0 to 1 
	 */
	snprintf(fs, sizeof(fs),
			 		 "precision mediump float;\n"
					 "uniform sampler2D s;\n"
					 "uniform sampler2D shadow;\n"
					 "uniform float scale;\n"
					 "uniform float shadowOpacity;\n"
					 "varying vec2 texcoord;\n"
					 "void main() {\n"
					 "	float x = float(texcoord.x)*0.5 + 0.5;\n"
					 "	vec4 p = texture2D(s, vec2(x,0));\n"
					 "	float v = p.x;\n"
					 "//	vec4 pl = texture2D(s, vec2(x-%f,0));\n"
					 "//	float vl = pl.x;\n"
					 "//	vec4 pr = texture2D(s, vec2(x+%f,0));\n"
					 "//	float vr = pr.x;\n"
					 "	float y = texcoord.y * 0.5 + 0.5;\n"
					 "	if(y<p.x){\n"
 					 "//	if( y < v && y<vl && y<vr){\n"
					 "	  	gl_FragColor = vec4(y/v,0,0.0,1);\n"
					 "	}else{gl_FragColor = vec4(0.0,0.0,0.0,1.0);}\n"
					 "//	else{\n"
					 "//	 	if(y>v && y>vl && y>vr){\n"
					 "//			gl_FragColor = vec4(0,0,0.0,1);\n"
					 "//		}else if(y>vl && y>vr){\n"
					 "//			gl_FragColor = vec4(0.5,0.0,0.0,1);\n"
					 "//		}else {\n"
					 "//			gl_FragColor = vec4(0.25,0.0,0.0,1);\n"
					 "//		}\n"
					 "//	}\n"
					 "	if(texture2D(shadow,vec2(x,0)).x>y){\n"
					 "		gl_FragColor += vec4(shadowOpacity,shadowOpacity,shadowOpacity,0);\n"
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

	// "Bind" the newly created texture : all future texture functions will modify this texture
	glActiveTexture(GL_TEXTURE1);
	// Give an empty image to OpenGL ( the last "0" )
	glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, width, 2, 1,GL_RGB, GL_UNSIGNED_BYTE, 0);

	// Poor filtering. Needed !
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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
               -1.0, -1.0,  1.0, -1.0,  1.0, 1.0,  -1, 1.0,// };
               -1, -0.8, 1, -0.8, 1, 1, -1, 1 };

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertsVid);
	return prog;
}

static GLint gl_text_setup(GLuint *texture){
//      char vs[256];

        const char *vs = "attribute vec4 pos;\n"
                         "uniform float imageNo;\n"
                         "uniform float scale;\n"
                         "varying vec2 texcoord;\n"
                         "\n"
                         "void main() {\n"
                         "  gl_Position = pos;\n"
                         "  texcoord.x =pos.x*0.5;\n"
                         "  texcoord.y =-(pos.y-0.75-imageNo)*0.0625;\n"
                         "}\n";

        GLint vs_s = compile_shader(GL_VERTEX_SHADER, vs);
        const char *fs =                 "precision mediump float;\n"
                                         "uniform sampler2D s;\n"
                                         "varying vec2 texcoord;\n"
                                         "void main() {\n"
                                         "  vec2 tc = vec2(texcoord.x+0.5,texcoord.y);\n"
                                         "  vec4 t = texture2D(s, tc);\n"
                                         //"  gl_FragColor = vec4(t.x, t.y*t.w, t.z*t.w,1.0);\n"
                                         "  gl_FragColor = vec4(t.x, t.y, t.z,1.0);\n"
                                        // "  gl_FragColor = vec4(1.0,1.0,1.0,1.0);\n"
                                         "}\n";
        GLint fs_s = compile_shader(GL_FRAGMENT_SHADER, fs);
        GLint prog = link_program(vs_s, fs_s);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE3);
//        unsigned int texture;
        glGenTextures(1, texture);
        glBindTexture(GL_TEXTURE_2D, *texture); // all upcoming GL_TEXTURE_2D operations now have effect on this texture object
        // set the texture wrapping parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);       // set texture wrapping to GL_REPEAT (default wrapping method)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        // set texture filtering parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        static const float verts[] = { 
                -1.0, -1.0,  1.0, -1.0,  1.0, 1.0,  -1, 1.0 };
        int width,height,nrChannels;
        unsigned char *data = stbi_load("wavelengths.png", &width, &height, &nrChannels, 0);
        if (data){
                if(nrChannels==4){
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                }else{
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
                }
                glGenerateMipmap(GL_TEXTURE_2D);
        }
        else
        {
                std::cout << "Failed to load texture" << std::endl;
        }
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
        stbi_image_free(data);
        return prog;
}

static GLint draw_text(int imageNo,int x, int y, int width, int height, float scale, GLint prog, GLuint *texture){
      glUseProgram(prog);
//      static const float vertsText[] = { 0.0, 0,0{ x, y, x+width*scale, y, x+width*scale, y+height*scale, x, y+height*scale};
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,*texture);
        glUniform1f(glGetUniformLocation(prog,"imageNo"), (float)imageNo*2);
        glUniform1f(glGetUniformLocation(prog,"scale"), scale);
	// we have put this texture into GL_TEXTURE3
        glUniform1i(glGetUniformLocation(prog,"s"), 3);
        // Give an empty image to OpenGL ( the last "0" )
        //glUniform1f( glGetUniformLocation(progGraph, "scale"), 1.0);
        glViewport( x,y,width,height);
        //glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        return prog;
}


EglPreview::EglPreview(Options const *options) : Preview(options), last_fd_(-1), first_time_(true)
{
	slope= 0;
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
	theOptions = options;
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
	std::cout <<"makeWindow width="<< width_ << " height=" << height_<<"\n";

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
	typedef struct Hints
	{
    		unsigned long   flags;
    		unsigned long   functions;
    		unsigned long   decorations;
    		long            inputMode;
    		unsigned long   status;
	} Hints;
	//code to remove decoration
	Hints hints;
	Atom property;
	hints.flags = 2;
	hints.decorations = 0;
	property = XInternAtom(display_, "_MOTIF_WM_HINTS", true);
	XChangeProperty(display_,window_,property,property,32,PropModeReplace,(unsigned char *)&hints,5);

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
		progText = gl_text_setup(&textTexture);
		progGraph=gl_setupGraph(info.width, info.height, width_, height_);
		prog=gl_setup(info.width, info.height, width_, height_);
		shrunk = new uint32_t[info.width+2];
		incandescentCalibration = new double[info.width+2];
		darkCalibration = new uint32_t[info.width+2];
		for(unsigned int i=0; i<info.width;i++){
			incandescentCalibration[i]=1.0;
			darkCalibration[i]=0;
		}
		shadowData = new GLubyte[info.width*4*8+1];
       		pixels = new GLubyte[info.width * info.height * 4+20];
//	first_time_ = false;
	}
	label_b = 1;
	label_c = 0;
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

		uint32_t differentiate(uint32_t* shrunk,int width){
			uint32_t d = 0;
			for(int x=0; x<width-1; x++){
				if(shrunk[x]>shrunk[x+1]){
					d+= shrunk[x]-shrunk[x+1];
				}else{
					d+= shrunk[x+1]-shrunk[x];
				}
			}
			return d;
		}

void Shrink(GLubyte * data, uint16_t x0, uint16_t x1, uint16_t width, uint16_t y0, uint16_t y1, uint16_t height, uint16_t stride, uint32_t * output, uint32_t * maxVal, float slope){
	*maxVal = 0;
	for(uint16_t x=x0; x<x1; x++){
		for(uint16_t y=y0; y<y1; y+=4){
			float x2f = slope * y + x;
			int x2 = (int)x2f;
			float rem = x2f-x2;
			if (x2<0) x2=0;
		/*	GLubyte vy = data[(y*stride+x2)];
		        GLubyte vu = data[(int)((y/2+height)*stride+(x2)/2)]-128;
			GLubyte vv = data[(int)((0.5+y/2+height)*stride+(x2)/2)]-128;

			float r = RY*vy + RU * vu + RV * vv;
			float g = GY*vy + GU * vu + GV * vv;
			float b = BY*vy + BU * vu + BV * vv;
			float val = std::max(std::max(r,g),b);*/
			float val= (data[(y*stride+x2)])*Y_FAC//3.0
				+ (data[(int)((y/2+height)*stride+(x2)/2)]-128)*U_FAC//0.781
			       	+ (data[(int)((0.5+y/2+height)*stride+(x2)/2)]-128)*V_FAC;//1.63 ;
			output[x] += (1.0-rem) * val;
			if(x<width-2){
				output[x+1] += rem*val;
			}
		}
	}
	
}
/*

// for SRGGB10
void Shrink(GLubyte * data, uint16_t x0, uint16_t x1, uint16_t width, uint16_t y0, uint16_t y1, uint16_t height, uint16_t stride, uint32_t * output, uint32_t * maxVal, float slope){
	*maxVal = 0;
	int16_t  stride2 = stride;
	uint16_t *iData = (uint16_t*) data;
	for(uint16_t x=x0; x<x1; x++){
		for(uint16_t y=y0; y<y1; y+=8){
        		//std::cout << "x=" <<x << " y=" << y <<" height="<< height<<"\n";
			float x2f = slope * y + x;
			int x2 = (int)x2f;
			float rem = x2f-x2;
			if (x2<0) x2=0;
			float val = 
				// green
			       	iData[ (y+x%2)*stride2 + x ] * G_PROP +// green
			      	iData[ (y+x+1)*stride2 + x + !(x%2) ] * R_PROP + // red
				iData[ (y+x) *stride2 + x - x%2 ] * B_PROP; // blue
			output[x] += (1.0-rem) * val;
			if(x<width-2){
				output[x+1] += rem*val;
			}
		}
	}
	
}
*/
uint32_t EglPreview::ShrinkData(GLubyte *pixels, StreamInfo const *info, uint32_t *shrunk, float *slope2       ){
        uint32_t max1=0, max2=0, max3=0, max4=0;
        //libcamera::Span<uint8_t> buffer = app.Mmap(buffers_[fd])[0];
//      Reduce the data using 4threads to speed it up
	//int16_t r_x = theOptions->roi_x*info->width;      
	//int16_t r_y = theOptions->roi_y*info->height;
	//int16_t r_width = theOptions->roi_width * info->width;
	//int16_t r_height = theOptions->roi_height * info->height;
	int16_t r_x = 0;//theOptions->roi_x*info->width;      
	int16_t r_y = 0;//theOptions->roi_y*info->height;
	int16_t r_width = info->width;//theOptions->roi_width * info->width;
	int16_t r_height = info->height;//theOptions->roi_height * info->height;
	if(r_width ==0 or r_height==0){
		r_x=r_y=0;
		r_width=info->width;
		r_height=info->height;
	}
	for(uint16_t x=0; x<info->width;x++){
		shrunk[x]=0;
	}
	//std::cout << "r_x=" << r_x << " r_y=" << r_y << " r_width=" << r_width << " r_height=" << r_height << "\n";
        //std::thread t1(&Shrink, pixels, 20, 		   400,     r_width, 0, info->height, info->height, info->stride, shrunk, &max1, *slope2 );
        std::thread t1(&Shrink, pixels, r_x, 		   r_x + r_width/4,     r_width, r_y, r_y+r_height, info->height, info->stride, shrunk, &max1, *slope2 );
        std::thread t2(&Shrink, pixels, r_x + r_width/4,   r_x + r_width/2,     r_width, r_y, r_y+r_height, info->height, info->stride, shrunk, &max2, *slope2 );
        std::thread t3(&Shrink, pixels, r_x + r_width/2,   r_x + (3*r_width)/4, r_width, r_y, r_y+r_height, info->height, info->stride, shrunk, &max3, *slope2 );
        std::thread t4(&Shrink, pixels, r_x + (3*r_width)/4, r_x+r_width, 	r_width, r_y, r_y+r_height, info->height, info->stride, shrunk, &max4, *slope2 );
        t1.join();
        t2.join();
        t3.join();
        t4.join();
        if(max2>max1) max1=max2;
        if(max3>max1) max1=max3;
        if(max4>max1) max1=max4;
//        std::cout << "info.width=" << info->width << " info.height=" << info->height << " info.stride="<< info->stride << "shurnk[5]="<<shrunk[5]<<"\n";
        return max1;
}


void EglPreview::Show(int fd, libcamera::Span<uint8_t> span, StreamInfo const &info)
{
	float w_factor = info.width / (float)width_;
	float h_factor = info.height / (float)height_ *2;
	float max_dimension = std::max(w_factor, h_factor);
	w_factor /= max_dimension;
	h_factor /= max_dimension;
	//std::cout << "streaminfo->pixel_format=" << info->pixel_format<<"\n";

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
		readCal(info.width);
		}
	}
	glClearColor(0, 0, 0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
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
	uint32_t max1;//, max2, max3, max4;
	//libcamera::Span<uint8_t> buffer = app.Mmap(buffers_[fd])[0];
        pixels = (GLubyte *)span.data();

//	glReadPixels(0, 0, info.width, info.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
//	Shrink(pixels, 0, info.width/4, info.width, info.height, shrunk, &max1,0 );

//	Reduce the data using 4threads to speed it up
	max1=ShrinkData(pixels, &info, shrunk, &slope );
	// optimise slope by maximising spikyness
	if(doSlope){
		float oldslope = slope;
		uint32_t lastd = differentiate(shrunk, info.width);
		int direction = 1;
		float step = 0.01;
		int count=0;
		while(count<20){
			max1=ShrinkData(pixels, &info, shrunk, &slope );
			uint32_t d = differentiate(shrunk,info.width);
			if(d-lastd > 0){
				direction = -direction;
				step *=0.5;
			}
			if(step<0.0001){
				break;
			}
			slope+=step*direction;
			count++;
		}
		if(count>=20){
			slope=oldslope;
		}
		doSlope=false;
		std::cout << "slope=" << slope << "\n"; 
	}
//	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void*)(0));
	if(doIncandescent){
		incandescentCal(shrunk,info.width);
		doIncandescent=false;
	}else if(doDark){
		darkCal(shrunk,info.width);
		doDark=false;

	}
	if(doMercury){
		parsePeaks(shrunk, info.width);
		doMercury=false;
	}
	max1 = 0;
	for(unsigned int i=0; i<info.width;i++){
		if(shrunk[i]>darkCalibration[i]){
			shrunk[i] -= darkCalibration[i];
		}else{
			shrunk[i]=0;
		}
		shrunk[i] = (int32_t)(incandescentCalibration[i] * (float)shrunk[i]); 
		if(shrunk[i]>max1){
			max1=shrunk[i];
		}
	}
	float scale=(256.0*256-1)/max1;
	// map pixel buffer
	//for(uint16_t i=0; i<info.width*4*4; i+=4){
	for(uint16_t i=0; i<info.width*4; i+=4){
		uint16_t value = scale * shrunk[i/4];
		graphData[i] =   value / 256;
		graphData[i+1] =  value % 256; 
		graphData[i+2] =  0;//value/256;//value % 256; 
		graphData[i+3] =  0;//value/256;//value % 256; 
	}	
	if(doShadow){
		std::cout << "Do shadow\n"; 
//std::memcpy(graphData, shadowData, info.width*4*8);
		for(uint16_t i=0;i<(int)info.width*4;i++){
			shadowData[i]=graphData[i];
		}
		std::cout << "\n";
		doShadow=false;
	}
	if(doSave){
		saveCal(info.width);
		doSave=false;
	}

	// ************************
	// Draw Graph
	// ************************
	//
//	std::cout << "GraphData[5]="
//		<< ","<< (int)graphData[0] 
//		<< ","<< (int)graphData[1] 
//		<< ","<< (int)graphData[2] 
//		<< ","<< (int)graphData[3] 
//		<< ","<< (int)graphData[4]; 
//	std::cout << " Scale="<< scale <<"\n"; 
	float shadowOpacity = 1.0-((float)(std::chrono::system_clock::now() - shadowTime).count()) / 10000000000;
	if(shadowOpacity<0) shadowOpacity=0.0;
	if(shadowOpacity>1.0) shadowOpacity=1.0;
	shadowOpacity*=0.5;
	glUseProgram(progGraph);
//	glEnableVertexAttribArray(1);
//	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, vertsGraph);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		std::cout << "FrameBuffer Graph issue\n";
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D,renderedTexture[1]);

	glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA, info.width, 1, 0,GL_RGBA, GL_UNSIGNED_BYTE, graphData);
	glUniform1i(glGetUniformLocation(progGraph,"s"), 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D,renderedTexture[2]);
	glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA, info.width, 1, 0,GL_RGBA, GL_UNSIGNED_BYTE, shadowData);
	glUniform1i(glGetUniformLocation(progGraph,"shadow"), 2);
	glUniform1f(glGetUniformLocation(progGraph,"shadowOpacity"), shadowOpacity);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// Give an empty image to OpenGL ( the last "0" )
	//glUniform1f( glGetUniformLocation(progGraph, "scale"), 1.0);
//	std::cout << "x0=" << (1.0-w_factor)/2*width_ << "y0=" << 0 << " width=" << (width_*w_factor) << "height" <<height_/2 << "\n";
	glViewport( (1.0-w_factor)/2*width_,0,width_*w_factor,height_/2);
	//glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_FAN, 8, 4);
//	GLuint vbo;
//	glGenBuffers(1, &vbo);
//	glBindBuffer(GL_ARRAY_BUFFER, vbo);
//	glBufferData(GL_ARRAY_BUFFER, sizeof(output[0])*info.width, output, GL_DYNAMIC_DRAW);
//	glDrawElements(GL_LINE_LOOP, info.width*2+4, GL_UNSIGNED_BYTE, 0);
//	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);       // Vertex attributes stay the same
  //  	glEnableVertexAttribArray(0);
  	for(int i=0; i<numLabels;i++){
		if(labelPositions[i] >=0 && labelPositions[i]<(int)info.width){
			//std::cout << "lp" << labelPositions[i];
			draw_text(i, labelPositions[i], 20, 50, 20,2.0, progText, &textTexture);
		}
	}
	//std::cout << "\n";
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
