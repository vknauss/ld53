#pragma once

#include <cstdint>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>

class SceneGraph;
template<typename T> class ComponentManager;

struct UniformBufferInfo
{
    GLuint buffer = 0;
    uintptr_t offset = 0;
    size_t size = 0;
};

struct DrawInstance
{
    glm::vec2 size = glm::vec2(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    GLuint texture = 0;
    bool flipHorizontal = false;
    uint32_t layer = 0;
};

struct DrawBatch
{
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
    GLuint texture = 0;
    uint32_t layer = 0;
    UniformBufferInfo transformBufferInfo;
    UniformBufferInfo materialBufferInfo;
};

class UniformBufferManager
{
    std::vector<UniformBufferInfo> bufferInfos;
    std::vector<uint32_t> inUseBuffers[2];
    std::vector<uint32_t> freeBuffers;
    int frameIndex = 0;
    uint32_t currentBuffer = 0;
    void* mappedPointer = nullptr;
    GLint uboAlignment = 1;

public:
    UniformBufferManager();
    virtual ~UniformBufferManager();

    void beginFrameUpload();
    void endFrameUpload();
    void prepareUpload(size_t requiredSize, UniformBufferInfo& rangeInfo);
    void uploadData(const void* data, size_t size, size_t offset = 0);
};

class TransformBufferManager : public UniformBufferManager
{
public:
    void updateDrawBatch(const glm::mat4& cameraMatrix, SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<uint32_t>& indices, DrawBatch& batch);
};

class MaterialBufferManager : public UniformBufferManager
{
public:
    void updateDrawBatch(SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<uint32_t>& indices, DrawBatch& batch);
};

class Renderer
{
    TransformBufferManager transformBufferManager;
    MaterialBufferManager materialBufferManager;
    std::vector<DrawBatch> batches;
    std::vector<uint32_t> sortIndices;
    GLuint shaderProgram;
    GLuint vertexArray;

public:
    Renderer();
    ~Renderer();

    void prepareRender(SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<glm::mat4>& layerCameras);
    void render(int windowWidth, int windowHeight, const glm::vec4& clearColor);
};
