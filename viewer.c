#include <malloc.h>

#include <SDL/SDL.h>

#include "imgproc.h"


Viewer * viewOpen(unsigned int width, unsigned int height, const char * title)
{	
	// set up the view
	Viewer * view = malloc(sizeof(*view));
	if(view == NULL){
		fprintf(stderr, "Could not allocate memory for view\n");
		return NULL;
	}
	
	// initialise the screen surface
	view->screen = SDL_SetVideoMode(width, height, 24, SDL_SWSURFACE);
	if(view == NULL){
		fprintf(stderr, "Failed to open screen surface\n");
		free(view);
		return NULL;
	}

	// set the window title
	SDL_WM_SetCaption(title, 0);
	
	// return the completed view object
	return view;
}


void viewClose(Viewer * view)
{
	// free the screen surface
	SDL_FreeSurface(view->screen);
	// free the view container
	free(view);
}


// take an image and display it on the view
void viewDisplayImage(Viewer * view, Image * img)
{
	// Blit the image to the window surface
	SDL_BlitSurface(img->sdl_surface, NULL, view->screen, NULL);
	
	// Flip the screen to display the changes
	SDL_Flip(view->screen);
}
