#pragma once
#include "ecsInternal.h"
#include "../jobs/jobs.h"






// dispatches jobs for each chunk (and build accessors) (use the fence provided)
JobBatchID dispatchChunks(SystemProcessFn process, uint32 chunkCount, SystemChunk* chunks, JobFence fence);
EcsSystemID submitSystem(SelectionAccessCriteria selection, SystemExecution execution, SystemProcessFn process, void* ctx);
EcsSystemID submitSystemCustom(SelectionAccessCriteria selection, SystemExecution execution, SystemProcessFn process, SystemDispatchFn dispatchOverride, void* ctx);

void enableSystem(EcsSystemID system, bool32 enable);
bool32 isSystemEnabled(EcsSystemID system);


void destroySystem(EcsSystemID system);