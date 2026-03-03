#pragma once
#include "../include/Pool.h"
#include <cstdint>
#include <vector>
#include <mutex>

using namespace GL_memoryPool;

/// 碰撞事件 —— 记录碰撞双方 ID 和碰撞力
struct CollisionEvent
{
    uint16_t a, b;   // 碰撞的两个实体 ID
    float    force;  // 碰撞力大小
};

/// 线程安全的事件队列 —— 事件对象从内存池分配/归还
class EventQueue
{
public:
    /// 从池中分配一个碰撞事件并加锁入队
    void push(uint16_t a, uint16_t b, float f)
    {
        auto* e = Pool<CollisionEvent>::create(CollisionEvent{a, b, f});
        if (e) { std::lock_guard<std::mutex> lk(mtx_); events_.push_back(e); }
    }

    /// 原子地取出全部事件，逐个调用 handler 后归还到池，返回处理数量
    template <typename Fn>
    size_t drain(Fn&& handler)
    {
        std::vector<CollisionEvent*> snap;
        { std::lock_guard<std::mutex> lk(mtx_); snap.swap(events_); }
        for (auto* e : snap) { handler(*e); Pool<CollisionEvent>::destroy(e); }
        return snap.size();
    }

    /// 返回当前队列中待处理的事件数量
    size_t size()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        return events_.size();
    }

private:
    std::vector<CollisionEvent*> events_;  // 待处理事件列表
    std::mutex mtx_;                       // 保护 events_ 的互斥锁
};
