#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include <getopt.h>             /* getopt_long() */

#include <fcntl.h>             /* low-level i/o */
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>

//#include <Python.h>

#include "imgproc.h"


struct Buffer {
	struct v4l2_buffer buf;
	void * start;
};


static void errno_exit(const char *s)
{
        fprintf (stderr, "%s error %d, %s\n",
			s, errno, strerror (errno));

        exit (EXIT_FAILURE);
}


static int xioctl(Camera * cam, int request, void *arg)
{
        int r;

        do r = ioctl (cam->handle, request, arg);
        while (-1 == r && EINTR == errno);

        return r;
}


// routine to initialise memory mapped i/o on the camera device
static void init_mmap(Camera * cam)
{
	struct v4l2_requestbuffers req;

	memset (&(req), 0, sizeof (req));

	req.count               = 4;
	req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory              = V4L2_MEMORY_MMAP;

	if (-1 == xioctl (cam, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s does not support "
					"memory mapping\n", cam->name);
			exit (EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf (stderr, "Insufficient buffer memory on %s\n",
				 cam->name);
		exit (EXIT_FAILURE);
	}

	// allocate memory for the buffers
	cam->buffers = calloc (req.count, sizeof (*(cam->buffers)));

	if (!cam->buffers) {
		fprintf (stderr, "Out of memory\n");
		exit (EXIT_FAILURE);
	}

	for (cam->n_buffers = 0; cam->n_buffers < req.count; cam->n_buffers++) {
		struct v4l2_buffer buffer;

		memset (&(buffer), 0, sizeof (buffer));

		buffer.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory      = V4L2_MEMORY_MMAP;
		buffer.index       = cam->n_buffers;

		if (-1 == xioctl (cam, VIDIOC_QUERYBUF, &buffer)){
			errno_exit ("VIDIOC_QUERYBUF");
		}

		// copy the v4l2 buffer into the device buffers
		cam->buffers[cam->n_buffers].buf = buffer;
		// memory map the device buffers
		cam->buffers[cam->n_buffers].start = 
			mmap( NULL, // start anywhere
				  buffer.length,
				  PROT_READ | PROT_WRITE, // required
				  MAP_SHARED, // recommended
				  cam->handle,
				  buffer.m.offset
			);

		if (MAP_FAILED == cam->buffers[cam->n_buffers].start){
			errno_exit ("mmap");
		}
	}
}


// returns an index to the dequeued buffer
static unsigned int camDequeueBuffer(Camera * cam)
{
	while(1){
		fd_set fds;
		struct timeval tv;
		int r;

		
		FD_ZERO (&fds);
		FD_SET (cam->handle, &fds);

		// Timeout.
		tv.tv_sec = 20;
		tv.tv_usec = 0;

		
		r = select (cam->handle + 1, &fds, NULL, NULL, &tv);

		if (-1 == r) {
			if (EINTR == errno){
				continue;
			}
			errno_exit ("select");
		}

		if (0 == r) {
			fprintf (stderr, "select timeout\n");
			exit (EXIT_FAILURE);
		}

		
		// read the frame
		struct v4l2_buffer buffer;
		memset (&(buffer), 0, sizeof (buffer));

		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		
		// dequeue a buffer
		if (-1 == xioctl (cam, VIDIOC_DQBUF, &buffer)) {
			switch (errno) {
				case EAGAIN:
					continue;

				case EIO:
					/* Could ignore EIO, see spec. */
					/* fall through */

				default:
					errno_exit ("VIDIOC_DQBUF");
			}
		}
		assert (buffer.index < cam->n_buffers);

		// return the buffer index handle to the buffer
		return buffer.index;
	}
}


// enqueue a given device buffer to the device
static void camEnqueueBuffer(Camera * cam, unsigned int buffer_id)
{
	// enqueue a given buffer by index
	if(-1 == xioctl(cam, VIDIOC_QBUF, &(cam->buffers[buffer_id].buf) )){
		errno_exit("VIDIOC_QBUF");
	}

	return;
}
	

Image * camGrabImage(Camera * cam)
{
	// Create a new image
	Image * img = imgNew(cam->width, cam->height);


	// dequeue a buffer
	unsigned int buffer_id = camDequeueBuffer(cam);


	// Copy data across, converting to RGB along the way
	char * buffer_ptr = cam->buffers[buffer_id].start;
	char * img_ptr = img->data;

	// iterate 2 pixels at a time, so 4 bytes for YUV and 6 bytes for RGB
	for(uint32_t i = 0, j = 0; i < (img->width * img->height * 2); i+=4, j+=6){
		char * buffer_pos = buffer_ptr + i;
		char * img_pos = img_ptr + j;
		

		// YCbCr to RGB conversion (from: http://www.equasys.de/colorconversion.html);
		int y0 = buffer_pos[0];
		int cb = buffer_pos[1];
		int y1 = buffer_pos[2];
		int cr = buffer_pos[3];
		int r;
		int g;
		int b;

		// first RGB
		r = y0 + ((357 * cr) >> 8) - 179;

		g = y0 - (( 87 * cb) >> 8) +  44 - ((181 * cr) >> 8) + 91;
		b = y0 + ((450 * cb) >> 8) - 226;
		// clamp to 0 to 255
		img_pos[2] = r > 254 ? 255 : (r < 0 ? 0 : r);
		img_pos[1] = g > 254 ? 255 : (g < 0 ? 0 : g);
		img_pos[0] = b > 254 ? 255 : (b < 0 ? 0 : b);
		
		// second RGB
		r = y1 + ((357 * cr) >> 8) - 179;
		g = y1 - (( 87 * cb) >> 8) +  44 - ((181 * cr) >> 8) + 91;
		b = y1 + ((450 * cb) >> 8) - 226;
		img_pos[5] = r > 254 ? 255 : (r < 0 ? 0 : r);
		img_pos[4] = g > 254 ? 255 : (g < 0 ? 0 : g);
		img_pos[3] = b > 254 ? 255 : (b < 0 ? 0 : b);
	}


	// requeue the buffer
	camEnqueueBuffer(cam, buffer_id);


	// return the image
	return img;
}


static void camSetFormat(Camera * cam, unsigned int width, unsigned int height)
{
	//printf("Setting device format\n");

	struct v4l2_format fmt;
	unsigned int min;

	memset (&(fmt), 0, sizeof (fmt));
	
	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = width; 
	fmt.fmt.pix.height      = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

	if (-1 == xioctl (cam, VIDIOC_S_FMT, &fmt)){
		errno_exit ("VIDIOC_S_FMT");
	}

    // Note VIDIOC_S_FMT may change width and height.
	
	// Buggy driver paranoia.
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	// ONLY CHANGES IN IMAGE SIZE ARE HANDLED ATM
	// set device image size to the returned width and height.
	cam->width = fmt.fmt.pix.width;
	cam->height = fmt.fmt.pix.height;
	

	//printf("Initialising memory mapped i/o\n");
	
	// initialise for memory mapped io
	init_mmap (cam);
	
	
	// initialise streaming for capture
	enum v4l2_buf_type type;
	
	// queue buffers ready for capture
	for (unsigned int i = 0; i < cam->n_buffers; ++i) {
		// buffers are initialised, so just call the enqueue function
		camEnqueueBuffer(cam, i);		
	}
	
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	// turn on streaming
	if (-1 == xioctl (cam, VIDIOC_STREAMON, &type)){
		if(errno == EINVAL){
			fprintf(stderr, "buffer type not supported, or no buffers allocated or mapped\n");
			exit(EXIT_FAILURE);
		} else if(errno == EPIPE){
			fprintf(stderr, "The driver implements pad-level format configuration and the pipeline configuration is invalid.\n");
			exit(EXIT_FAILURE);
		} else {
			errno_exit ("VIDIOC_STREAMON");
		}
	}
}


// close video capture device
void camClose(Camera * cam)
{
	//printf("Stopping camera capture\n");

	// stop capturing
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl (cam, VIDIOC_STREAMOFF, &type)){
		errno_exit ("VIDIOC_STREAMOFF");
	}


	//printf("Uninitialising device\n");

	// uninitialise the device
	for (unsigned int i = 0; i < cam->n_buffers; ++i){
		if(-1 == munmap(cam->buffers[i].start, cam->buffers[i].buf.length)){
			errno_exit("munmap");
		}
	}
	
	// free buffers
	free (cam->buffers);
	
	//printf("Closing device\n");

	// close the device
	if (-1 == close(cam->handle)){
		errno_exit ("close");
	}

	free(cam);
	cam->handle = -1;
}


// Open a video capture device
Camera * camOpen(unsigned int width, unsigned int height)
{
	//printf("Opening the device\n");
	

	char * dev_name = "/dev/video0";

	
	// initialise the device
	struct stat st; 

	if (-1 == stat (dev_name, &st)) {
		fprintf (stderr, "Cannot identify '%s': %d, %s\n",
			dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}

	if (!S_ISCHR (st.st_mode)) {
		fprintf (stderr, "%s is no device\n", dev_name);
		exit (EXIT_FAILURE);
	}

	
	// set up the device
	Camera * cam = malloc(sizeof(*cam));
	if(cam == NULL){
		fprintf(stderr, "Could not allocate memory for device structure\n");
		exit(EXIT_FAILURE);
	}
	
	// open the device
	cam->handle = open(dev_name, O_RDWR | O_NONBLOCK, 0);
	cam->name = dev_name;

	if (-1 == cam->handle) {
		fprintf (stderr, "Cannot open '%s': %d, %s\n",
			 dev_name, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}
	
	
	//printf("Initialising device\n");
	
	
	// initialise the device
	struct v4l2_capability cap;

	// check capabilities
	if (-1 == xioctl (cam, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf (stderr, "%s is no V4L2 device\n",
					 cam->name);
			exit (EXIT_FAILURE);
		} else {
				errno_exit ("VIDIOC_QUERYCAP");
		}
	}

	// check capture capable
	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf (stderr, "%s is no video capture device\n",
					 cam->name);
		exit (EXIT_FAILURE);
	}


	// check for memory mapped io
	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf (stderr, "%s does not support streaming i/o\n",
			 cam->name);
		exit (EXIT_FAILURE);
	}
	
	
	// Set the Camera's format
	camSetFormat(cam, width, height);


	return cam;
}
