#ifndef _IMGPROC_H_
#define _IMGPROC_H_

#include <SDL/SDL.h>


// forward declarations of internal types
struct Buffer;


typedef struct {
	unsigned int width;
	unsigned int height;

	char * name;
	int handle;
	struct Buffer * buffers;
	unsigned int n_buffers;
} Camera;


typedef struct {
	unsigned int width;
	unsigned int height;
	char * data;
	char * mem_ptr;
	
	SDL_Surface * sdl_surface;
} Image;


typedef struct {
	unsigned int width;
	unsigned int height;
	SDL_Surface * screen;
} Viewer;



/* Utility operations */
void init_imgproc();
void quit_imgproc();
void waitTime(size_t milliseconds);


/* Webcam operations */
Camera * camOpen(unsigned int width, unsigned int height);
unsigned int camGetWidth(Camera * cam);
unsigned int camGetHeight(Camera * cam);
Image * camGrabImage(Camera * cam);
void camClose(Camera * cam);


/* Image operations */
Image * imgNew(unsigned int width, unsigned int height);
Image * imgFromBitmap(const char * filename);
Image * imgCopy(Image * img);
void imgDestroy(Image * img);

unsigned int imgGetWidth(Image * img);
unsigned int imgGetHeight(Image * img);
void imgSetPixel(Image * img, unsigned int x, unsigned int y, char r, char g, char b);
char * imgGetPixel(Image * img, unsigned int x, unsigned int y);
//PyObject * imgPixel(Image * img, unsigned int x, unsigned int y);


/* Viewer operations */
Viewer * viewOpen(unsigned int width, unsigned int height, const char * title);
void viewDisplayImage(Viewer * view, Image * img);
void viewClose(Viewer * view);


#endif // _IMGPROC_H_

