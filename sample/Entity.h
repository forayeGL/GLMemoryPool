#pragma once
#include "Component.h"
#include "../include/Pool.h"
#include <vector>

using namespace GL_memoryPool;

/// 实体 —— 持有一组从内存池分配的组件
struct Entity
{
    uint16_t id = 0;
    std::vector<Component*> comps;  // 该实体拥有的所有组件

    /// 通过 Pool<T> 创建组件并挂载到实体
    template <typename T, typename... A>
    T* add(A&&... a)
    {
        T* c = Pool<T>::create(std::forward<A>(a)...);
        if (c) { c->entityId = id; comps.push_back(c); }
        return c;
    }

    /// 遍历更新所有组件
    void update(float dt) { for (auto* c : comps) c->update(dt); }

    /// 逐个析构组件并将内存归还到池（通过 size() 获取实际字节数）
    void destroy()
    {
        for (auto* c : comps)
        {
            size_t sz = c->size();
            c->~Component();
            ThreadCache::getInstance()->deallocate(c, sz);
        }
        comps.clear();
    }
};
