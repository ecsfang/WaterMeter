#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include <SDL/SDL.h>

#include "imgproc.h"


Image * imgNew(unsigned int width, unsigned int height)
{
	// Allocate for the image container
	Image * img = malloc(sizeof(*img));
	if(img == NULL){
		fprintf(stderr, "Failed to allocate memory for image container\n");
		return NULL;
	}

	// Set the width and height
	img->width = width;
	img->height = height;

	// allocate for image data, 3 byte per pixel, aligned to an 8 byte boundary
	img->mem_ptr = malloc(img->width * img->height * 3 + 8);
	if(img->mem_ptr == NULL){
		fprintf(stderr, "Memory allocation of image data failed\n");
		free(img);
		return NULL;
	}

	// make certain it is aligned to 8 bytes
	unsigned int remainder = ((size_t)img->mem_ptr) % 8;
	if(remainder == 0){
		img->data = img->mem_ptr;
	} else {
		img->data = img->mem_ptr + (8 - remainder);
	}

	
	// Fill the SDL_Surface container
	img->sdl_surface = SDL_CreateRGBSurfaceFrom(
				img->data,
				img->width,
				img->height,
				24, 
				img->width * 3,
				0xff0000,
				0x00ff00,
				0x0000ff,
				0x000000
	);

	// check the surface was initialised
	if(img->sdl_surface == NULL){
		fprintf(stderr, "Failed to initialise RGB surface from pixel data\n");
		SDL_FreeSurface(img->sdl_surface);
		free(img->mem_ptr);
		free(img);
		return NULL;
	}

	// return the image
	return img;
}


Image * imgFromBitmap(const char * filename)
{
	// Load the Bitmap
	SDL_Surface * bitmap = SDL_LoadBMP(filename);


	// Allocate for the image container
	Image * img = malloc(sizeof(*img));
	if(img == NULL){
		fprintf(stderr, "Failed to allocate memory for image container\n");
		return NULL;
	}

	// set the image surface to the bitmap
	img->sdl_surface = bitmap;


	// Set the width and height
	img->width = bitmap->w;
	img->height = bitmap->h;

	// set the data pointer
	img->data = bitmap->pixels;	

	// set the memory pointer to NULL, so we don't cause mayhem trying to free it
	img->mem_ptr = NULL;


	// return the new image
	return img;
}


Image * imgCopy(Image * img)
{
	// Create a new empty image
	Image * copy = imgNew(img->width, img->height);

	// Copy the data between the images
	copy = memcpy(copy->data, img->data, img->width * img->height * 3 );

	// return the copy
	return copy;	
}


unsigned int imgGetWidth(Image * img)
{
	return img->width;
}


unsigned int imgGetHeight(Image * img)
{
	return img->height;
}


void imgSetPixel(Image * img, unsigned int x, unsigned int y, char r, char g, char b)
{
	// calculate the offset into the image array
	uint32_t offset = 3 * (x + (y * img->width));
	// set the rgb value
	img->data[offset + 2] = b;
	img->data[offset + 1] = g;
	img->data[offset + 0] = r;
}


// returns a pointer to the rgb tuple
char * imgGetPixel(Image * img, unsigned int x, unsigned int y)
{
	uint32_t offset = 3 * (x + (y * img->width));
	return (char *)(img->data + offset);
}


// Destroys the image
void imgDestroy(Image * img)
{
	// Free the SDL surface
	SDL_FreeSurface(img->sdl_surface);
	if(img->mem_ptr != NULL){
		free(img->mem_ptr);
	}
	// Free the image container, Python will handle the memoryview + buffer
	free(img);
}

