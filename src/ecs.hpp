#pragma once

#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

class ComponentManagerBase
{
public:
    virtual ~ComponentManagerBase() = default;
    virtual void create(uint32_t index) = 0;
    virtual void destroy(uint32_t index) = 0;
    virtual bool has(uint32_t index) const = 0;
};

template<typename T>
class ComponentManager : public ComponentManagerBase
{
    static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();
    std::vector<T> components;
    std::vector<uint32_t> componentIndices;
    std::vector<uint32_t> packedIndices;

public:
    virtual ~ComponentManager() = default;

    virtual void create(uint32_t index) override
    {
        if (index >= packedIndices.size())
        {
            packedIndices.resize(index + 1, InvalidIndex);
        }
        packedIndices[index] =  components.size();
        components.emplace_back();
        componentIndices.push_back(index);
    }

    virtual void destroy(uint32_t index) override
    {
        if (packedIndices[index] < components.size() - 1)
        {
            components[packedIndices[index]] = std::move(components.back());
            componentIndices[packedIndices[index]] = componentIndices.back();
            packedIndices[componentIndices.back()] = packedIndices[index];
        }
        components.pop_back();
        componentIndices.pop_back();
        packedIndices[index] = InvalidIndex;
    }

    virtual bool has(uint32_t index) const override
    {
        return index < packedIndices.size() && packedIndices[index] != InvalidIndex;
    }

    virtual const T& get(uint32_t index) const
    {
        return components[packedIndices[index]];
    }

    virtual T& get(uint32_t index)
    {
        return components[packedIndices[index]];
    }

    virtual const std::vector<T>& all() const
    {
        return components;
    };

    virtual const std::vector<uint32_t>& indices() const
    {
        return componentIndices;
    }
};

class EntityManager
{
    std::deque<uint32_t> freeIndices;
    uint32_t nextIndex = 1;
    std::vector<ComponentManagerBase*> componentManagers;

public:

    uint32_t create()
    {
        if (!freeIndices.empty())
        {
            uint32_t index = freeIndices.front();
            freeIndices.pop_front();
            return index;
        }

        return nextIndex++;
    }

    void destroy(uint32_t index)
    {
        for (auto* componentManager : componentManagers)
        {
            if (componentManager->has(index))
            {
                componentManager->destroy(index);
            }
        }
        freeIndices.push_back(index);
    }

    void addComponentManager(ComponentManagerBase& componentManager)
    {
        componentManagers.push_back(&componentManager);
    }
};
