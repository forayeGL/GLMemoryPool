#pragma once
#include "ThreadCache.h"
#include <utility>

namespace GL_memoryPool
{

/// 类型安全的池化分配器
/// 对 ThreadCache 的轻量封装，提供 placement-new 构造和手动析构 + 归还的完整生命周期管理。
/// 用法：
///   auto* obj = Pool<MyClass>::create(arg1, arg2);  // 从内存池分配并构造
///   Pool<MyClass>::destroy(obj);                     // 析构并归还内存到池
template <typename T>
struct Pool
{
    // 从线程本地缓存分配 sizeof(T) 字节的内存，
    template <typename... Args>
    static T* create(Args&&... args)
    {
        void* mem = ThreadCache::getInstance()->allocate(sizeof(T));
        return mem ? new (mem) T(std::forward<Args>(args)...) : nullptr;
    }

    // 手动调用 T 的析构函数，然后将内存归还到线程本地缓存的对应自由链表中。
    static void destroy(T* p)
    {
        if (!p) return;
        p->~T();
        ThreadCache::getInstance()->deallocate(p, sizeof(T));
    }
};

} // namespace GL_memoryPool
