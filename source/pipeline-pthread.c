#include <stdio.h>
#include <pthread.h>
#include "filter.h"
#include "queue.h"
#include "pipeline.h"

#define MAX_QUEUE_SIZE 24
#define UPSCALE_FACTOR 3

static queue_t* ready_to_scale_queue;
static queue_t* ready_to_reverse_queue;
static queue_t* ready_to_save_queue;


static void* read_image(void* arg){
	while(true){
		image_dir_t* image_dir = (image_dir_t*)arg;
		image_t* image = image_dir_load_next(image_dir);
		if(image == NULL){
			break;
		}

		queue_push(ready_to_scale_queue, image);
	}
}

static void* upscale_image(void* arg){
	while(true){
		image_t* image = queue_pop(ready_to_scale_queue);
		if(image == NULL){
			break;
		}

		image_t* triple_size_image = filter_scale_up(image, UPSCALE_FACTOR);
		queue_push(ready_to_reverse_queue, triple_size_image);
		image_destroy(image);
	}
	return NULL;
}

static void* reverse_image(void* arg){
	while(true){
		image_t* image = queue_pop(ready_to_reverse_queue);
		if(image == NULL){
			break;
		}

		image_t* reversed_image = filter_vertical_flip(image);
		queue_push(ready_to_save_queue, reversed_image);
		image_destroy(image);
	}
}

static void* save_image(void* arg){
	while(true){
		image_t* image = queue_pop(ready_to_save_queue);
		if(image == NULL){
			break;
		}

		image_dir_t* image_dir = (image_dir_t*)arg;
		image_dir_save(image_dir, image);
	}
}

static void init_queues(){
	ready_to_scale_queue = queue_create(MAX_QUEUE_SIZE);
	ready_to_reverse_queue = queue_create(MAX_QUEUE_SIZE);
	ready_to_save_queue = queue_create(MAX_QUEUE_SIZE);
}

static void destroy_queues(){
	queue_destroy(ready_to_scale_queue);
	queue_destroy(ready_to_reverse_queue);
	queue_destroy(ready_to_save_queue);
}


int pipeline_pthread(image_dir_t* image_dir) {

	pthread_t thread_create;
	pthread_t thread_scale;
	pthread_t thread_reverse;
	pthread_t thread_save;

	init_queues();

	pthread_create(thread_create, NULL, read_image, image_dir);
	pthread_create(thread_scale, NULL, upscale_image, NULL);
	pthread_create(thread_reverse, NULL, reverse_image, NULL);
	pthread_create(thread_save, NULL, save_image, image_dir);


	pthread_join(thread_create, NULL);
	pthread_join(thread_scale, NULL);
	pthread_join(thread_reverse, NULL);
	pthread_join(thread_save, NULL);

	destroy_queues();

	return -1;
}
