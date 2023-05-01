#pragma once

#include <glm/glm.hpp>
#include "ecs.hpp"

struct Transform
{
    glm::vec2 position = glm::vec2(0.0f);
    float rotation = 0.0f;
    float depth = 0.0f;

    glm::mat4 computeMatrix() const;
};

struct SceneGraphNode
{
    Transform local;
    Transform world;
    float heightForDepth = 0; // amount to subtract from y-coord to get to front edge of sprite, for depth sorting purposes. only applies for top-level
    uint32_t parent = 0;
    std::vector<uint32_t> children;
    bool dirty = true;
};

class SceneGraph : public ComponentManager<SceneGraphNode>
{
    void removeParent(uint32_t index);
    void addParent(uint32_t index, uint32_t parent);
    void setDirty(uint32_t index);
    virtual SceneGraphNode& get(uint32_t index) override;
    virtual const SceneGraphNode& get(uint32_t index) const override;
    virtual const std::vector<SceneGraphNode>& all() const override;

public:
    SceneGraph();

    void create(uint32_t index) override
    {
        create(index, 0);
    }

    void create(uint32_t index, uint32_t parent);
    void destroy(uint32_t index) override;

    void setParent(uint32_t index, uint32_t parent);
    void setPosition(uint32_t index, const glm::vec2& position);
    void setRotation(uint32_t index, float rotation);
    void setDepth(uint32_t index, float depth);
    void setHeightForDepth(uint32_t index, float heightForDepth);

    uint32_t getParent(uint32_t index) const;
    const Transform& getLocalTransform(uint32_t index) const;
    const Transform& getWorldTransform(uint32_t index);

    void destroyHierarchy(EntityManager& entityManager, uint32_t index, int callLevel = 0);

    template<typename Fn, typename ... Args>
    decltype(auto) recurse(uint32_t index, const Fn& fn, const Args& ... args)
    {
        SceneGraphNode& node = get(index);
        for (auto childIndex : node.children)
        {
            recurse(childIndex, fn, args...);
        }
        return fn(index, args...);
    }
};
