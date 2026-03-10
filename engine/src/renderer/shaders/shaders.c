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

	uint64 layoutHash;
} ShaderDescriptorBinding;

typedef struct ShaderPushConstant {
	char name[256];
	uint32 offset;
	uint32 size;
	uint32 layoutHash;
}ShaderPushConstant;

typedef struct ShaderStage {
	ShaderStageType type;
	byte* bytes;
	uint64 size;
	struct ShaderStage* next;

	ShaderInterfaceVar* inputs;
	ShaderInterfaceVar* outputs;
	ShaderPushConstant* pushConstants;
	ShaderDescriptorBinding* descriptorBindings;
}ShaderStage;



typedef struct DarrayHeader {
	size_t capacity;
	size_t count;
} DarrayHeader;
#define ARRAY_INIT_CAPACITY 1
#define darrayPush(array, ...)\
do{																											\
	if(array == NULL) {																						\
		DarrayHeader* header = avAllocate(sizeof(*(array))*ARRAY_INIT_CAPACITY + sizeof(DarrayHeader),"");	\
		header->count = 0;																					\
		header->capacity = ARRAY_INIT_CAPACITY;																\
		array = (void*)(header + 1);																		\
	}																										\
	DarrayHeader* header = (DarrayHeader*)(array) - 1;														\
	if(header->count >= header->capacity){																	\
		header->capacity *= 2;																				\
		header = avReallocate(header, sizeof(*(array))*header->capacity + sizeof(DarrayHeader), "");		\
		array = (void*)(header + 1);																		\
	}																										\
	(array)[header->count++] = (__VA_ARGS__);																			\
} while(0)																									\

#define darrayLength(array) ((DarrayHeader*)(array) -1)->count
#define darrayFree(array) avFree((DarrayHeader*)(array) - 1)

#define VULKAN_FORMAT(format, size, components)
#define VULKAN_FORMAT_LIST \
    VULKAN_FORMAT(VK_FORMAT_UNDEFINED, 0, 0, false) \
    VULKAN_FORMAT(VK_FORMAT_R4G4_UNORM_PACK8, 1, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R4G4B4A4_UNORM_PACK16, 2, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B4G4R4A4_UNORM_PACK16, 2, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R5G6B5_UNORM_PACK16, 2, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B5G6R5_UNORM_PACK16, 2, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R5G5B5A1_UNORM_PACK16, 2, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B5G5R5A1_UNORM_PACK16, 2, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R8_UNORM, 1, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R8_SNORM, 1, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R8_USCALED, 1, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R8_SSCALED, 1, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R8_UINT, 1, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R8_SINT, 1, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R8_SRGB, 1, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8_UNORM, 2, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8_SNORM, 2, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8_USCALED, 2, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8_SSCALED, 2, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8_UINT, 2, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8_SINT, 2, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8_SRGB, 2, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8_UNORM, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8_SNORM, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8_USCALED, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8_SSCALED, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8_UINT, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8_SINT, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8_SRGB, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8_UNORM, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8_SNORM, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8_USCALED, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8_SSCALED, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8_UINT, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8_SINT, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8_SRGB, 3, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8A8_UNORM, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8A8_SNORM, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8A8_USCALED, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8A8_SSCALED, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8A8_UINT, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8A8_SINT, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R8G8B8A8_SRGB, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8A8_UNORM, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8A8_SNORM, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8A8_USCALED, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8A8_SSCALED, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8A8_UINT, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8A8_SINT, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_B8G8R8A8_SRGB, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A8B8G8R8_UNORM_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A8B8G8R8_SNORM_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A8B8G8R8_USCALED_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A8B8G8R8_SSCALED_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A8B8G8R8_UINT_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A8B8G8R8_SINT_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A8B8G8R8_SRGB_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2R10G10B10_UNORM_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2R10G10B10_SNORM_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2R10G10B10_USCALED_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2R10G10B10_SSCALED_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2R10G10B10_UINT_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2R10G10B10_SINT_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2B10G10R10_SNORM_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2B10G10R10_USCALED_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2B10G10R10_SSCALED_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2B10G10R10_UINT_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_A2B10G10R10_SINT_PACK32, 4, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R16_UNORM, 2, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R16_SNORM, 2, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R16_USCALED, 2, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R16_SSCALED, 2, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R16_UINT, 2, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R16_SINT, 2, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R16_SFLOAT, 2, 1, true) \
    VULKAN_FORMAT(VK_FORMAT_R16G16_UNORM, 4, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16_SNORM, 4, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16_USCALED, 4, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16_SSCALED, 4, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16_UINT, 4, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16_SINT, 4, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16_SFLOAT, 4, 2, true) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16_UNORM, 6, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16_SNORM, 6, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16_USCALED, 6, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16_SSCALED, 6, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16_UINT, 6, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16_SINT, 6, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16_SFLOAT, 6, 3, true) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16A16_UNORM, 8, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16A16_SNORM, 8, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16A16_USCALED, 8, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16A16_SSCALED, 8, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16A16_UINT, 8, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16A16_SINT, 8, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R16G16B16A16_SFLOAT, 8, 4, true) \
    VULKAN_FORMAT(VK_FORMAT_R32_UINT, 4, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R32_SINT, 4, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R32_SFLOAT, 4, 1, true) \
    VULKAN_FORMAT(VK_FORMAT_R32G32_UINT, 8, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R32G32_SINT, 8, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R32G32_SFLOAT, 8, 2, true) \
    VULKAN_FORMAT(VK_FORMAT_R32G32B32_UINT, 12, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R32G32B32_SINT, 12, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R32G32B32_SFLOAT, 12, 3, true) \
    VULKAN_FORMAT(VK_FORMAT_R32G32B32A32_UINT, 16, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R32G32B32A32_SINT, 16, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R32G32B32A32_SFLOAT, 16, 4, true) \
    VULKAN_FORMAT(VK_FORMAT_R64_UINT, 8, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R64_SINT, 8, 1, false) \
    VULKAN_FORMAT(VK_FORMAT_R64_SFLOAT, 8, 1, true) \
    VULKAN_FORMAT(VK_FORMAT_R64G64_UINT, 16, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R64G64_SINT, 16, 2, false) \
    VULKAN_FORMAT(VK_FORMAT_R64G64_SFLOAT, 16, 2, true) \
    VULKAN_FORMAT(VK_FORMAT_R64G64B64_UINT, 24, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R64G64B64_SINT, 24, 3, false) \
    VULKAN_FORMAT(VK_FORMAT_R64G64B64_SFLOAT, 24, 3, true) \
    VULKAN_FORMAT(VK_FORMAT_R64G64B64A64_UINT, 32, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R64G64B64A64_SINT, 32, 4, false) \
    VULKAN_FORMAT(VK_FORMAT_R64G64B64A64_SFLOAT, 32, 4, true) \
    VULKAN_FORMAT(VK_FORMAT_B10G11R11_UFLOAT_PACK32, 4, 3, true) \

#undef VULKAN_FORMAT
#define VULKAN_FORMAT(format, size, ...) case format: return size;
static uint32_t FormatSize(VkFormat format) {
	switch (format) {
		VULKAN_FORMAT_LIST
		default:
			return 0;
	}
}
#undef VULKAN_FORMAT
#define VULKAN_FORMAT(format, size, components, ...) case format: return components;
static uint32 FormatComponentCount(VkFormat f) {
    switch(f) {
		VULKAN_FORMAT_LIST
        default: return 0;
    }
}
#undef VULKAN_FORMAT
#define VULKAN_FORMAT(format, size, components, isFloat, ...) case format: return isFloat;
static bool32 FormatIsFloat(VkFormat f){
	switch(f){
		VULKAN_FORMAT_LIST
		default: return false;
	}
} 
#undef VULKAN_FORMAT

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

static VkShaderStageFlags translateVkShaderStage(ShaderStageType stageMask){
    return  (stageMask & SHADER_STAGE_VERTEX ?					VK_SHADER_STAGE_VERTEX_BIT 					: 0) |
            (stageMask & SHADER_STAGE_TESSELATION_CONTROL ? 	VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT	: 0) |
            (stageMask & SHADER_STAGE_TESSELATION_EVALUATION ? 	VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : 0) |
            (stageMask & SHADER_STAGE_GEOMETRY ? 				VK_SHADER_STAGE_GEOMETRY_BIT 				: 0) |
            (stageMask & SHADER_STAGE_FRAGMENT ? 				VK_SHADER_STAGE_FRAGMENT_BIT 				: 0) |
            (stageMask & SHADER_STAGE_COMPUTE ? 				VK_SHADER_STAGE_COMPUTE_BIT 				: 0);
}

static bool32 enumerateShaderDataCounts(SpvReflectShaderModule* module, uint32* inputCount, uint32* outputCount, uint32* pushConstCount, uint32* descriptorSetCount){
	SpvReflectResult result;
	result = spvReflectEnumerateInputVariables(module, inputCount, NULL);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	result = spvReflectEnumerateOutputVariables(module, outputCount, NULL);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	result = spvReflectEnumeratePushConstantBlocks(module, pushConstCount, NULL);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	result = spvReflectEnumerateDescriptorSets(module, descriptorSetCount, NULL);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	return true;
}

static bool32 enumerateShaderData(SpvReflectShaderModule* module, uint32* inputCount, uint32* outputCount, uint32* pushConstCount, uint32* descriptorSetCount, SpvReflectInterfaceVariable** inputVars, SpvReflectInterfaceVariable** outputVars, SpvReflectBlockVariable** pushConstBlocks, SpvReflectDescriptorSet** descriptorSets){
	SpvReflectResult result;
	result = spvReflectEnumerateInputVariables(module, inputCount, inputVars);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	result = spvReflectEnumerateOutputVariables(module, outputCount, outputVars);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	result = spvReflectEnumeratePushConstantBlocks(module, pushConstCount, pushConstBlocks);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	result = spvReflectEnumerateDescriptorSets(module, descriptorSetCount, descriptorSets);
	if(result != SPV_REFLECT_RESULT_SUCCESS){
		return false;
	}
	return true;
}

static uint64 hashCombine(uint64 h, uint64 v) {
	h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	return h;
}

static uint64 hashSpirvType(const SpvReflectTypeDescription* type)
{
	if(!type)
		return 0;

	uint64 h = 1469598103934665603ULL;

	h = hashCombine(h, type->type_flags);

	if(type->traits.numeric.scalar.width)
		h = hashCombine(h, type->traits.numeric.scalar.width);

	if(type->traits.numeric.vector.component_count)
		h = hashCombine(h, type->traits.numeric.vector.component_count);

	if(type->traits.numeric.matrix.column_count)
		h = hashCombine(h, type->traits.numeric.matrix.column_count);

	if(type->traits.numeric.matrix.row_count)
		h = hashCombine(h, type->traits.numeric.matrix.row_count);

	if(type->traits.array.dims_count)
	{
		for(uint32 i = 0; i < type->traits.array.dims_count; i++)
			h = hashCombine(h, type->traits.array.dims[i]);
	}

	if(type->member_count)
	{
		h = hashCombine(h, type->member_count);

		for(uint32 i = 0; i < type->member_count; i++)
		{
			const SpvReflectTypeDescription member = type->members[i];

			h = hashCombine(h, hashSpirvType(&member));
		}
	}

	return h;
}

static uint64 hashBlockLayout(const SpvReflectBlockVariable* block)
{
	uint64 h = 1469598103934665603ULL;

	h = hashCombine(h, block->size);
	h = hashCombine(h, block->member_count);

	for(uint32 i = 0; i < block->member_count; i++)
	{
		const SpvReflectBlockVariable* m = &block->members[i];

		h = hashCombine(h, m->offset);
		h = hashCombine(h, hashSpirvType(m->type_description));
	}

	return h;
}

static bool32 descriptorTypesEqual(
    const ShaderDescriptorBinding* a,
    const ShaderDescriptorBinding* b)
{
    if(a->type != b->type)
        return false;

    if(a->count != b->count)
        return false;

    if(a->layoutHash != b->layoutHash)
        return false;

    return true;
}

static void getPushConstants(const SpvReflectBlockVariable* block, ShaderPushConstant** pushConsts, const char* parentName, uint32 baseOffset){

	char qualifiedName[256];
	for(uint32 i = 0; i < block->member_count; i++){
		const SpvReflectBlockVariable* member = &block->members[i];

		const char* name = member->name ? member->name : "member";
		if(parentName){
			snprintf(qualifiedName, 256, "%s.%s", parentName, member->name);
		}else{
			snprintf(qualifiedName, 256, "%s", member->name);
		}

		uint32 offset = baseOffset + member->offset;
		if(member->member_count > 0){
			getPushConstants(member, pushConsts, qualifiedName, offset);
		}else{
			ShaderPushConstant m;
			avMemcpy(m.name, qualifiedName, 255);
			m.name[255] = 0;
			m.offset = offset;
			m.size = member->size;
			m.layoutHash = hashSpirvType(member->type_description);
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

	if(!enumerateShaderData(&module, &inputCount, &outputCount, &pushConstCount, &descriptorSetCount, 0, 0, 0, 0)){
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

	inputVars = (SpvReflectInterfaceVariable**) avAllocate(allocSize, "");
	outputVars = (SpvReflectInterfaceVariable**) (inputVars + inputCount);
	pushConstBlocks = (SpvReflectBlockVariable**) (outputVars + outputCount);
	descriptorSets = (SpvReflectDescriptorSet**) (pushConstBlocks + pushConstCount);

	if(!enumerateShaderData(&module, &inputCount, &outputCount, &pushConstCount, &descriptorSetCount, inputVars, outputVars, pushConstBlocks, descriptorSets)){
		spvReflectDestroyShaderModule(&module);
		avFree(inputVars);
		return false;
	}
	
	ShaderInterfaceVar* shaderInputs = NULL;
	ShaderInterfaceVar* shaderOutputs = NULL;
	ShaderPushConstant* pushConstants = NULL;
	ShaderDescriptorBinding* descriptors = NULL;
	for(uint32 i = 0; i < inputCount; i++){
		SpvReflectInterfaceVariable* input = inputVars[i];
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
	for(uint32 i = 0; i < outputCount; i++){
		SpvReflectInterfaceVariable* output = outputVars[i];
		if(output->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN){
			continue;
		}
		ShaderInterfaceVar out = {0};
		out.location = output->location;
		out.format = (VkFormat)output->format;
		out.size = FormatSize((VkFormat)output->format);
		snprintf(out.name, 256, "%s", output->name);
		darrayPush(shaderOutputs, out);
	}
	
	for(uint32 i = 0; i < pushConstCount; i++){
		SpvReflectBlockVariable* pushConstant = pushConstBlocks[i];
		getPushConstants(pushConstant, &pushConstants, pushConstant->name, 0);
	}

	for(uint32 i = 0; i < descriptorSetCount; i++) {
		SpvReflectDescriptorSet* set = descriptorSets[i];

		for(uint32 j = 0; j < set->binding_count; j++) {
			SpvReflectDescriptorBinding* b = set->bindings[j];

			ShaderDescriptorBinding binding = {0};

			binding.set     = b->set;
			binding.binding = b->binding;
			binding.count   = b->count;
			binding.type    = (VkDescriptorType)b->descriptor_type;

			if(b->block.type_description)
				binding.layoutHash = hashBlockLayout(&b->block);

			darrayPush(descriptors, binding);
		}
	}

	stage->inputs = shaderInputs;
	stage->outputs = shaderOutputs;
	stage->pushConstants = pushConstants;
	stage->descriptorBindings = descriptors;

	avFree(inputVars);
	spvReflectDestroyShaderModule(&module);
}   

bool32 validateShaderInterface(ShaderStage* vertex, ShaderStage* fragment){
	for(uint32 i = 0; i < darrayLength(fragment->inputs); i++){
		bool32 found = false;
		ShaderInterfaceVar input = fragment->inputs[i];
		for(uint32 j = 0; j < darrayLength(vertex->outputs); j++){
			ShaderInterfaceVar output = vertex->outputs[i];

			if(output.location != input.location){
				continue;
			}
			if(output.format != input.format){
				continue;
			}
			if(output.size != input.size){
				continue;
			}
			if(strncmp(output.name, input.name, 256)!=0){
				continue;
			}
			found = true;
		}
		if(!found){
			return false;
		}
	}
	return true;
}

ShaderLoadResult processShaderChain(ShaderStage* vertex){

	//vertex input

	ShaderStage* next = vertex->next;
	ShaderStage* current = vertex;
	while(next){
		if(!validateShaderInterface(current, next)){
			return SHADER_LOAD_ERROR_INTERFACE_INVALID;
		}
		current = next;
		next = current->next;
	}

	return SHADER_LOAD_SUCCESS;

}

ShaderLoadResult validateVertexInput(ShaderInterfaceVar* inputs, ShaderLoadInfo info){
	uint32 inputCount = darrayLength(inputs);
	uint32 attribCount = info.vertexInputAttributeCount;
	bool32 success = true;
	for(uint32 i = 0; i < inputCount; i++){
		ShaderInterfaceVar input = inputs[i];
		bool32 found = false;
		for(uint32 j = 0; j < attribCount; j++){
			VkVertexInputAttributeDescription attrib = info.vertexInputAttributes[j];
			if(attrib.location != input.location){
				continue;
			}
			found = true;
			if(attrib.format != input.format){
				success = false;
				break;
			}
			break;
		}
		if(!found){
			success = false;
		}
	}
	if(!success){
		return SHADER_LOAD_ERROR_INPUT_INVALID;
	}
	return SHADER_LOAD_SUCCESS;
}

bool32 formatsCompatible(VkFormat shaderFormat, VkFormat attachmentFormat){
	if (shaderFormat == attachmentFormat) {
        return true; // perfect match
    }
	// Get component counts and sizes
    uint32 shaderSize = FormatSize(shaderFormat);       // in bytes
    uint32 attachmentSize = FormatSize(attachmentFormat);

    // Basic size check
    if (shaderSize > attachmentSize){
        return false;
	}

	uint32 shaderComponents = FormatComponentCount(shaderFormat);
    uint32 attachmentComponents = FormatComponentCount(attachmentFormat);

	if(shaderComponents > attachmentComponents){
		return false;
	}

	if(FormatIsFloat(shaderFormat) != FormatIsFloat(attachmentFormat)){
		return false;
	}

	return true;
}

ShaderLoadResult validateFragmentOutput(ShaderInterfaceVar* outputs, ShaderLoadInfo info){
	uint32 outputCount = darrayLength(outputs);
	for(uint32 i = 0; i < outputCount; i++){
		ShaderInterfaceVar output = outputs[i];
		if(output.location >= info.subpass.colorAttachmentCount){
			return SHADER_LOAD_ERROR_OUTPUT_INVALID;
		}

		VkAttachmentReference ref = info.subpass.pColorAttachments[output.location];
		if(ref.attachment == VK_ATTACHMENT_UNUSED){
			return SHADER_LOAD_ERROR_OUTPUT_INVALID;
		}
		VkAttachmentDescription attachment = info.attachements[ref.attachment];
		if(!formatsCompatible(output.format, attachment.format)){
			return SHADER_LOAD_ERROR_OUTPUT_INVALID;
		}
	}
	return true;
}



ShaderLoadResult validatePushConstants(ShaderStage* entry, VkPushConstantRange* pcRange, ShaderLoadInfo info){
	struct PushConstantVariable {
		uint32 offset;
		uint32 size;
		ShaderStageType stageMask;
		uint64 layoutHash;
	};

	struct PushConstantVariable* pushConstants = NULL;
	ShaderStage* stage = entry;
	while(stage){
		for(uint32 i = 0; i < darrayLength(stage->pushConstants); i++){
			ShaderPushConstant pc = stage->pushConstants[i];
			bool32 found = false;
			for(uint32 j = 0; j < darrayLength(pushConstants); j++){
				struct PushConstantVariable var = pushConstants[j];
				if(
					(pc.offset < var.offset + var.size) &&
					(var.offset < pc.offset+pc.size)
				){
					if(pc.layoutHash != var.layoutHash || pc.size != var.size || pc.offset != var.offset){
						darrayFree(pushConstants);
						return SHADER_LOAD_ERROR_INCOMPATIBLE_PUSH_CONSTANTS;
					}
					pushConstants[j].stageMask |= stage->type;
					found = true;
					break;
				}
			}
			if(!found){
				darrayPush(pushConstants, 
					(struct PushConstantVariable){
						.offset=pc.offset, 
						.size=pc.size, 
						.layoutHash=pc.layoutHash,
						.stageMask=stage->type 
					} 
				);
			}
		}
		stage = stage->next;
	}
	uint32 minOffset = UINT32_MAX;
	uint32 maxOffset = 0;
	ShaderStageType stageMask = 0;
	for(uint32 i = 0; i < darrayLength(pushConstants); i++){
		struct PushConstantVariable var = pushConstants[i];
		if(var.offset <= minOffset){
			minOffset = var.offset;
		}
		if(var.offset+var.size >= maxOffset){
			maxOffset = var.offset + var.size;
		}
		stageMask |= var.stageMask;
	}
	darrayFree(pushConstants);

	if(minOffset == UINT32_MAX && maxOffset == 0){
		return SHADER_LOAD_SUCCESS;
	}

	VkPushConstantRange range = { 0};
	range.offset = minOffset;
	range.size = maxOffset - minOffset;
	range.stageFlags = translateVkShaderStage(stageMask);
	
	if(info.pushConstants.offset > range.offset || info.pushConstants.offset + info.pushConstants.size < range.offset + range.size || (range.stageFlags & (~info.pushConstants.stageFlags))){
		return SHADER_LOAD_ERROR_INCOMPATIBLE_PUSH_CONSTANTS;
	}
	
    if(range.offset + range.size > info.maxPushConstantsSize){
        return SHADER_LOAD_ERROR_PUSH_CONSTANTS_TOO_LARGE;
    }

    *pcRange = range;

	return SHADER_LOAD_SUCCESS;
}

struct ValidateDescriptorResult {

};

ShaderLoadResult validateDescriptors(ShaderStage* entry, ShaderLoadInfo info){

    struct DescriptorVariable {
        uint32 set;
        uint32 binding;
        VkDescriptorType type;
        uint32 count;
        uint64 layoutHash;
        VkShaderStageFlags stageFlags; // accumulated across stages
    };
    struct DescriptorVariable* descriptors = NULL;

    ShaderStage* stage = entry;

    while(stage) {
        for(uint32 i = 0; i < darrayLength(stage->descriptorBindings); i++) {
            ShaderDescriptorBinding sb = stage->descriptorBindings[i];
            VkShaderStageFlags stageFlag = translateVkShaderStage(stage->type);

            bool found = false;
            for(uint32 j = 0; j < darrayLength(descriptors); j++) {
                struct DescriptorVariable var = descriptors[j];
                if(var.set == sb.set && var.binding == sb.binding) {
                    found = true;
                    // Validate type, count, layoutHash consistency across stages
                    if(var.type != sb.type || var.count != sb.count || var.layoutHash != sb.layoutHash) {
                        darrayFree(descriptors);
                        return SHADER_LOAD_ERROR_DESCRIPTOR_STAGE_CONFLICT;
                    }
                    descriptors[j].stageFlags |= stageFlag;
                    break;
                }
            }

            if(!found) {
                darrayPush(descriptors, (struct DescriptorVariable){
                    .set = sb.set,
                    .binding = sb.binding,
                    .type = sb.type,
                    .count = sb.count,
                    .layoutHash = sb.layoutHash,
                    .stageFlags = stageFlag
                });
            }
        }
        stage = stage->next;
    }

    // Validate against pipeline layout
    for(uint32 i = 0; i < darrayLength(descriptors); i++) {
        struct DescriptorVariable var = descriptors[i];
        if(var.set >= info.descriptorSetCount) {
            darrayFree(descriptors);
            return SHADER_LOAD_ERROR_INVALID_DESCRIPTOR_SET;
        }

        VkDescriptorSetLayoutCreateInfo* setLayout = &info.descriptorSets[var.set];
        bool foundBinding = false;
        for(uint32 j = 0; j < setLayout->bindingCount; j++) {
            VkDescriptorSetLayoutBinding binding = setLayout->pBindings[j];
            if(binding.binding == var.binding) {
                foundBinding = true;

                if(binding.descriptorType != var.type) {
                    darrayFree(descriptors);
                    return SHADER_LOAD_ERROR_DESCRIPTOR_TYPE_MISMATCH;
                }

                if(binding.descriptorCount < var.count) {
                    darrayFree(descriptors);
                    return SHADER_LOAD_ERROR_DESCRIPTOR_COUNT_MISMATCH;
                }

                if((binding.stageFlags & var.stageFlags) != var.stageFlags) {
                    darrayFree(descriptors);
                    return SHADER_LOAD_ERROR_DESCRIPTOR_STAGE_MISMATCH;
                }

                break;
            }
        }

        if(!foundBinding) {
            darrayFree(descriptors);
            return SHADER_LOAD_ERROR_DESCRIPTOR_NOT_FOUND;
        }
    }

    darrayFree(descriptors);
    return SHADER_LOAD_SUCCESS;
}

ShaderLoadResult createObjects(VkDevice device, ShaderStage* entry, ShaderLoadInfo loadInfo, VkPushConstantRange range, Shader shader){
    ShaderLoadResult res = 0;
    uint32 createdDescriptorSets = 0;
    uint32 createdShaderModules = 0;
    {
        shader->setLayouts = avAllocate(sizeof(VkDescriptorSetLayout) * loadInfo.descriptorSetCount, "");
        shader->setLayoutCount = loadInfo.descriptorSetCount;
        for(uint32 i = 0; i < loadInfo.descriptorSetCount; i++){
            if(vkCreateDescriptorSetLayout(device, loadInfo.descriptorSets + i, NULL, shader->setLayouts+i)!=VK_SUCCESS){
                res = SHADER_LOAD_ERROR_DESCRIPTOR_SET_LAYOUT_CREATION_FAILED;
                goto failure;
            }
            createdDescriptorSets++;
        }
    }
    {
        VkPipelineLayoutCreateInfo info = { 0 };
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if(range.size != 0){
            info.pushConstantRangeCount = 1;
            info.pPushConstantRanges = &range;
        }else{
            info.pushConstantRangeCount = 0;
        }
        info.setLayoutCount = shader->setLayoutCount;
        info.pSetLayouts = shader->setLayouts;
        info.flags = 0;
        info.pNext = NULL;
        if(vkCreatePipelineLayout(device, &info, NULL, &shader->pipelineLayout)!=VK_SUCCESS){
            res = SHADER_LOAD_ERROR_PIPELINE_LAYOUT_CREATION_FAILED;
            goto failure;
        }
    }
    {
        ShaderStage* stage = entry;
        while(stage){
            stage = stage->next;
            shader->shaderStageCount++;
        }
    }
    {
        shader->modules = avAllocate(sizeof(VkShaderModule)*shader->shaderStageCount, "");
        shader->stages = avAllocate(sizeof(VkPipelineShaderStageCreateInfo)*shader->shaderStageCount, "");
        ShaderStage* stage = entry;
        uint32 index = 0;
        while(stage){
            VkShaderModuleCreateInfo info = {0};
            info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = stage->size;
            info.pCode = (uint32*)stage->bytes;
            if(vkCreateShaderModule(device, &info, NULL, &shader->modules[index])!=VK_SUCCESS){
                res = SHADER_LOAD_ERROR_SHADER_MODULE_CREATION_FAILED;
                goto failure;
            }
            
            VkPipelineShaderStageCreateInfo* stageInfo = shader->stages + index;
            stageInfo->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo->module = shader->modules[index];
            stageInfo->stage = translateVkShaderStage(stage->type);
            stageInfo->pName = "main";
            for(uint32 i = 0; i < loadInfo.shaderStageCount; i++){
                if(loadInfo.shaderStages[i].stage == stage->type){
                    stageInfo->pName = loadInfo.shaderStages[i].entry;
                    break;
                }
            }

            createdShaderModules++;
            stage = stage->next;
            index++;
        }
    }

    return res;
failure:
    if(shader->stages) avFree(shader->stages);
    for(uint32 i = 0; i < createdShaderModules; i++){
        vkDestroyShaderModule(device, shader->modules[i], NULL);
    }
    if(shader->modules) avFree(shader->modules);
    if(shader->pipelineLayout) vkDestroyPipelineLayout(device, shader->pipelineLayout, NULL);
    for(uint32 j = 0; j < createdDescriptorSets; j++){
        vkDestroyDescriptorSetLayout(device, shader->setLayouts[j], NULL);
    }
    if(shader->setLayouts) avFree(shader->setLayouts);
    return res;

}

ShaderLoadResult loadShader(VkDevice device, ShaderLoadInfo info, Shader* shader){
	shaderc_optimization_level level;
	switch(info.optimization){
		case SHADER_OPTIMIZATION_NONE:
			level = shaderc_optimization_level_zero;
			break;
		case SHADER_OPTIMIZATION_SPEED:
			level = shaderc_optimization_level_performance;
			break;
		case SHADER_OPTIMIZATION_SIZE:
			level = shaderc_optimization_level_size;
			break;
		default:
			return SHADER_LOAD_ERROR_INVALID_OPTIMIZATION_LEVEL;
	}

	if(info.shaderStageCount==0 || info.shaderStages==NULL){
		return SHADER_LOAD_ERROR_NO_SHADER_STAGES_SPECIFIED;
	}

	uint32 shaderStageMask = 0;

	for(uint32 i = 0; i < info.shaderStageCount; i++){
		ShaderStageLoadInfo stageInfo = info.shaderStages[i];
		if(shaderStageMask & stageInfo.stage){
			return SHADER_LOAD_ERROR_DUPLICATE_STAGES;
		}
		shaderStageMask |= stageInfo.stage;
	}

	if(shaderStageMask & SHADER_STAGE_COMPUTE && (shaderStageMask & ~SHADER_STAGE_COMPUTE)){
		return SHADER_LOAD_ERROR_COMPUTE_AND_GRAPHICS_SHADERS_SPECIFIED;
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
	ShaderLoadResult res = SHADER_LOAD_SUCCESS;
	for(uint32 i = 0; i < info.shaderStageCount; i++){
		ShaderStageLoadInfo stageInfo = info.shaderStages[i];
		shaderc_shader_kind shaderStage = translateShaderStage(stageInfo.stage);
		if(shaderStage==-1){
			res |= SHADER_LOAD_ERROR_INVALID_SHADER_TYPE;
			continue;
		}

		shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, stageInfo.code, stageInfo.codeSize, shaderStage, stageInfo.fileName, stageInfo.entry, options);

		shaderc_compilation_status status = shaderc_result_get_compilation_status(result);
		if(status != shaderc_compilation_status_success){
			uint64 numWarnings = shaderc_result_get_num_warnings(result);
			uint64 numErrors = shaderc_result_get_num_errors(result);
			const char* errorMsg = shaderc_result_get_error_message(result);
			fprintf(stderr, "compilation error:\n%s\n\ncompilation resulted in %i warnings and %i errors\n",errorMsg, numWarnings, numErrors);
			res |= SHADER_LOAD_ERROR_COMPILATION_ERRORS;
			shaderc_result_release(result);
			continue;
		}

		const char* bytes = shaderc_result_get_bytes(result);
		uint64 length = shaderc_result_get_length(result);

		if(length == 0){
			res |= SHADER_LOAD_ERROR_COMPILATION_ERRORS;
		}

		ShaderStage** stage = NULL;
		switch(stageInfo.stage){
			case SHADER_STAGE_VERTEX: stage = &vertex; break;
			case SHADER_STAGE_TESSELATION_CONTROL: stage = &tessControl; break;
			case SHADER_STAGE_TESSELATION_EVALUATION: stage = &tessEval; break;
			case SHADER_STAGE_GEOMETRY: stage = &geometry; break;
			case SHADER_STAGE_FRAGMENT: stage = &fragment; break;
			case SHADER_STAGE_COMPUTE: stage = &compute; break;
		}
		
		(*stage) = avAllocate(sizeof(ShaderStage) + length, "ShaderStage");
		avMemset((*stage), 0, sizeof(ShaderStage) + length);
		(*stage)->type = stageInfo.stage;
		(*stage)->bytes = (byte*)((*stage) + 1);
		(*stage)->size = length;
		avMemcpy((*stage)->bytes, bytes, length);
		

		shaderc_result_release(result);

		if(res){
			break;
		}

		res |= processShaderStage(*stage);
	}
	

	shaderc_compile_options_release(options);
	shaderc_compiler_release(compiler);

	(*shader) = avAllocate(sizeof(struct Shader_T), "");
	avMemset(*shader, 0, sizeof(struct Shader_T));
	if(res){
        goto loadShaderExit;
	}



	if(shaderStageMask & SHADER_STAGE_COMPUTE){
		(*shader)->type = SHADER_PIPELINE_COMPUTE;
		res |= SHADER_LOAD_ERROR_NOT_IMPLEMENTED; // TODO: implement compute shaders
        goto loadShaderExit;

	}else{
		(*shader)->type = SHADER_PIPELINE_GRAPHICS; 
		if(!vertex || !fragment){
			return res | SHADER_LOAD_ERROR_REQUIRED_STAGE_NOT_SPECIFIED;
		}
		if(tessControl && !tessEval){
			return res | SHADER_LOAD_ERROR_REQUIRED_STAGE_NOT_SPECIFIED;
		}
		if(tessEval && !tessControl){
			return res | SHADER_LOAD_ERROR_REQUIRED_STAGE_NOT_SPECIFIED;
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
		res |= processShaderChain(vertex);
		if(res){
			goto loadShaderExit;
		}
	
		res |= validateVertexInput(vertex->inputs, info);
		if(res){
			goto loadShaderExit;
		}

		res |= validateFragmentOutput(fragment->outputs, info);
		if(res){
			goto loadShaderExit;
		}

        VkPushConstantRange range = {0};
		res |= validatePushConstants(vertex, &range, info);
		if(res){
			goto loadShaderExit;
		}

        res |= validateDescriptors(vertex, info);
        if(res){
            goto loadShaderExit;
        }

        // we are now fully validated
        res |= createObjects(device, vertex, info, range, *shader);
        if(res){
            goto loadShaderExit;
        }
	}
loadShaderExit:
    avFree(*shader);
	if(vertex) avFree(vertex);
	if(tessControl) avFree(tessControl);
	if(tessEval) avFree(tessEval);
	if(geometry) avFree(geometry);
	if(fragment) avFree(fragment);
	if(compute) avFree(compute);

	return res;
}

void unloadShader(VkDevice device, Shader shader){
	if(shader==NULL) return;
	if(shader->stages) avFree(shader->stages);
    for(uint32 i = 0; i < shader->shaderStageCount; i++){
        vkDestroyShaderModule(device, shader->modules[i], NULL);
    }
    if(shader->modules) avFree(shader->modules);
    if(shader->pipelineLayout) vkDestroyPipelineLayout(device, shader->pipelineLayout, NULL);
    for(uint32 j = 0; j < shader->setLayoutCount; j++){
        vkDestroyDescriptorSetLayout(device, shader->setLayouts[j], NULL);
    }
    if(shader->setLayouts) avFree(shader->setLayouts);

	avFree(shader);
}