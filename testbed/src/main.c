#include <entry.h>
#include <ecs/ecs.h>

extern bool8 renderFrameCpp(struct Application*);

extern bool8 initializeCpp(struct Application*);

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

void fooConstructor(Scene scene, Entity entity, void* component, uint32 size,  ComponentInfo* info){
    ((FooComponent*)component)->a = 2;
    avLog(AV_DEBUG, "Initialized foo component of entity %x, with %u", entity, ((FooComponent*)component)->a);
}
void fooDestructor(Scene scene, Entity entity, void* component, uint32 size){
    avLog(AV_DEBUG, "Uninitialized foo component of entity %x, with %u", entity, ((FooComponent*)component)->a);
}

void barConstructor(Scene scene, Entity entity, void* component, uint32 size, ComponentInfo* info){
    ((BarComponent*)component)->a = 2;
    ((BarComponent*)component)->b = 3;
    avLog(AV_DEBUG, "Initialized bar component of entity %x, with %u %u", entity, ((BarComponent*)component)->a, ((BarComponent*)component)->b);
}
void barDestructor(Scene scene, Entity entity, void* component, uint32 size){
    avLog(AV_DEBUG, "Uninitialized bar component of entity %x, with %u %u", entity, ((BarComponent*)component)->a, ((BarComponent*)component)->b);
}


bool8 initialize(struct Application* app){
    initializeCpp(app);

    scene = sceneCreate();
    registerComponent(&COMPONENT_TYPE_FOO, sizeof(FooComponent), fooConstructor, fooDestructor);
    registerComponent(&COMPONENT_TYPE_BAR, sizeof(BarComponent), barConstructor, barDestructor);
    registerComponent(&COMPONENT_TYPE_BAZ, 0, NULL, NULL);

    ComponentInfo bazInfo = {
        .type = COMPONENT_TYPE_BAZ,
        .next = NULL,
    };
    ComponentInfo barInfo = {
        .type = COMPONENT_TYPE_BAR,
        .next = &bazInfo,
    };
    ComponentInfo fooInfo = {
        .type = COMPONENT_TYPE_FOO,
        .next = &barInfo,
    };

    ComponentInfo singleFoo = {
        .type = COMPONENT_TYPE_FOO,
        .next = NULL,
    };

    Entity foo = entityCreate(scene, &fooInfo);
    Entity bar = entityCreate(scene, &fooInfo);
    Entity baz = entityCreate(scene, &fooInfo);

    Entity a = entityCreate(scene, &barInfo);

    entityDestroy(scene, foo);

    foo = entityCreate(scene, &fooInfo);

    entityAddComponent(scene, a, &singleFoo);
    entityAddComponent(scene, a, &singleFoo);

    entityRemoveComponent(scene, a, COMPONENT_TYPE_FOO, 0);
    //entityRemoveComponentType(scene, a, COMPONENT_TYPE_FOO);
    return true;
}

bool8 renderFrame(struct Application* app){
    renderFrameCpp(app);

    return true;
}

bool8 update(struct Application* app_inst){
    return true;
}

bool8 prepareFrame(struct Application* app_inst){
    return true;
}

void onResize(struct Application* app_inst, uint32 width, uint32 height){

}

void shutdown(struct Application* app_inst){
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
bool8 create_application(Application* out_application) {

    AvLogSettings logSettings = avLogSettingsDefault;
	logSettings.printSuccess = false;
	logSettings.printCode = false;
	logSettings.printAssert = false;
	logSettings.printType = true;
	logSettings.printFunc = false;
	logSettings.printError = true;
	logSettings.printCategory = false;
	logSettings.colors = true;
	logSettings.disabledCategories = disabledLogCategories;
	logSettings.disabledCategoryCount = disabledLogCategoryCount;
	logSettings.disabledMessages = disabledLogMessages;
	logSettings.disabledMessageCount = disabledLogMessageCount;
	logSettings.validationLevel = AV_VALIDATION_LEVEL_WARNINGS_AND_ERRORS;
	logSettings.level = AV_LOG_LEVEL_ALL;
    out_application->logSettings = &logSettings;

    // Application configuration.
    out_application->appConfig.startPosX = 100;
    out_application->appConfig.startPosY = 100;
    out_application->appConfig.startWidth = 1280;
    out_application->appConfig.startHeight = 720;
    out_application->appConfig.name = "AvVisualizer TestBed!";


    out_application->engineState = 0;
    out_application->state = 0;

    out_application->initialize = initialize;
    out_application->update = update;
    out_application->prepareFrame = prepareFrame;
    out_application->renderFrame = renderFrame;
    out_application->on_resize = onResize;
    out_application->shutdown = shutdown;


    return true;
}

bool8 initialize_application(Application* app) {


    return true;
}