#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include <imgproc.h>
#include <mosquitto.h>

#define NUM_REGIONS      8
#define IMAGE_WIDTH    176
#define IMAGE_HEIGHT   144 
#define WATER_METER_TOTAL_FILE   "/home/pi/logs/water-meter-total"

typedef struct _REGION {
   unsigned int x, y;
   unsigned int w, h;
} REGION;

REGION region[] =
{
   {45, 65, 10, 10},
   {40, 45, 10, 10},
   {45, 25, 10, 10},
   {67, 15, 10, 10},
   {90, 25, 10, 10},
   {95, 45, 10, 10},
   {90, 65, 10, 10},
   {67, 75, 10, 10}
};

Viewer *view = NULL;
Camera *cam  = NULL;
struct mosquitto *mosq = NULL;
double meter_start_value = 0.0;

static int regionHit(Image *img) {

   unsigned int i;
   unsigned int x, y;
   unsigned int rx, ry, rw, rh;
   unsigned int count_dark;
   unsigned char red;
   unsigned char green;
   unsigned char blue;
   unsigned char *pixel;

   for (i = 0; i < NUM_REGIONS; i++) {
      count_dark = 0;
      rx = region[i].x;
      ry = region[i].y;
      rw = region[i].w;
      rh = region[i].h;

      // Count number of dark pixels in given region
      for (x = rx; x < rx + rw; x++) {
         for (y = ry; y < ry + rh; y++) {
            // Get a pointer to the current pixel
            pixel = (unsigned char *)imgGetPixel(img, x, y);

            // index 0 is blue, 1 is green and 2 is red
            red = pixel[2];
            green = pixel[1];
            blue = pixel[0];

            // check if pixel is dark
            if (red < 128 || green < 128 || blue < 128){
               count_dark++;
            }
         }
      }

      // We have a hit if more than 80% of the pixels is dark 
      if (count_dark > (rw * rh) * 0.8){
         return i;
      }
   }

   return -1;
}

static void drawRegion(Image *img, REGION region, unsigned char red, unsigned char green, unsigned char blue) {

   unsigned int x, y;
   unsigned int rx, ry, rw, rh;

   rx = region.x;
   ry = region.y;
   rw = region.w;
   rh = region.h;

   y = ry;
   for (x = rx; x < rx + rw; x++) imgSetPixel(img, x, y, blue, green, red);
   y = ry + rh;
   for (x = rx; x < rx + rw; x++) imgSetPixel(img, x, y, blue, green, red);
   x = rx;
   for (y = ry; y < ry + rh; y++) imgSetPixel(img, x, y, blue, green, red);
   x = rx + rw;
   for (y = ry; y < ry + rh; y++) imgSetPixel(img, x, y, blue, green, red);
}
void doPublish(char *topic, char *payload) {

   int i;
   int  rc;

   for (i = 0; i < 10; i++) {
      rc = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, true);
      if (rc != MOSQ_ERR_SUCCESS) {
         fprintf(stderr, "Error: mosquitto_publish %d. Try to reconnect.\n", rc);
         fflush(stderr);
         rc = mosquitto_reconnect(mosq);
         if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Error: mosquitto_reconnect %d. Failed to reconnect.\n", rc);
            fflush(stderr);
         }
      }
      else {
         mosquitto_loop(mosq, 0, 1);
         return;
      }
   }
}

void publishValues(time_t time, double last_minute, double last_10minute, double last_drain,
                   double total) {
   char *last_minute_topic   = "/lusa/misc-1/WATER_METER_FLOW/status";
   char *last_10minute_topic = "/lusa/misc-1/WATER_METER_10MIN/status";
   char *last_drain_topic    = "/lusa/misc-1/WATER_METER_DRAIN/status";
   char *total_topic         = "/lusa/misc-1/WATER_METER_TOTAL/status";
   char *payload_format      = "{\"type\":\"METER_VALUE\",\"update_time\":%llu,\"value\":%.2f,\"unit\":\"%s\"}";

   char payload[200];
   static double published_last_minute   = -1.0;
   static double published_last_10minute = -1.0;
   static double published_last_drain    = -1.0;
   static double published_total         = -1.0;

   if (mosq) {
      if (last_minute != published_last_minute) {
         sprintf(payload, payload_format, (long long)time*1000, last_minute, "l/m");
         doPublish(last_minute_topic, payload);
         published_last_minute = last_minute;
      }
      if (last_10minute != published_last_10minute) {
         sprintf(payload, payload_format, (long long)time*1000, last_10minute, "l/10m");
         doPublish(last_10minute_topic, payload);
         published_last_10minute = last_10minute;
      }

      if (last_drain != published_last_drain && last_drain > 0.0) {
         sprintf(payload, payload_format, (long long)time*1000, last_drain, "l");
         doPublish(last_drain_topic, payload);
         published_last_drain = last_drain;
      }

      if (total != published_total) {
         sprintf(payload, payload_format, (long long)time*1000, total + meter_start_value, "l");
         doPublish(total_topic, payload);
         published_total = total;

         FILE *fp = fopen(WATER_METER_TOTAL_FILE, "w+");
         if (fp) {
            fprintf(fp, "%8.2f", total + meter_start_value);
            fclose(fp);
         }
      }

      mosquitto_loop(mosq, 0, 1);
   }
   else {
      fprintf(stderr, "Error: mosq\n");
      fflush(stderr);
   }
}

void updateValues(int new_region_number) {

   static time_t last_update_time = 0;
   static time_t last_update_10time = 0;
   static int last_region_number = -1;

   static int    frame_rate = 0;
   static double total = 0.0;
   static double last_drain = 0.0;
   static double last_minute = 0.0;
   static double last_10minute = 0.0;

   int    elapsed_regions;
   time_t new_time = time(0);

   struct tm *tmptr = localtime(&new_time);
   char time_str[20];
   strftime(time_str, sizeof(time_str), "%H:%M:%S", tmptr);

   if (last_update_time == 0) last_update_time = new_time;
   if (last_update_10time == 0) last_update_10time = new_time;

   if (new_region_number != -1 && last_region_number != -1 &&
       new_region_number != last_region_number) {
      fprintf(stdout, "%s - Hit region: %d [ +0.125 l ]\n", time_str, new_region_number);
      fflush(stdout);

      elapsed_regions = new_region_number - last_region_number;
      if (elapsed_regions < 0) elapsed_regions += NUM_REGIONS;  

      total         += (elapsed_regions * 1.0 / NUM_REGIONS);
      last_minute   += (elapsed_regions * 1.0 / NUM_REGIONS);
      last_10minute += (elapsed_regions * 1.0 / NUM_REGIONS);
      last_drain    += (elapsed_regions * 1.0 / NUM_REGIONS);

   }
   if (new_time >= last_update_time + 60) {
      
      publishValues(new_time, last_minute, last_10minute, last_drain, total);


      fprintf(stdout, "%s - Last minute: %6.2f l, Last 10min: %6.2f l, Last drain: %6.2f l, Total: %8.2f l, Framerate: %d\n",
              time_str, last_minute, last_10minute, last_drain, total + meter_start_value, frame_rate/60);
      fflush(stdout);
   
      if (last_minute == 0.0) last_drain = 0.0;
      last_minute = 0.0;
      last_update_time = new_time;
      frame_rate = 0;
   }
   if (new_time >= last_update_10time + 10*60) {
      last_10minute = 0.0;
      last_update_10time = new_time;
   }
   if (new_region_number != -1) last_region_number = new_region_number;
   frame_rate++;
}

static void cleanup(int sig, siginfo_t *siginfo, void *context) {

   if (view) viewClose(view);
   if (cam) camClose(cam);

   // unintialise the library
   quit_imgproc();

   // cleanup mosquitto connection
   if (mosq) mosquitto_destroy(mosq);
   mosquitto_lib_cleanup();

   exit(0);
}

int main(int argc, char * argv[])
{
   int    i;
   char   *host = "192.168.0.23";
   int    port = 1883;
   int    keepalive = 120;
   bool   clean_session = true;
   int    new_region_number;
   bool   display_image = false;

//   struct sigaction sa;

//   memset(&sa, '\0', sizeof(sa));
//   sa.sa_sigaction = &cleanup;
//   sa.sa_flags = SA_SIGINFO;
//   if (sigaction(SIGINT, &sa, NULL) < 0) {
//      fprintf(stderr, "Error: sigaction\n");
//      fflush(stderr);
//      return 1;
//   }

   // get start options
   for (i = 0; i < argc; i++) {
      if (strcmp(argv[i], "-di") == 0) { 
         display_image = true;
      }
      if (strcmp(argv[i], "-start_value") == 0) { 
         i++;
         sscanf(argv[i], "%lf", &meter_start_value);
      }
   }

   if (meter_start_value == 0.0) {
      FILE *fp = fopen(WATER_METER_TOTAL_FILE, "r");
      if (fp) {
         fscanf(fp, "%lf", &meter_start_value);
         fclose(fp);
      }
   }

   

   // initialise mosquitto connection
   mosquitto_lib_init();
   mosq = mosquitto_new(NULL, clean_session, NULL);
   if (!mosq) {
      fprintf(stderr, "Error: Out of memory.\n");
      fflush(stderr);
      return 1;
   }

   if(mosquitto_connect(mosq, host, port, keepalive)){
      fprintf(stderr, "Unable to connect.\n");
      fflush(stderr);
      return 1;
   }

   // initialise the image library
   init_imgproc();

   // open the webcam 
   cam = camOpen(IMAGE_WIDTH, IMAGE_HEIGHT);
   if (!cam) {
      fprintf(stderr, "Unable to open camera\n");
      fflush(stderr);
      exit(1);
   }

   // create a new viewer of the same resolution with a caption
   if (display_image) {
      view = viewOpen(IMAGE_WIDTH, IMAGE_HEIGHT, "WATER-METER");
      if (!view) {
         fprintf(stderr, "Unable to open view\n");
         fflush(stderr);
         exit(1);
      }
   }

   // capture images from the webcam	
   while(1){
      Image *img = camGrabImage(cam);
      if (!img) {
         fprintf(stderr, "Unable to grab image\n");
         fflush(stderr);
         exit(1);
      }

      // check if any region has a hit
      new_region_number = regionHit(img);

      // update accumulated values
      updateValues(new_region_number);

      if (display_image) {
         for (i = 0; i < NUM_REGIONS; i++) {
          
            unsigned char red = 0;
            unsigned char green= 255;
            unsigned char blue = 0;

            if (i == new_region_number) {
               red = 255;
               green = 0;
               blue = 0;
            }
            drawRegion(img, region[i], red, green, blue);
         }
         // display the image to view the changes
         viewDisplayImage(view, img);
      }

      // destroy image
      imgDestroy(img);
   }

   // cleanup and exit
   cleanup(0, NULL, NULL);
   return 0;
}
