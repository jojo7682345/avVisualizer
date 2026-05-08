#include "logging.h"
#include "containers/idMapping.h"
#include "../expect.h"
#include "stdlib.h"
struct MappingTestStruct {
    char content[400];
};

static void fillMappingTestStruct(struct MappingTestStruct* a){

    for(uint32 i = 0; i < sizeof(a->content)-1; i++){
        char c = (rand() % (('9'-'0')+1)) + '0';
        a->content[i] = c;
    }
    a->content[sizeof(a->content)-1] = '\0';
}

bool32 runIdMapTests(){
  
    struct MappingTestStruct* mapping;
    expect_to_be_true(MAPPING_CREATE(mapping, 32));

    struct MappingTestStruct data[4];
    for(uint32 i = 0; i < 4; i++){
        fillMappingTestStruct(&data[i]);
    }

    uint32 id = MAPPING_ADD(mapping, data[0]);
    expect_should_not_be((uint32)-1, id);

    struct MappingTestStruct* a = MAPPING_GET(mapping, id);
    expect_should_not_be(a, NULL);
    expect_string_to_be(data[0].content, a->content);
    
    uint32 ids[8];
    for(uint32 i = 0; i < 8; i++){
        uint32 id2 = MAPPING_ADD(mapping, data[i % 4]);
        expect_should_not_be((uint32)-1, id2);
        ids[i] = id2;

        struct MappingTestStruct* b = MAPPING_GET(mapping, ids[i]);
        expect_should_not_be(b, NULL);
        const char* AA = data[i % 4].content;
        const char* BB = b->content;
        expect_string_to_be(AA, BB);
    }

    expect_should_be(9, MAPPING_SIZE(mapping));

    expect_to_be_true(MAPPING_REMOVE(mapping, id));

    expect_should_be(NULL, MAPPING_GET(mapping, id));

    for(uint32 i = 0; i < 8; i++){
        struct MappingTestStruct* b = MAPPING_GET(mapping, ids[i]);
        expect_should_not_be(b, NULL);
        expect_string_to_be(data[i % 4].content, b->content);
    }


    expect_to_be_true(MAPPING_DESTROY(mapping));
    return true;
}

#define TEST_ENTRY(func) {.test = func, .name=#func }
struct {
    bool32 (*test)();
    const char* name; 
} tests[] = {
    TEST_ENTRY(runIdMapTests),
};

bool32 runTests(){
    bool32 failed = false;

    for(uint32 i = 0; i < sizeof(tests)/sizeof(tests[0]); i++){

        bool32 success = tests[i].test();
        if(success){
            avLog(AV_TEST_SUCCESS, "%s passed", tests[i].name);
        }else{
            avLog(AV_TEST_ERROR, "%s failed", tests[i].name);
            failed = true;
        }
    }

    
    return !failed;
}

int main(){
    AvLogConfig logConfig = {0};
    logConfig.queueSize = 4096;
    logConfig.assertLevel = AV_ASSERT_LEVEL_ERROR;
    logConfig.level = AV_LOG_LEVEL_ALL;
    logConfig.useColors = true;
    avLogInit(&logConfig);
    avLogAddSink(avLogConsoleSink, NULL);
    avLog(AV_SUCCESS, "Starting tests");
    if(runTests()){
        avLog(AV_SUCCESS, "Tests executed sucessfully");
    }else{
        avError("Some tests failed");
    }

    avLogShutdown();
    return 0;
}