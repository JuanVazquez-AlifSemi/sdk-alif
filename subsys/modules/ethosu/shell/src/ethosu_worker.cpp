/*
 * Copyright 2019-2022 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/****************************************************************************
 * Includes
 ****************************************************************************/

#include "inference_process.hpp"

#include <inttypes.h>
#include <string>
#include <stdio.h>
#include <vector>
#include <zephyr/kernel.h>

/* Model data */
#include "ethosu/models/keyword_spotting_cnn_small_int8/input.h"
#include "ethosu/models/keyword_spotting_cnn_small_int8/output.h"

#if defined(CONFIG_ARM_ETHOS_U55_256)
#include "ethosu/models/keyword_spotting_cnn_small_int8/model_u55_256.h"
#else
#include "ethosu/models/keyword_spotting_cnn_small_int8/model.h"
#endif

#include "ethosu_worker.hpp"

using namespace std;
using namespace InferenceProcess;

/****************************************************************************
 * Defines
 ****************************************************************************/

/* Tensor arena size */
#ifdef TENSOR_ARENA_SIZE /* If defined in model.h */
#define TENSOR_ARENA_SIZE_PER_INFERENCE TENSOR_ARENA_SIZE
#else /* If not defined, use maximum available */
#define TENSOR_ARENA_SIZE_PER_INFERENCE 2000000 / NUM_INFERENCE_TASKS
#endif

/****************************************************************************
 * InferenceJob
 ****************************************************************************/

namespace
{
struct InferenceProcessParams {
	InferenceProcessParams() : tensorArena(nullptr), arenaSize(0)
	{
	}

	InferenceProcessParams(k_queue *_queue, uint8_t *_tensorArena, size_t _arenaSize)
		: queueHandle(_queue), tensorArena(_tensorArena), arenaSize(_arenaSize)
	{
	}

	k_queue *queueHandle;
	uint8_t *tensorArena;
	size_t arenaSize;
};

class Worker_info {
	public:
	std::string name;
	k_thread thread;
	k_tid_t id;
	k_thread_stack_t *stack = NULL;
	InferenceProcessParams taskParams;
	volatile bool running = false;
};

/* Wrapper around InferenceProcess::InferenceJob. Adds responseQueue and status
 * for Zephyr multi-tasking purposes.& */
struct xInferenceJob : public InferenceJob {
	xInferenceJob() : InferenceJob(), responseQueue(nullptr), status(false)
	{
	}

	xInferenceJob(const string &_name, const DataPtr &_networkModel,
		      const vector<DataPtr> &_input, const vector<DataPtr> &_output,
		      const vector<DataPtr> &_expectedOutput, k_queue *_queue)
		: InferenceJob(_name, _networkModel, _input, _output, _expectedOutput),
		  responseQueue(_queue), status(false)
	{
	}

	k_queue *responseQueue;
	bool status;
};

/* Number of total completed jobs, needed to exit application correctly if
 * NUM_JOB_TASKS > 1 */
volatile int totalCompletedJobs = 0;

/* TensorArena static initialisation */
const size_t arenaSize = TENSOR_ARENA_SIZE_PER_INFERENCE;

__attribute__((section(".bss.tflm_arena"), aligned(16)))
uint8_t inferenceProcessTensorArena[arenaSize];


static k_heap *g_heap = NULL;

/* Allocate and initialize heap */
void *allocateHeap(const size_t size)
{
	if (!g_heap) {
		k_heap *heap = static_cast<k_heap *>(k_malloc(sizeof(k_heap)));
		uint8_t *buf = static_cast<uint8_t *>(k_malloc(size));

		if ((buf == nullptr) || (heap == nullptr)) {
			printk("Heap allocation failed. heap=%p, buf=%p, size=%zu\n", heap, buf, size);
			exit(1);
		}

		k_heap_init(heap, buf, size);
		g_heap = heap;
	}

	return static_cast<void *>(g_heap);
}


/* inferenceProcessTask - Run jobs from queue with available driver */
void inferenceProcessTask(void *_worker_info, void *heap, void *_params)
{
	Worker_info *wi = static_cast<Worker_info *>(_worker_info);
	InferenceProcessParams *params = static_cast<InferenceProcessParams *>(_params);

	/* Assign the pre allocated heap - used in the k_queue_alloc_append */
	k_thread_heap_assign(k_current_get(), static_cast<k_heap *>(heap));

	class InferenceProcess inferenceProcess(params->tensorArena, params->arenaSize);

	uint32_t jobcnt = 0;
	for (;;) {
		if (!wi->running) {
			break;
		}
		/* Receive inference job */
		xInferenceJob job = xInferenceJob(modelName, DataPtr((void*)networkModelData, sizeof(networkModelData)),
						{ DataPtr((void*)inputData, sizeof(inputData)) }, {},
						{ DataPtr((void*)expectedOutputData, sizeof(expectedOutputData)) },
						nullptr);

		/* Run inference */
		job.status = inferenceProcess.runJob(job);
		jobcnt++;
		if ((jobcnt % 100) == 0) {
			printk("%s jobcnt=%u status=%s\n", wi->name.c_str(), jobcnt, job.status ? "failed" : "ok");
		}
	}
}


} /* namespace */


static Worker_info worker_info;

void Ethos_worker::start_eu()
{
	if (worker_info.running) {
		return;
	}

	/* Allocate one global heap for all threads */
	void *heapPtr = allocateHeap(256);

	/* Create inferenceProcess tasks to process the queued jobs */
	const size_t stackSize = 8192;
	if (!worker_info.stack) {
		worker_info.stack = static_cast<k_thread_stack_t *>(k_malloc(stackSize));
		if (worker_info.stack == nullptr) {
			printk("Failed to allocate stack to 'inferenceSenderTask'\n");
			exit(1);
		}
	}

    worker_info.name = "Inference 0";
	worker_info.taskParams = InferenceProcessParams(nullptr, inferenceProcessTensorArena,
						    arenaSize);

	worker_info.id = k_thread_create(&worker_info.thread, worker_info.stack, stackSize, inferenceProcessTask,
					&worker_info, heapPtr, &worker_info.taskParams, 2, 0, K_FOREVER);

	if (worker_info.id == 0) {
		printk("Failed to create 'inferenceProcessTask'\n");
		exit(1);
	}

    worker_info.running = true;
	k_thread_start(worker_info.id);
}

void Ethos_worker::stop_eu()
{
	worker_info.running = false;
}
