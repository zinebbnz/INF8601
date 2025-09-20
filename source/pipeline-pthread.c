#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include "filter.h"
#include "queue.h"
#include "pipeline.h"

// TODO : Vérifier si fuite de mémoire, figure out comment savoir les quantités de threads supportés par le serveur

#define MAX_QUEUE_SIZE 576
#define UPSCALE_FACTOR 3

static queue_t* ready_to_scale_queue;
static queue_t* ready_to_reverse_queue;
static queue_t* ready_to_save_queue;

typedef struct{
	image_dir_t* image_dir;
	long max_threads;
} read_args_t;


static void* read_image(void* arg){
	read_args_t* args = (read_args_t*)arg;
	while(true){
		image_t* image = image_dir_load_next(args->image_dir);
		if(image == NULL){
			break;
		}

		queue_push(ready_to_scale_queue, image);
	}

	long max_threads = args->max_threads;
	for(int i = 0 ; i < max_threads ; i++){
		queue_push(ready_to_scale_queue, NULL);
	}

	return NULL;
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

	long max_threads = (long)(intptr_t)arg;
	for(int i = 0 ; i < max_threads ; i++){
		queue_push(ready_to_reverse_queue, NULL);
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

	long max_threads = (long)(intptr_t)arg;
	for(int i = 0 ; i < max_threads ; i++){
		queue_push(ready_to_save_queue, NULL);
	}

	return NULL;
}

static void* save_image(void* arg){
	while(true){
		image_t* image = queue_pop(ready_to_save_queue);
		if(image == NULL){
			break;
		}

		image_dir_t* image_dir = (image_dir_t*)arg;
		image_dir_save(image_dir, image);
		printf(".");
        fflush(stdout);
		image_destroy(image);
	}

	return NULL;
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

	long MAX_THREADS = sysconf(_SC_NPROCESSORS_ONLN);
	pthread_t thread_read;
	pthread_t thread_scale[MAX_THREADS];
	pthread_t thread_reverse[MAX_THREADS];
	pthread_t thread_save[MAX_THREADS];

	read_args_t read_args = {.image_dir = image_dir, .max_threads = MAX_THREADS};

	init_queues();

	pthread_create(&thread_read, NULL, read_image, &read_args);
	
	for(int i = 0 ; i < MAX_THREADS ; i++){
		pthread_create(&thread_scale[i], NULL, upscale_image, (void*)(intptr_t)MAX_THREADS);
	}

	for(int i = 0 ; i < MAX_THREADS ; i++){
		pthread_create(&thread_reverse[i], NULL, reverse_image, (void*)(intptr_t)MAX_THREADS);
	}

	for(int i = 0 ; i < MAX_THREADS ; i++){
		pthread_create(&thread_save[i], NULL, save_image, image_dir);
	}
	
	
	// Destruction des threads et désallocation de mémoire

	pthread_join(thread_read, NULL);

	for(int i = 0 ; i < MAX_THREADS ; i++){
		pthread_join(thread_scale[i], NULL);
	}

	for(int i = 0 ; i < MAX_THREADS ; i++){
		pthread_join(thread_reverse[i], NULL);
	}

	for(int i = 0 ; i < MAX_THREADS ; i++){
		pthread_join(thread_save[i], NULL);
	}
	
	destroy_queues();

	return 0;
}
