#pragma once
#include "../jobs.h"
#include "jobsInternal.h"


JobBatchID allocateJobBatch(JobBatchDescription* description, uint32 dependencyCount, JobBatchID* dependencies);

bool32 signalJobComplete(JobBatchID batch);

void getJobBatch(JobBatchID id, JobBatchDescription* batch);

void writeJobBatch(JobBatchID id, JobBatchDescription* batch);

void freeJobBatch(JobBatchID batch);

bool32 addDependent(JobBatchID target, JobBatchID dependent);

void jobBatchPoolInit();
void jobBatchPoolDeinit();


