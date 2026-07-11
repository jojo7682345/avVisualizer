#include "renderer.h"
#include "core/utils/math.h"
#include "logging.h"
#include "renderer/renderer.h"
#include "renderer/vulkan/renderer.h"
#include <AvUtils/avMemory.h>

typedef struct RendererState {
	Renderer renderer;
} RendererState;
static RendererState* state;

bool8 rendererStartup(uint64* memoryRequirement, void* statePtr, void* configPtr){
 	*memoryRequirement = sizeof(RendererState) + _Alignof(RendererState);
    if(statePtr==NULL) return true;
    state = alignPtr(statePtr, _Alignof(RendererState));
	avMemset(state, 0, sizeof(RendererState));
    
    RendererConfig* config = (RendererConfig*)configPtr;
	(void)config;
	if(!rendererCreate(&state->renderer)){
		avError("Failed to create renderer");
		return false;
	}

	return true;
}
void rendererShutdown(void* statePtr){
	(void)statePtr;
	if(!state) return;
	rendererDestroy(&state->renderer);

	
}