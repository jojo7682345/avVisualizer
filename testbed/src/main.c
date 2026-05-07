#include <entry.h>
#include <core/systems/ecs.h>
#include <core/systems/jobs.h>
#include <core/systems/io.h>

extern bool8 renderFrameCpp(struct EngineConfig*);

extern bool8 initializeCpp(struct EngineConfig*);

Scene scene = {0};

ComponentType COMPONENT_TYPE_FOO = INVALID_COMPONENT;
ComponentType COMPONENT_TYPE_BAR = 5;
ComponentType COMPONENT_TYPE_BAZ = INVALID_COMPONENT;

typedef struct FooComponent {
    uint32 a;
} FooComponent;

typedef struct BarComponent {
    uint32 a;
    uint32 b;
} BarComponent;

void fooConstructor(Scene scene, Entity entity, void* component, uint32 size, byte* data, uint32 dataSize){
    ((FooComponent*)component)->a = 2;
    avLog(AV_DEBUG, "Initialized foo component of entity %x, with %u", entity, ((FooComponent*)component)->a);
}
void fooDestructor(Scene scene, Entity entity, void* component, uint32 size){
    avLog(AV_DEBUG, "Uninitialized foo component of entity %x, with %u", entity, ((FooComponent*)component)->a);
}

void barConstructor(Scene scene, Entity entity, void* component, uint32 size, byte* data, uint32 dataSize){
    ((BarComponent*)component)->a = 2;
    ((BarComponent*)component)->b = 3;
    avLog(AV_DEBUG, "Initialized bar component of entity %x, with %u %u", entity, ((BarComponent*)component)->a, ((BarComponent*)component)->b);
}
void barDestructor(Scene scene, Entity entity, void* component, uint32 size){
    avLog(AV_DEBUG, "Uninitialized bar component of entity %x, with %u %u", entity, ((BarComponent*)component)->a, ((BarComponent*)component)->b);
}

volatile uint32 completedWork[12] = {0, 0};

#include <AvUtils/avThreading.h>
JobControl exampleJob(byte* input, uint32 inputSize, byte* output, uint32 outputSize, JobContext* context){
    //avDebug("Hello from worker thread %u", context->threadId);
    for(uint32 i = 0; i < 10000000; i++){
    }
    //avThreadSleep(10);
    completedWork[context->threadId]++;
    //avDebug("Thread %u completed %u jobs", context->threadId, completedWork[context->threadId]);
    JOB_EXIT();
}


JobControl exampleSystemProcess(Scene scene, void* ctx, uint32 entityCount, const Entity* entities, const ComponentData* components, JobContext* context){
    for(uint32 i = 0; i < entityCount; i++){
        avDebug("System for %x", entities[i]);
    }
    JOB_EXIT();
} 

JobControl exampleIoProcess(const void *data, uint64 dataSize, void *ctx, JobContext *context) {
    avDebug("Loaded %llu bytes", dataSize);
    //avDebug("%s", data);
    JOB_EXIT();
}

void exampleIoComplete(IoResult result, const void* data, uint64 dataSize, void* ctx){
    avDebug("Completed with result %u", result);
}

bool8 initialize(struct EngineConfig* app){
    initializeCpp(app);

    // initialize
    scene = sceneCreate();
    registerComponent(&COMPONENT_TYPE_FOO, sizeof(FooComponent), fooConstructor, fooDestructor);
    registerComponent(&COMPONENT_TYPE_BAR, sizeof(BarComponent), barConstructor, barDestructor);
    registerComponent(&COMPONENT_TYPE_BAZ, 0, NULL, NULL);


    SelectionAccessCriteria testSelection = {
        .requiredRead = componentMaskMake(COMPONENT_TYPE_FOO),
        .requiredWrite = componentMaskMake(COMPONENT_TYPE_BAR),
        .excluded = {0},
    };
    EcsSystemID testSystem = createSystem(scene, testSelection, SYSTEM_EXECUTE_ASYNC, exampleSystemProcess, NULL);

    sceneSetSystemsOrder(scene, 1, &testSystem);

    Entity foo = entityCreate(scene);
    Entity bar = entityCreate(scene);

    entityAddComponent(scene, foo, COMPONENT_TYPE_FOO, NULL, 0);
    entityAddComponent(scene, foo, COMPONENT_TYPE_BAR, NULL, 0);
    entityAddComponent(scene, bar, COMPONENT_TYPE_BAR, NULL, 0);

    // for(uint32 i = 0; i < 25; i++){
    //     Entity e = entityCreate(scene);
    //     entityAddComponent(scene, e, COMPONENT_TYPE_FOO, NULL, 0);
    //     entityAddComponent(scene, e, COMPONENT_TYPE_BAR, NULL, 0);
    // }

    //entityRemoveComponent(scene, a, COMPONENT_TYPE_FOO);
    //entityRemoveComponentType(scene, a, COMPONENT_TYPE_FOO);

    avLog(AV_DEBUG, "Entity %x has %u foo components", foo, entityHasComponent(scene, foo, COMPONENT_TYPE_FOO));

    sceneApply(scene);

    
    avLog(AV_DEBUG, "Entity %x has %u foo components", foo, entityHasComponent(scene, foo, COMPONENT_TYPE_FOO));
    
    sceneRunSystems(scene, NULL);

    submitIoRead("./avVisualizer.project", exampleIoProcess, exampleIoComplete, NULL); // wastefull
    
    JobFence fence; 
    jobFenceCreate(&fence);

    JobBatchDescription batch = {
        .size = 4096,
        .entry = exampleJob,
        .flags.priority = JOB_PRIORITY_MAX,
    };
    JobBatchID id = submitJobBatch(&batch, fence);
    //wait untill id is already completed;
    jobFenceWait(fence);

    //JobBatchID id2 = submitJobBatch(&batch, NULL);
    JobBatchID ids[2] = {id, 0};
    submitJobBatchWithDependencies(&batch, 1, ids, NULL);

    //jobFenceWait(fence);
    jobFenceDestroy(fence);



    return true;
}


void onResize(struct EngineConfig* app_inst, uint32 width, uint32 height){

}

void shutdown(struct EngineConfig* app_inst){
    sceneDestroy(scene);
}

const char* disabledLogCategories[] = {
	//"avixel",
	//"avixel_core",
	//"avixel_renderer"
};
uint disabledLogCategoryCount = sizeof(disabledLogCategories) / sizeof(const char*);

AvResult disabledLogMessages[] = {
	//AV_DEBUG_CREATE,
	//AV_DEBUG_DESTROY,
};
uint disabledLogMessageCount = sizeof(disabledLogMessages) / sizeof(AvResult);

// Define the function to create a game
bool8 configureEngine(EngineConfig* out_application) {

    // AvLogSettings logSettings = avLogSettingsDefault;
	// logSettings.printSuccess = false;
	// logSettings.printCode = false;
	// logSettings.printAssert = false;
	// logSettings.printType = true;
	// logSettings.printFunc = false;
	// logSettings.printError = true;
	// logSettings.printCategory = false;
	// logSettings.colors = true;
	// logSettings.disabledCategories = disabledLogCategories;
	// logSettings.disabledCategoryCount = disabledLogCategoryCount;
	// logSettings.disabledMessages = disabledLogMessages;
	// logSettings.disabledMessageCount = disabledLogMessageCount;
	// logSettings.validationLevel = AV_VALIDATION_LEVEL_WARNINGS_AND_ERRORS;
	// logSettings.level = AV_LOG_LEVEL_ALL;
    //out_application->logSettings = &logSettings;

    // Application configuration.
    out_application->appConfig.startPosX = 100;
    out_application->appConfig.startPosY = 100;
    out_application->appConfig.startWidth = 1280;
    out_application->appConfig.startHeight = 720;
    out_application->appConfig.name = "AvVisualizer TestBed!";

    out_application->initialize = initialize;
    out_application->onResize = onResize;
    out_application->shutdown = shutdown;
    return true;
}