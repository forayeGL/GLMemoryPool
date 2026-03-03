#pragma once
#include "Entity.h"
#include <vector>
#include <mutex>

/// 场景管理器 —— 线程安全的实体生命周期管理
class Scene
{
public:
    /// 创建实体：分配 Entity + Transform + RigidBody，设置初始位置
    Entity* spawn(float x, float y, float z)
    {
        Entity* e = Pool<Entity>::create();
        if (!e) return nullptr;
        e->id = nextId_++;
        auto* tf = e->add<Transform>();
        if (tf) { tf->pos[0] = x; tf->pos[1] = y; tf->pos[2] = z; }
        e->add<RigidBody>();
        std::lock_guard<std::mutex> lk(mtx_);
        entities_.push_back(e);
        return e;
    }

    /// 每帧更新所有实体
    void update(float dt)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto* e : entities_) e->update(dt);
    }

    /// 销毁所有实体及其组件，内存归还到池（模拟关卡切换）
    void clear()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto* e : entities_) { e->destroy(); Pool<Entity>::destroy(e); }
        entities_.clear();
    }

    /// 返回当前存活的实体数量
    size_t count()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return entities_.size();
    }

private:
    std::vector<Entity*> entities_;  // 所有存活实体
    std::mutex mtx_;                 // 保护 entities_ 的互斥锁
    uint16_t nextId_ = 1;            // 自增实体 ID
};
