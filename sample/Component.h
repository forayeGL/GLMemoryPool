#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>

/// 组件基类
/// 提供 update() 和 size() 虚接口，size() 用于销毁时告知内存池归还的字节数
struct Component
{
    uint16_t entityId = 0;        // 所属实体 ID
    virtual ~Component() = default;
    virtual void   update(float dt) = 0;  // 每帧更新逻辑
    virtual size_t size() const = 0;      // 返回实际派生类的 sizeof
};

/// Transform 组件 —— 位置、旋转、缩放
struct Transform : Component
{
    float pos[3] = {}, rot[3] = {}, scl[3] = {1,1,1};

    void   update(float) override { pos[1] = std::sin(pos[0]) * scl[0]; } // 模拟矩阵变换
    size_t size() const override  { return sizeof(Transform); }
};

/// RigidBody 组件 —— 速度、加速度、质量
struct RigidBody : Component
{
    float vel[3] = {}, acc[3] = {};
    float mass = 1.f;

    void update(float dt) override  // 简化的物理积分：v += a * dt
    {
        for (int i = 0; i < 3; ++i) vel[i] += acc[i] * dt;
    }
    size_t size() const override { return sizeof(RigidBody); }
};
