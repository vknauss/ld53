#include "renderer.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

#include "ecs.hpp"
#include "scene_graph.hpp"
#include "opengl_utils.hpp"

#define INSTANCES_PER_UNIFORM_BUFFER 256

UniformBufferManager::UniformBufferManager()
{
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uboAlignment);
}

UniformBufferManager::~UniformBufferManager()
{
    for (const auto& info : bufferInfos)
    {
        glDeleteBuffers(1, &info.buffer);
    }
}

void UniformBufferManager::beginFrameUpload()
{
    for (auto i : inUseBuffers[frameIndex])
    {
        freeBuffers.push_back(i);
    }
    inUseBuffers[frameIndex].clear();
}

void UniformBufferManager::endFrameUpload()
{
    if (mappedPointer)
    {
        glUnmapBuffer(GL_UNIFORM_BUFFER);
        mappedPointer = nullptr;
    }
    frameIndex = (frameIndex + 1) % 2;
}

void UniformBufferManager::prepareUpload(size_t requiredSize, UniformBufferInfo& rangeInfo)
{
    UniformBufferInfo* info = nullptr;
    if (currentBuffer < bufferInfos.size())
    {
        info = &bufferInfos[currentBuffer];
        info->offset = (info->offset + uboAlignment - 1) & ~(uboAlignment - 1);
        if (!info->offset + requiredSize < info->size)
        {
            glUnmapBuffer(GL_UNIFORM_BUFFER);
            mappedPointer = nullptr;
            info = nullptr;
        }
    }
    if (!info)
    {
        if (!freeBuffers.empty())
        {
            currentBuffer = freeBuffers.back();
            freeBuffers.pop_back();
            info = &bufferInfos[currentBuffer];
            info->offset = 0;
            glBindBuffer(GL_UNIFORM_BUFFER, info->buffer);
        }
        else
        {
            currentBuffer = bufferInfos.size();
            info = &bufferInfos.emplace_back();
            info->offset = 0;
            info->size = INSTANCES_PER_UNIFORM_BUFFER * sizeof(glm::mat4);
            glGenBuffers(1, &info->buffer);
            glBindBuffer(GL_UNIFORM_BUFFER, info->buffer);
            glBufferData(GL_UNIFORM_BUFFER, info->size, NULL, GL_STREAM_DRAW);
        }
            
        inUseBuffers[frameIndex].push_back(currentBuffer);
        mappedPointer = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
        if (!mappedPointer)
        {
            throw std::runtime_error("Failed to map uniform buffer");
        }
    }

    rangeInfo.buffer = info->buffer;
    rangeInfo.offset = info->offset;
    rangeInfo.size = requiredSize;
}

void UniformBufferManager::uploadData(const void* data, size_t size, size_t offset)
{
    memcpy(static_cast<char*>(mappedPointer) + bufferInfos[currentBuffer].offset, data, size);
    bufferInfos[currentBuffer].offset += offset ? offset : size;
}

void TransformBufferManager::updateDrawBatch(const glm::mat4& cameraMatrix, SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<uint32_t>& indices, DrawBatch& batch)
{
    if (batch.instanceCount > INSTANCES_PER_UNIFORM_BUFFER)
    {
        throw std::runtime_error("Batch instance count exceeds max per buffer");
    }

    size_t requiredSize = batch.instanceCount * sizeof(glm::mat4);
    prepareUpload(requiredSize, batch.transformBufferInfo);

    glm::mat4 matrix;
    for (uint32_t i = 0; i < batch.instanceCount; ++i)
    {
        auto index = indices[batch.firstInstance + i];
        const auto& instance = drawInstances.get(index);
        matrix = sceneGraph.getWorldTransform(index).computeMatrix();
        matrix[0] *= instance.flipHorizontal ? -instance.size.x : instance.size.x;
        matrix[1] *= instance.size.y;
        matrix = cameraMatrix * matrix;
        uploadData(glm::value_ptr(matrix), sizeof(matrix));
    }
}

void MaterialBufferManager::updateDrawBatch(SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<uint32_t>& indices, DrawBatch& batch)
{
    if (batch.instanceCount > INSTANCES_PER_UNIFORM_BUFFER)
    {
        throw std::runtime_error("Batch instance count exceeds max per buffer");
    }

    size_t requiredSize = batch.instanceCount * 2 * sizeof(glm::vec4);
    prepareUpload(requiredSize, batch.materialBufferInfo);

    int useTexture = (batch.texture != 0);
    for (uint32_t i = 0; i < batch.instanceCount; ++i)
    {
        auto index = indices[batch.firstInstance + i];
        uploadData(glm::value_ptr(drawInstances.get(index).color), sizeof(glm::vec4));
        uploadData(&useTexture, sizeof(int), sizeof(glm::vec4));
    }
}

Renderer::Renderer()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint vertexShader = loadShader("shaders/vertex.glsl", GL_VERTEX_SHADER);
    GLuint fragmentShader = loadShader("shaders/fragment.glsl", GL_FRAGMENT_SHADER);
    shaderProgram = createShaderProgram({ vertexShader, fragmentShader });
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glUniformBlockBinding(shaderProgram, glGetUniformBlockIndex(shaderProgram, "TransformData"), 0);
    glUniformBlockBinding(shaderProgram, glGetUniformBlockIndex(shaderProgram, "MaterialData"), 1);

    glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);

    glGenVertexArrays(1, &vertexArray);
    glBindVertexArray(vertexArray);
}

Renderer::~Renderer()
{
    glDeleteVertexArrays(1, &vertexArray);
    glDeleteProgram(shaderProgram);
}

void Renderer::prepareRender(SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const std::vector<glm::mat4>& layerCameras)
{
    sortIndices.assign(drawInstances.indices().begin(), drawInstances.indices().end());
    std::sort(sortIndices.begin(), sortIndices.end(),
        [&] (auto index0, auto index1)
        {
            return drawInstances.get(index0).layer < drawInstances.get(index1).layer ||
                sceneGraph.getWorldTransform(index0).depth < sceneGraph.getWorldTransform(index1).depth;
        });

    batches.clear();
    for (uint32_t i = 0; i < sortIndices.size(); ++i)
    {
        const auto& instance = drawInstances.get(sortIndices[i]);
        auto texture = instance.texture;
        if (batches.empty() || instance.layer > batches.back().layer || batches.back().texture != texture)
        {
            auto& batch = batches.emplace_back();
            batch.texture = texture;
            batch.firstInstance = i;
            batch.instanceCount = 0;
        }
        ++batches.back().instanceCount;
    }

    transformBufferManager.beginFrameUpload();
    for (auto& batch : batches)
    {
        transformBufferManager.updateDrawBatch(layerCameras[batch.layer], sceneGraph, drawInstances, sortIndices, batch);
    }
    transformBufferManager.endFrameUpload();

    materialBufferManager.beginFrameUpload();
    for (auto& batch : batches)
    {
        materialBufferManager.updateDrawBatch(sceneGraph, drawInstances, sortIndices, batch);
    }
    materialBufferManager.endFrameUpload();
}

void Renderer::render(int windowWidth, int windowHeight, const glm::vec4& clearColor)
{
    glViewport(0, 0, windowWidth, windowHeight);
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a); 
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shaderProgram);

    GLuint boundTransformBuffer = 0;
    GLuint boundMaterialBuffer = 0;
    for (const auto& batch : batches)
    {
        if (batch.transformBufferInfo.buffer != boundTransformBuffer)
        {
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, batch.transformBufferInfo.buffer, batch.transformBufferInfo.offset, batch.transformBufferInfo.size);
            boundTransformBuffer = batch.transformBufferInfo.buffer;
        }

        if (batch.materialBufferInfo.buffer != boundMaterialBuffer)
        {
            glBindBufferRange(GL_UNIFORM_BUFFER, 1, batch.materialBufferInfo.buffer, batch.materialBufferInfo.offset, batch.materialBufferInfo.size);
            boundMaterialBuffer = batch.materialBufferInfo.buffer;
        }

        glBindTexture(GL_TEXTURE_2D, batch.texture);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.instanceCount);
    }
}
