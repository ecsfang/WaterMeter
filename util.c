#include <SDL/SDL.h>

#include "imgproc.h"


// initialise
void init_imgproc()
{
	SDL_Init(SDL_INIT_VIDEO);
}


// delay for a given number of milliseconds
void waitTime(size_t msec)
{
	SDL_Delay(msec);
}


// quit
void quit_imgproc()
{
	SDL_Quit();
}

