/**
 * 游戏引擎对象管理示例
 * 演示内存池的典型用法：多种大小的 ECS 组件、事件消息、
 * 多线程并发分配，以及关卡切换时的内存复用。
 */

#include "Scene.h"
#include "EventQueue.h"
#include <iostream>
#include <thread>
#include <chrono>

/// 计时辅助工具 —— 记录构造时刻，调用 us() 返回经过的微秒数
struct Timer
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point start = Clock::now();
    long long us() const
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - start).count();
    }
};

// ============================================================================
// Main simulation
// ============================================================================

int main()
{
    std::cout << "=== Game Engine Memory Pool Sample ===\n\n";

    std::cout << "Object sizes (bytes | aligned):\n"
              << "  Transform:      " << sizeof(Transform)
              << " -> " << SizeClass::roundUp(sizeof(Transform)) << "\n"
              << "  RigidBody:      " << sizeof(RigidBody)
              << " -> " << SizeClass::roundUp(sizeof(RigidBody)) << "\n"
              << "  Entity:         " << sizeof(Entity)
              << " -> " << SizeClass::roundUp(sizeof(Entity)) << "\n"
              << "  CollisionEvent: " << sizeof(CollisionEvent)
              << " -> " << SizeClass::roundUp(sizeof(CollisionEvent)) << "\n\n";

    constexpr int FRAMES   = 100;
    constexpr int ENTITIES = 500;

    Scene      scene;
    EventQueue events;

    // -- 阶段 1：并发线程 -- 物理事件生成 + 实体批量创建 --
    std::cout << "[Phase 1] Concurrent threads (" << FRAMES << " frames)\n";
    {
        Timer t;

        std::thread tPhys([&] {
            for (int f = 0; f < FRAMES; ++f)
                for (int i = 0; i < 20; ++i)
                    events.push(uint16_t(i), uint16_t(i+1), 9.8f * (i+1));
        });

        std::thread tSpawn([&] {
            for (int i = 0; i < ENTITIES; ++i)
                scene.spawn(float(i % 100), 0.f, float(i / 100));
        });

        tPhys.join();
        tSpawn.join();

        std::cout << "  Elapsed:  " << t.us() << " us\n"
                  << "  Entities: " << scene.count() << "\n"
                  << "  Events:   " << events.size() << "\n\n";
    }

    // -- 阶段 2：消费事件队列，运行 10 帧更新 --
    std::cout << "[Phase 2] Main-loop update\n";
    {
        Timer t;

        size_t processed = events.drain([&](const CollisionEvent& c) {
            if (c.force > 50.f) scene.spawn(c.force, 0.f, 0.f);
        });

        for (int f = 0; f < 10; ++f) scene.update(0.016f);

        std::cout << "  Processed: " << processed << " events\n"
                  << "  Entities:  " << scene.count() << "\n"
                  << "  Elapsed:   " << t.us() << " us\n\n";
    }

    // -- 阶段 3：关卡切换 -- 全量销毁后立即重建（验证内存池复用）--
    std::cout << "[Phase 3] Level transition\n";
    {
        Timer t;
        size_t before = scene.count();
        scene.clear();
        long long destroyUs = t.us();

        Timer t2;
        for (int i = 0; i < ENTITIES; ++i)
            scene.spawn(float(i), 0.f, 0.f);
        long long rebuildUs = t2.us();

        std::cout << "  Destroyed: " << before << "  (" << destroyUs << " us)\n"
                  << "  Rebuilt:   " << scene.count() << "  (" << rebuildUs << " us)\n"
                  << "  (Pool reuses memory -- no new system allocation)\n\n";
    }

    scene.clear();
    std::cout << "=== Done ===\n";
    return 0;
}
