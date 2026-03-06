#include "shaders.h"
#include <AvUtils/avMemory.h>
#include <vulkan/vulkan.h>
#include <shaderc/shaderc.h>
#include "spirv-reflect/spirv_reflect.h"
#include <stdio.h>
typedef shaderc_shader_kind ShaderType;

typedef struct ShaderInterfaceVar {
    char name[256];
    uint32 location;
    VkFormat format;
    uint32 size;
}ShaderInterfaceVar;

typedef struct ShaderDescriptorBinding {
    uint32 set;
    uint32 binding;
    VkDescriptorType type;
    uint32 count;
    VkShaderStageFlags stages;
} ShaderDescriptorBinding;

typedef struct ShaderPushConstant {
    char name[256];
    uint32 offset;
    uint32 size;
    uint32 typeHash;
}ShaderPushConstant;

typedef struct ShaderStage {
    ShaderStageType type;
    byte* bytes;
    uint64 size;
    struct ShaderStage* next;
}ShaderStage;

typedef struct Shader_T{
    ShaderStage* entry;


} Shader_T;

typedef struct DarrayHeader {
	size_t capacity;
	size_t count;
} DarrayHeader;
#define ARRAY_INIT_CAPACITY 1
#define darrayPush(array, x)\
do{																									\
	if(array == NULL) {																				\
		DarrayHeader* header = malloc(sizeof(*(array))*ARRAY_INIT_CAPACITY + sizeof(DarrayHeader));	\
		header->count = 0;																			\
		header->capacity = ARRAY_INIT_CAPACITY;														\
		array = (void*)(header + 1);																\
	}																								\
	DarrayHeader* header = (DarrayHeader*)(array) - 1;												\
	if(header->count >= header->capacity){															\
		header->capacity *= 2;																		\
		header = realloc(header, sizeof(*(array))*header->capacity + sizeof(DarrayHeader));			\
		array = (void*)(header + 1);																\
	}																								\
	(array)[header->count++] = (x);																	\
} while(0)																							\

#define darrayLength(array) ((DarrayHeader*)(array) -1)->count
#define darrayFree(array) free((DarrayHeader*)(array) - 1)

static uint32_t FormatSize(VkFormat format) {
  uint32_t result = 0;
  switch (format) {
    case VK_FORMAT_UNDEFINED:
      result = 0;
      break;
    case VK_FORMAT_R4G4_UNORM_PACK8:
      result = 1;
      break;
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
      result = 2;
      break;
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
      result = 2;
      break;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
      result = 2;
      break;
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
      result = 2;
      break;
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
      result = 2;
      break;
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
      result = 2;
      break;
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
      result = 2;
      break;
    case VK_FORMAT_R8_UNORM:
      result = 1;
      break;
    case VK_FORMAT_R8_SNORM:
      result = 1;
      break;
    case VK_FORMAT_R8_USCALED:
      result = 1;
      break;
    case VK_FORMAT_R8_SSCALED:
      result = 1;
      break;
    case VK_FORMAT_R8_UINT:
      result = 1;
      break;
    case VK_FORMAT_R8_SINT:
      result = 1;
      break;
    case VK_FORMAT_R8_SRGB:
      result = 1;
      break;
    case VK_FORMAT_R8G8_UNORM:
      result = 2;
      break;
    case VK_FORMAT_R8G8_SNORM:
      result = 2;
      break;
    case VK_FORMAT_R8G8_USCALED:
      result = 2;
      break;
    case VK_FORMAT_R8G8_SSCALED:
      result = 2;
      break;
    case VK_FORMAT_R8G8_UINT:
      result = 2;
      break;
    case VK_FORMAT_R8G8_SINT:
      result = 2;
      break;
    case VK_FORMAT_R8G8_SRGB:
      result = 2;
      break;
    case VK_FORMAT_R8G8B8_UNORM:
      result = 3;
      break;
    case VK_FORMAT_R8G8B8_SNORM:
      result = 3;
      break;
    case VK_FORMAT_R8G8B8_USCALED:
      result = 3;
      break;
    case VK_FORMAT_R8G8B8_SSCALED:
      result = 3;
      break;
    case VK_FORMAT_R8G8B8_UINT:
      result = 3;
      break;
    case VK_FORMAT_R8G8B8_SINT:
      result = 3;
      break;
    case VK_FORMAT_R8G8B8_SRGB:
      result = 3;
      break;
    case VK_FORMAT_B8G8R8_UNORM:
      result = 3;
      break;
    case VK_FORMAT_B8G8R8_SNORM:
      result = 3;
      break;
    case VK_FORMAT_B8G8R8_USCALED:
      result = 3;
      break;
    case VK_FORMAT_B8G8R8_SSCALED:
      result = 3;
      break;
    case VK_FORMAT_B8G8R8_UINT:
      result = 3;
      break;
    case VK_FORMAT_B8G8R8_SINT:
      result = 3;
      break;
    case VK_FORMAT_B8G8R8_SRGB:
      result = 3;
      break;
    case VK_FORMAT_R8G8B8A8_UNORM:
      result = 4;
      break;
    case VK_FORMAT_R8G8B8A8_SNORM:
      result = 4;
      break;
    case VK_FORMAT_R8G8B8A8_USCALED:
      result = 4;
      break;
    case VK_FORMAT_R8G8B8A8_SSCALED:
      result = 4;
      break;
    case VK_FORMAT_R8G8B8A8_UINT:
      result = 4;
      break;
    case VK_FORMAT_R8G8B8A8_SINT:
      result = 4;
      break;
    case VK_FORMAT_R8G8B8A8_SRGB:
      result = 4;
      break;
    case VK_FORMAT_B8G8R8A8_UNORM:
      result = 4;
      break;
    case VK_FORMAT_B8G8R8A8_SNORM:
      result = 4;
      break;
    case VK_FORMAT_B8G8R8A8_USCALED:
      result = 4;
      break;
    case VK_FORMAT_B8G8R8A8_SSCALED:
      result = 4;
      break;
    case VK_FORMAT_B8G8R8A8_UINT:
      result = 4;
      break;
    case VK_FORMAT_B8G8R8A8_SINT:
      result = 4;
      break;
    case VK_FORMAT_B8G8R8A8_SRGB:
      result = 4;
      break;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2R10G10B10_SINT_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
      result = 4;
      break;
    case VK_FORMAT_A2B10G10R10_SINT_PACK32:
      result = 4;
      break;
    case VK_FORMAT_R16_UNORM:
      result = 2;
      break;
    case VK_FORMAT_R16_SNORM:
      result = 2;
      break;
    case VK_FORMAT_R16_USCALED:
      result = 2;
      break;
    case VK_FORMAT_R16_SSCALED:
      result = 2;
      break;
    case VK_FORMAT_R16_UINT:
      result = 2;
      break;
    case VK_FORMAT_R16_SINT:
      result = 2;
      break;
    case VK_FORMAT_R16_SFLOAT:
      result = 2;
      break;
    case VK_FORMAT_R16G16_UNORM:
      result = 4;
      break;
    case VK_FORMAT_R16G16_SNORM:
      result = 4;
      break;
    case VK_FORMAT_R16G16_USCALED:
      result = 4;
      break;
    case VK_FORMAT_R16G16_SSCALED:
      result = 4;
      break;
    case VK_FORMAT_R16G16_UINT:
      result = 4;
      break;
    case VK_FORMAT_R16G16_SINT:
      result = 4;
      break;
    case VK_FORMAT_R16G16_SFLOAT:
      result = 4;
      break;
    case VK_FORMAT_R16G16B16_UNORM:
      result = 6;
      break;
    case VK_FORMAT_R16G16B16_SNORM:
      result = 6;
      break;
    case VK_FORMAT_R16G16B16_USCALED:
      result = 6;
      break;
    case VK_FORMAT_R16G16B16_SSCALED:
      result = 6;
      break;
    case VK_FORMAT_R16G16B16_UINT:
      result = 6;
      break;
    case VK_FORMAT_R16G16B16_SINT:
      result = 6;
      break;
    case VK_FORMAT_R16G16B16_SFLOAT:
      result = 6;
      break;
    case VK_FORMAT_R16G16B16A16_UNORM:
      result = 8;
      break;
    case VK_FORMAT_R16G16B16A16_SNORM:
      result = 8;
      break;
    case VK_FORMAT_R16G16B16A16_USCALED:
      result = 8;
      break;
    case VK_FORMAT_R16G16B16A16_SSCALED:
      result = 8;
      break;
    case VK_FORMAT_R16G16B16A16_UINT:
      result = 8;
      break;
    case VK_FORMAT_R16G16B16A16_SINT:
      result = 8;
      break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      result = 8;
      break;
    case VK_FORMAT_R32_UINT:
      result = 4;
      break;
    case VK_FORMAT_R32_SINT:
      result = 4;
      break;
    case VK_FORMAT_R32_SFLOAT:
      result = 4;
      break;
    case VK_FORMAT_R32G32_UINT:
      result = 8;
      break;
    case VK_FORMAT_R32G32_SINT:
      result = 8;
      break;
    case VK_FORMAT_R32G32_SFLOAT:
      result = 8;
      break;
    case VK_FORMAT_R32G32B32_UINT:
      result = 12;
      break;
    case VK_FORMAT_R32G32B32_SINT:
      result = 12;
      break;
    case VK_FORMAT_R32G32B32_SFLOAT:
      result = 12;
      break;
    case VK_FORMAT_R32G32B32A32_UINT:
      result = 16;
      break;
    case VK_FORMAT_R32G32B32A32_SINT:
      result = 16;
      break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
      result = 16;
      break;
    case VK_FORMAT_R64_UINT:
      result = 8;
      break;
    case VK_FORMAT_R64_SINT:
      result = 8;
      break;
    case VK_FORMAT_R64_SFLOAT:
      result = 8;
      break;
    case VK_FORMAT_R64G64_UINT:
      result = 16;
      break;
    case VK_FORMAT_R64G64_SINT:
      result = 16;
      break;
    case VK_FORMAT_R64G64_SFLOAT:
      result = 16;
      break;
    case VK_FORMAT_R64G64B64_UINT:
      result = 24;
      break;
    case VK_FORMAT_R64G64B64_SINT:
      result = 24;
      break;
    case VK_FORMAT_R64G64B64_SFLOAT:
      result = 24;
      break;
    case VK_FORMAT_R64G64B64A64_UINT:
      result = 32;
      break;
    case VK_FORMAT_R64G64B64A64_SINT:
      result = 32;
      break;
    case VK_FORMAT_R64G64B64A64_SFLOAT:
      result = 32;
      break;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
      result = 4;
      break;
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
      result = 4;
      break;

    default:
      break;
  }
  return result;
}

static shaderc_shader_kind translateShaderStage(ShaderStageType type){
    shaderc_shader_kind shaderStage;
    switch(type){
        case SHADER_STAGE_VERTEX: return shaderc_vertex_shader;
        case SHADER_STAGE_TESSELATION_CONTROL: return shaderc_tess_control_shader;
        case SHADER_STAGE_TESSELATION_EVALUATION: return shaderc_tess_evaluation_shader;
        case SHADER_STAGE_GEOMETRY: return shaderc_geometry_shader;
        case SHADER_STAGE_FRAGMENT: return shaderc_fragment_shader;
        case SHADER_STAGE_COMPUTE: return shaderc_compute_shader;
        default: return -1;
    }
}

static bool32 enumerateShaderDataCounts(SpvReflectShaderModule* module, uint32* inputCount, uint32* outputCount, uint32* pushConstCount, uint32* descriptorSetCount){
    SpvReflectResult result;
    result = spvReflectEnumerateInputVariables(&module, inputCount, NULL);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    result = spvReflectEnumerateOutputVariables(&module, outputCount, NULL);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    result = spvReflectEnumeratePushConstantBlocks(&module, pushConstCount, NULL);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    result = spvReflectEnumerateDescriptorSets(&module, descriptorSetCount, NULL);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    return true;
}

static bool32 enumerateShaderData(SpvReflectShaderModule* module, SpvReflectInterfaceVariable** inputVars, SpvReflectInterfaceVariable** outputVars, SpvReflectBlockVariable** pushConstBlocks, SpvReflectDescriptorSet** descriptorSets){
    SpvReflectResult result;
    uint32 inputCount;
    uint32 outputCount;
    uint32 pushConstCount;
    uint32 descriptorSetCount;
    result = spvReflectEnumerateInputVariables(&module, &inputCount, inputVars);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    result = spvReflectEnumerateOutputVariables(&module, &outputCount, outputVars);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    result = spvReflectEnumeratePushConstantBlocks(&module, &pushConstCount, pushConstBlocks);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    result = spvReflectEnumerateDescriptorSets(&module, &descriptorSetCount, descriptorSets);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    return true;
}

static void getPushConstants(const SpvReflectBlockVariable* block, ShaderPushConstant** pushConsts, const char* parentName){

    char qualifiedName[256];
    for(uint32 i = 0; i < block->member_count; i++){
        const SpvReflectBlockVariable* member = &block->members[i];
        if(parentName){
            snprintf(qualifiedName, 256, "%s.%s", parentName, member->name);
        }else{
            snprintf(qualifiedName, 256, "%s", member->name);
        }

        if(member->member_count > 0){
            getPushConstants(member, pushConsts, qualifiedName);
        }else{
            ShaderPushConstant m;
            avMemcpy(m.name, qualifiedName, 255);
            m.name[255] = 0;
            m.offset = member->offset;
            m.size = member->size;
            m.typeHash = member->type_description ? member->type_description->type_flags : 0;
            darrayPush(*pushConsts, m);
        }
    }

}

bool32 processShaderStage(ShaderStage* stage){

    SpvReflectShaderModule module = {0};
    SpvReflectResult result = spvReflectCreateShaderModule(stage->size, stage->bytes, &module);
    if(result != SPV_REFLECT_RESULT_SUCCESS){
        return false;
    }
    
    uint32 inputCount;
    uint32 outputCount;
    uint32 pushConstCount;
    uint32 descriptorSetCount;

    if(!enumerateShaderDataCounts(&module, &inputCount, &outputCount, &pushConstCount, &descriptorSetCount)){
        spvReflectDestroyShaderModule(&module);
        return false;
    }

    SpvReflectInterfaceVariable** inputVars;
    SpvReflectInterfaceVariable** outputVars;
    SpvReflectBlockVariable** pushConstBlocks;
    SpvReflectDescriptorSet** descriptorSets;
    uint64 allocSize = sizeof(*inputVars)*inputCount + sizeof(*outputVars)*outputCount + sizeof(*pushConstBlocks)*pushConstCount + sizeof(*descriptorSets)*descriptorSetCount;

    if(allocSize == 0){
        spvReflectDestroyShaderModule(&module);
        return false;
    }

    inputVars = avAllocate(allocSize, "");
    outputVars = inputVars + inputCount;
    pushConstBlocks = outputVars + outputCount;
    descriptorSets = pushConstCount + pushConstCount;

    if(!enumerateShaderData(&module, inputVars, outputVars, pushConstBlocks, descriptorSets)){
        spvReflectDestroyShaderModule(&module);
        avFree(inputVars);
        return false;
    }
    
    ShaderInterfaceVar* shaderInputs = NULL;
    ShaderInterfaceVar* shaderOutputs = NULL;
    ShaderPushConstant* pushConstants = NULL;
    ShaderDescriptorBinding* descriptors = NULL;
    for(uint32 i = 0; i < inputVars; i++){
        SpvReflectInterfaceVariable* input = inputVars[i];
        if(input->built_in){
            continue;
        }
        if(input->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN){
            continue;
        }
        ShaderInterfaceVar in = {0};
        in.location = input->location;
        in.format = (VkFormat)input->format;
        in.size = FormatSize((VkFormat)input->format);
        snprintf(in.name, 256, "%s", input->name);
        darrayPush(shaderInputs, in);
    }
    for(uint32 i = 0; i < outputVars; i++){
        SpvReflectInterfaceVariable* output = outputVars[i];
        if(output->built_in){
            continue;
        }
        if(output->decoration_flags * SPV_REFLECT_DECORATION_BUILT_IN){
            continue;
        }
        ShaderInterfaceVar out = {0};
        out.location = output->location;
        out.format = (VkFormat)output->format;
        out.size = FormatSize((VkFormat)output->format);
        darrayPush(shaderOutputs, out);
    }
    
    for(uint32 i = 0; i < pushConstCount; i++){
        SpvReflectBlockVariable* pushConstant = pushConstBlocks[i];
        getPushConstants(pushConstant, pushConstants, pushConstant->name);
    }

    for(uint32 i = 0; i < descriptorSetCount; i++){
        SpvReflectDescriptorSet* descriptor = descriptorSets[i];
        spvReflectEnumerate
    }

    avFree(inputVars);
    spvReflectDestroyShaderModule(&module);
}   

void processShaderChain(ShaderStage* vertex){





}

bool32 loadShader(ShaderLoadInfo info, Shader* shader){
    shaderc_optimization_level level;
    switch(info.optimization){
        case SHADER_OPTIMIZATION_O0:
            level = shaderc_optimization_level_zero;
            break;
        case SHADER_OPTIMIZATION_O1:
            level = shaderc_optimization_level_performance;
            break;
        case SHADER_OPTIMIZATION_Oz:
            level = shaderc_optimization_level_size;
            break;
        default:
            return false;
    }

    if(info.shaderStageCount==0 || info.shaderStages==NULL){
        return false;
    }

    uint32 shaderStageMask = 0;

    for(uint32 i = 0; i < info.shaderStageCount; i++){
        ShaderStageLoadInfo stageInfo = info.shaderStages[i];
        if(shaderStageMask & stageInfo.stage){
            return false;
        }
        shaderStageMask |= stageInfo.stage;
    }

    if(shaderStageMask & SHADER_STAGE_COMPUTE && (shaderStageMask & ~SHADER_STAGE_COMPUTE)){
        return false;
    }

    ShaderStage* vertex = NULL;
    ShaderStage* tessControl = NULL;
    ShaderStage* tessEval = NULL;
    ShaderStage* geometry = NULL;
    ShaderStage* fragment = NULL;
    ShaderStage* compute = NULL;
   

    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();
    shaderc_compile_options_set_optimization_level(options, level);
    bool32 success = true;
    for(uint32 i = 0; i < info.shaderStageCount; i++){
        ShaderStageLoadInfo stageInfo = info.shaderStages[i];
        shaderc_shader_kind shaderStage = translateShaderStage(stageInfo.stage);
        if(shaderStage==-1){
            success = false;
            continue;
        }

        shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, stageInfo.code, stageInfo.codeSize, shaderStage, stageInfo.fileName, stageInfo.entry, options);

        shaderc_compilation_status status = shaderc_result_get_compilation_status(result);
        if(status != shaderc_compilation_status_success){
            uint64 numWarnings = shaderc_result_get_num_warnings(result);
            uint64 numErrors = shaderc_result_get_num_errors(result);
            success = false;
            shaderc_result_release(result);
            continue;
        }

        const char* bytes = shaderc_result_get_bytes(result);
        uint64 length = shaderc_result_get_length(result);

        if(length == 0){
            success = false;
        }
        ShaderStage** stage = NULL;
        switch(stageInfo.stage){
            case SHADER_STAGE_VERTEX: stage = &vertex; break;
            case SHADER_STAGE_TESSELATION_CONTROL: stage = &tessControl; break;
            case SHADER_STAGE_TESSELATION_EVALUATION: stage = &tessEval; break;
            case SHADER_STAGE_GEOMETRY: stage = &geometry; break;
            case SHADER_STAGE_FRAGMENT: stage = &fragment; break;
            case SHADER_STAGE_COMPUTE: stage = &compute; break;
            default: success = false; break;
        }
        if(stage != NULL){
            (*stage) = avAllocate(sizeof(ShaderStage) + length, "ShaderStage");
            avMemset((*stage), 0, sizeof(ShaderStage) + length);
            (*stage)->type = stageInfo.stage;
            (*stage)->bytes = (byte*)((*stage) + 1);
            (*stage)->size = length;
            avMemcpy((*stage)->bytes, bytes, length);
        }

        shaderc_result_release(result);

        if(!success){
            break;
        }

        processShaderStage(*stage);
    }
    

    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);


    if(shaderStageMask & SHADER_STAGE_COMPUTE){

    }else{
        if(!vertex || !fragment){
            success = false;
        }
        if(tessControl && !tessEval){
            success = false;
        }
        if(tessEval && !tessControl){
            success = false;
        }
        if(tessControl){
            vertex->next = tessControl;
            tessControl->next = tessEval;
            if(geometry){
                tessEval->next = geometry;
            }else{
                tessEval->next = fragment;
            }
        }else if(geometry){
            vertex->next = geometry;
            geometry->next = fragment;
        }else {
            vertex->next = fragment;
        }
        processShaderChain(vertex);
    }

    if(success == false){
        if(vertex) avFree(vertex);
        if(tessControl) avFree(tessControl);
        if(tessEval) avFree(tessEval);
        if(geometry) avFree(geometry);
        if(fragment) avFree(fragment);
        if(compute) avFree(compute);
    }
    

    return success;

}

void unloadShader(Shader* shader){
    
}