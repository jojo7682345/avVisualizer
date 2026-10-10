#ifdef __linux__
#include "core/platform.h"
#include "logging.h"

#include <AvUtils/avMemory.h>
#include <AvUtils//avString.h>

#include <dlfcn.h>

#include <unistd.h>
#define _GNU_SOURCE
#define __USE_GNU
#include <sys/mman.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "core/plugins/plugin.h"


#define LIST_OF_BACKEND_FUNCTIONS \
	BACKEND_FUNC(bool8, startup, uint64* memoryRequirement, void* state, PlatformConfig* config)\
    BACKEND_FUNC(void, shutdown, void* state)\
    BACKEND_FUNC(void, getFramebufferSize, uint32* w, uint32* h)\
    BACKEND_FUNC(bool8, pumpMessages, void)\
	BACKEND_FUNC(void, getVulkanRequiredExtensions, const char*** namesDarray)\
	BACKEND_FUNC(bool32, createVulkanSurface, void* instance, void* surface, Window window)\
	BACKEND_FUNC(Window, createWindow, uint32* x, uint32* y, uint32* width, uint32* height, const char* title)\
	BACKEND_FUNC(void, destroyWindow, Window window)\

#define BACKEND_FUNC(type, name, ...) type (*name) (__VA_ARGS__);
typedef struct PlatformBackendPlugin {
	Plugin* plugin;
    LIST_OF_BACKEND_FUNCTIONS // main functions always required
	// define optional functions here
} PlatformBackendPlugin;
#undef BACKEND_FUNC

static PlatformBackendPlugin backend = {0};

__attribute__((unused))
static void* platform_state;

bool32 isWayland(){
    const char *xdg_session_type = getenv("XDG_SESSION_TYPE");
    if (xdg_session_type && strcmp(xdg_session_type, "wayland") == 0) return true;
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (wayland_display) return true;
    return false;
}

bool32 isX11(){
    return getenv("DISPLAY") != NULL;
}

static inline bool8 loadSymbol(void** dest, Plugin* plugin, const char* name){
	void* sym = platformLoadPluginSymbol(plugin, name);
	if(sym == NULL){
		avError("Failed to load symbol %s", name);
		return false;
	}
	*dest = sym;
	return true;
}

bool8 platformSystemStartup(uint64* memoryRequirement, void* state, void* config) {
	if(backend.plugin==NULL){
		if (isWayland()) {
			loadPlugin(wayland_backend, backend.plugin);
			if(backend.plugin==NULL) return false;
			#define BACKEND_FUNC(type, name, ...) if(!loadSymbol((void**)&backend.name, backend.plugin, "waylandBackend_"#name)) return false;
			LIST_OF_BACKEND_FUNCTIONS
			#undef BACKEND_FUNC
		} else if (isX11()) {
			return false;
		} else {
			return false;
		}
	}
	return backend.startup(memoryRequirement, state, config);
}

void platformGetVulkanRequiredExtensionNames(const char*** namesDarray){
	backend.getVulkanRequiredExtensions(namesDarray);
}
bool32 platformCreateVulkanSurface(void* vkInstance, void* surface, Window window){
	return backend.createVulkanSurface(vkInstance, surface, window);
}

AV_API Window platformCreateWindow(uint32* x, uint32* y, uint32* width, uint32* height, const char* title){
	return backend.createWindow(x, y, width, height, title);
}
AV_API void platformDestroyWindow(Window window){
	return backend.destroyWindow(window);
}


void platformSystemShutdown(void* platformState){
	backend.shutdown(platformState);

	platformUnloadPlugin(backend.plugin);
}

bool8 platformPumpMessages(void){
	return false;
}

double platformGetAbsoluteTime(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

AV_API void platformSleep(uint64 ms){
	usleep(ms*1000);
}

AV_API uint64 platformGetPageSize(){
	long result = sysconf(_SC_PAGESIZE);
	return (result > 0) ? (uint64)result : 4096;
}

AV_API uint64 platformGetAllocationGranularity(){
	return platformGetPageSize();
}

AV_API MemoryRegion platformReserveMemory(uint64 size){
	MemoryRegion region = {0};
	void* ptr = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(ptr != MAP_FAILED){
		region.ptr = ptr;
		region.size = size;
	}
	return region;
}
AV_API bool32 platformCommitMemory(void* ptr, uint64 size){
	int result = mprotect(ptr, size, PROT_READ | PROT_WRITE);
	if(result != 0){
		return false;
	}
	return true;
}
AV_API bool32 platformDecommitMemory(void* ptr, uint64 size){
	if(mprotect(ptr, size, PROT_NONE) != 0){
		return false;
	}
	madvise(ptr, size, MADV_DONTNEED);
	return true;
}
AV_API void platformReleaseMemory(MemoryRegion region){
	if(region.ptr && region.size){
		munmap(region.ptr, region.size);
	}
}

struct Plugin {
	void* lib;
};

AV_API Plugin* platformLoadPlugin(const char* name, const void* data, uint64 size){
	int fd = memfd_create(name, MFD_CLOEXEC | MFD_EXEC);
	if(fd < 0) return NULL;
	uint64 bytesToWrite = size;
	uint64 bytesWritten = 0;
	while(bytesWritten < bytesToWrite){
		uint64 remaining = bytesToWrite - bytesWritten;
		int64 writtenBytes = write(fd, (const char*)data +  bytesWritten, remaining);
		if(writtenBytes < 0){
			close(fd);
			return NULL;
		}
		bytesWritten += writtenBytes;
	}

	lseek(fd, 0, SEEK_SET);
	char path[64] = {0};
	avStringPrintfToBuffer(path, sizeof(path), AV_CSTRA("/proc/%u/fd/%i"), getpid(), fd);
	
	
	Plugin* plugin = (Plugin*) dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if(plugin == NULL){
		close(fd);
		avFree(plugin);
		return NULL;
	}
	return plugin;
}
AV_API void* platformLoadPluginSymbol(Plugin* plugin, const char* symbol){
	return dlsym(plugin, symbol);
}
AV_API void platformUnloadPlugin(Plugin* plugin){
	dlclose(plugin);
}

#endif
