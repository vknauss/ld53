#include "scene_graph.hpp"

#include <iostream>

glm::mat4 Transform::computeMatrix() const
{
    glm::mat4 matrix;
    float cosAngle = std::cos(rotation);
    float sinAngle = std::sin(rotation);
    matrix[0] = glm::vec4(cosAngle, sinAngle, 0.0f, 0.0f);
    matrix[1] = glm::vec4(-sinAngle, cosAngle, 0.0f, 0.0f);
    matrix[2] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix[3] = glm::vec4(position, 0.0f, 1.0f);

    return matrix;
}

void SceneGraph::removeParent(uint32_t index)
{
    bool found = false;
    uint32_t childIndex = 0;
    SceneGraphNode& parent = get(get(index).parent);
    for (uint32_t i = 0; !found && i < parent.children.size(); ++i)
    {
        if (parent.children[i] == index)
        {
            childIndex = i;
            found = true;
        }
    }
    if (found)
    {
        if (childIndex < parent.children.size() - 1)
        {
            parent.children[childIndex] = parent.children.back();
        }
        parent.children.pop_back();
    }
    else
    {
        std::cerr << "Warning: didn't find child. Transform hierarchy data is suspect" << std::endl;
    }
}

void SceneGraph::addParent(uint32_t index, uint32_t parent)
{
    auto& node = get(index);
    node.parent = parent;
    get(node.parent).children.push_back(index);
}

void SceneGraph::setDirty(uint32_t index)
{
    auto& node = get(index);
    if (!node.dirty)
    {
        node.dirty = true;
        for (auto childIndex : node.children)
        {
            setDirty(childIndex);
        }
    }
}

SceneGraphNode& SceneGraph::get(uint32_t index)
{
    return ComponentManager::get(index);
}

const SceneGraphNode& SceneGraph::get(uint32_t index) const
{
    return ComponentManager::get(index);
}

const std::vector<SceneGraphNode>& SceneGraph::all() const
{
    return ComponentManager::all();
}

SceneGraph::SceneGraph()
{
    create(0); // it's helpful to have the 0 entity (which EntityManager will not return) have a valid node, so we can parent top-level nodes to it
}

void SceneGraph::create(uint32_t index, uint32_t parent)
{
    ComponentManager::create(index);
    auto& node = get(index);
    node.local.position = glm::vec2(0.0f);
    node.local.rotation = 0.0f;
    node.dirty = true;
    addParent(index, parent);
}

void SceneGraph::destroy(uint32_t index)
{
    removeParent(index);
    ComponentManager::destroy(index);
}

void SceneGraph::setParent(uint32_t index, uint32_t parent)
{
    if (get(index).parent != parent)
    {
        removeParent(index);
        addParent(index, parent);
        setDirty(index);
    }
}

void SceneGraph::setPosition(uint32_t index, const glm::vec2& position)
{
    get(index).local.position = position;
    setDirty(index);
}

void SceneGraph::setRotation(uint32_t index, float rotation)
{
    get(index).local.rotation = rotation;
    setDirty(index);
}

void SceneGraph::setDepth(uint32_t index, float depth)
{
    get(index).local.depth = depth;
    setDirty(index);
}

void SceneGraph::setHeightForDepth(uint32_t index, float heightForDepth)
{
    get(index).heightForDepth = heightForDepth;
    setDirty(index);
}

uint32_t SceneGraph::getParent(uint32_t index) const
{
    return get(index).parent;
}

const Transform& SceneGraph::getLocalTransform(uint32_t index) const
{
    return get(index).local;
}

const Transform& SceneGraph::getWorldTransform(uint32_t index)
{
    SceneGraphNode& node = get(index);
    if (node.dirty)
    {
        if (node.parent != index)
        {
            const Transform& parentTransform = getWorldTransform(node.parent);
            float cosAngle = std::cos(parentTransform.rotation);
            float sinAngle = std::sin(parentTransform.rotation);
            node.world.position = parentTransform.position + glm::vec2(cosAngle * node.local.position.x - sinAngle * node.local.position.y, sinAngle * node.local.position.x + cosAngle * node.local.position.y);
            node.world.rotation = parentTransform.rotation + node.local.rotation;
            node.world.depth = parentTransform.depth + node.local.depth;
            if (node.parent == 0)
            {
                node.world.depth -= (node.local.position.y - node.heightForDepth);
            }
        }
        else
        {
            node.world = node.local;
        }
        node.dirty = false;
    }

    return node.world;
}

void SceneGraph::destroyHierarchy(EntityManager& entityManager, uint32_t index, int callLevel)
{
    SceneGraphNode& node = get(index);
    for (auto childIndex : node.children)
    {
        destroyHierarchy(entityManager, childIndex, callLevel + 1);
    }
    entityManager.destroy(index);
}
