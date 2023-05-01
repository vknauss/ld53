#include "renderer.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

#include "ecs.hpp"
#include "scene_graph.hpp"
#include "opengl_utils.hpp"

static constexpr size_t INSTANCES_PER_UNIFORM_BUFFER  = 256;
static constexpr size_t TEXT_VERTEX_BUFFER_SIZE  = 16384;

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

TextBufferManager::~TextBufferManager()
{
    for (const auto& info : bufferInfos)
    {
        glDeleteBuffers(1, &info.vertexBuffer);
        glDeleteVertexArrays(1, &info.vertexArray);
    }
}

void TextBufferManager::beginFrameUpload()
{
    for (auto i : inUseBuffers[frameIndex])
    {
        freeBuffers.push_back(i);
    }
    inUseBuffers[frameIndex].clear();
}

void TextBufferManager::endFrameUpload()
{
    if (mappedPointer)
    {
        glUnmapBuffer(GL_ARRAY_BUFFER);
        mappedPointer = nullptr;
    }
    frameIndex = (frameIndex + 1) % 2;
}

void TextBufferManager::uploadData(const std::string& text, DrawBatch& batch)
{
    TextBufferInfo* info = nullptr;
    size_t requiredSize = 4 * sizeof(glm::vec2) * text.size();
    if (currentBuffer < bufferInfos.size())
    {
        info = &bufferInfos[currentBuffer];
        if (!info->offset + requiredSize < info->size)
        {
            glUnmapBuffer(GL_ARRAY_BUFFER);
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
            info->currentIndex = 0;
            glBindBuffer(GL_ARRAY_BUFFER, info->vertexBuffer);
        }
        else
        {
            currentBuffer = bufferInfos.size();
            info = &bufferInfos.emplace_back();
            info->offset = 0;
            info->currentIndex = 0;
            info->size = TEXT_VERTEX_BUFFER_SIZE;
            glGenBuffers(1, &info->vertexBuffer);
            glBindBuffer(GL_ARRAY_BUFFER, info->vertexBuffer);
            glBufferData(GL_ARRAY_BUFFER, info->size, NULL, GL_STREAM_DRAW);
            glGenVertexArrays(1, &info->vertexArray);
            glBindVertexArray(info->vertexArray);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::vec2), reinterpret_cast<void*>(0));
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::vec2), reinterpret_cast<void*>(sizeof(glm::vec2)));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
        }
            
        inUseBuffers[frameIndex].push_back(currentBuffer);
        mappedPointer = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        if (!mappedPointer)
        {
            throw std::runtime_error("Failed to map text buffer");
        }
    }

    glm::vec2 texCoordScale(1.0f / 16.0f, 1.0f / 8.0f);
    for (uint32_t i = 0; i < text.size(); ++i)
    {
        glm::vec2 texCoord = texCoordScale * glm::vec2(text[i] >> 3, text[i] & 7);
        const glm::vec2 vertexData[] {
            { i, 1 }, texCoord, { i, 0 }, { texCoord.x, texCoord.y + texCoordScale.y },
            { i + 1, 1 }, { texCoord.x + texCoordScale.x, texCoord.y }, { i + 1, 0 }, texCoord + texCoordScale,
        };
        memcpy(static_cast<char*>(mappedPointer) + info->offset, vertexData, sizeof(vertexData));
        info->offset += sizeof(vertexData);
    }

    batch.vertexArray = info->vertexArray;
    batch.firstIndex = info->currentIndex;
    batch.count = text.size() * 4;

    info->currentIndex += batch.count;
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

Renderer::Renderer(SceneGraph& sceneGraph, const ComponentManager<DrawInstance>& drawInstances, const ComponentManager<TextInstance>& textInstances) :
    sceneGraph(sceneGraph),
    drawInstances(drawInstances),
    textInstances(textInstances)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint vertexShader = loadShader("shaders/vertex.glsl", GL_VERTEX_SHADER);
    GLuint textVertexShader = loadShader("shaders/text_vertex.glsl", GL_VERTEX_SHADER);
    GLuint fragmentShader = loadShader("shaders/fragment.glsl", GL_FRAGMENT_SHADER);
    shaderProgram = createShaderProgram({ vertexShader, fragmentShader });
    textProgram = createShaderProgram({ textVertexShader, fragmentShader });
    glDeleteShader(vertexShader);
    glDeleteShader(textVertexShader);
    glDeleteShader(fragmentShader);

    glUniformBlockBinding(shaderProgram, glGetUniformBlockIndex(shaderProgram, "TransformData"), 0);
    glUniformBlockBinding(shaderProgram, glGetUniformBlockIndex(shaderProgram, "MaterialData"), 1);
    glUniformBlockBinding(textProgram, glGetUniformBlockIndex(textProgram, "TransformData"), 0);
    glUniformBlockBinding(textProgram, glGetUniformBlockIndex(textProgram, "MaterialData"), 1);

    glUniform1i(glGetUniformLocation(shaderProgram, "textureSampler"), 0);
    glUniform1i(glGetUniformLocation(textProgram, "textureSampler"), 0);

    glGenVertexArrays(1, &vertexArray);

    fontTexture = loadTexture("textures/font.png");
}

Renderer::~Renderer()
{
    glDeleteVertexArrays(1, &vertexArray);
    glDeleteProgram(shaderProgram);
}

void Renderer::prepareRender(const std::vector<glm::mat4>& layerCameras)
{
    sortIndices.assign(drawInstances.indices().begin(), drawInstances.indices().end());
    std::sort(sortIndices.begin(), sortIndices.end(),
        [&] (auto index0, auto index1)
        {
            const auto& instance0 = drawInstances.get(index0);
            const auto& instance1 = drawInstances.get(index1);
            return (instance0.layer == instance1.layer && sceneGraph.getWorldTransform(index0).depth < sceneGraph.getWorldTransform(index1).depth) || instance0.layer < instance1.layer;
        });

    batches.clear();
    for (uint32_t i = 0; i < sortIndices.size(); ++i)
    {
        const auto& instance = drawInstances.get(sortIndices[i]);
        auto texture = instance.isText ? fontTexture : instance.texture;

        if (batches.empty() || instance.isText || batches.back().instanceCount == INSTANCES_PER_UNIFORM_BUFFER || instance.layer > batches.back().layer || batches.back().texture != texture)
        {
            auto& batch = batches.emplace_back();
            batch.texture = texture;
            batch.firstInstance = i;
            batch.instanceCount = 0;
            batch.layer = instance.layer;

            if (!instance.isText)
            {
                batch.vertexArray = vertexArray;
                batch.shaderProgram = shaderProgram;
                batch.firstIndex = 0;
                batch.count = 4;
            }
            else
            {
                batch.shaderProgram = textProgram;
                textBufferManager.uploadData(textInstances.get(sortIndices[i]).text, batch);
            }
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

    GLuint boundTransformBuffer = 0;
    GLuint boundMaterialBuffer = 0;
    GLuint boundVertexArray = 0;
    GLuint boundShaderProgram = 0;
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

        if (batch.vertexArray != boundVertexArray)
        {
            glBindVertexArray(batch.vertexArray);
            boundVertexArray = batch.vertexArray;
        }

        if (batch.shaderProgram != boundShaderProgram)
        {
            glUseProgram(batch.shaderProgram);
            boundShaderProgram = batch.shaderProgram;
        }

        glBindTexture(GL_TEXTURE_2D, batch.texture);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, batch.firstIndex, batch.count, batch.instanceCount);
    }
}
