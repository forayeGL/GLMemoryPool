# GLMemoryPool

一个基于 C++17 的**高性能三层内存池**，采用类似 TCMalloc 的分层架构，针对多线程场景下的高频小对象分配进行优化。

## 特性

- **三层缓存架构**：ThreadCache（线程本地无锁） → CentralCache（全局自旋锁） → PageCache（系统页管理）
- **线程安全**：`thread_local` 快路径 + 原子操作 + 自旋锁，最大限度减少锁竞争
- **模板化大小类**：`SizeClassT<Align, MaxBytes>` 支持自定义对齐和最大分配大小，编译期 `constexpr` 零开销
- **类型安全分配器**：`Pool<T>` 封装 placement-new / 析构 + 归还的完整生命周期
- **延迟归还机制**：CentralCache 内置延迟归还策略，减少线程间内存抖动
- **大对象透传**：超过 256KB 的请求自动退化为 `malloc/free`

## 架构

```
┌─────────────────────────────────────────────────────┐
│                    应用层                             │
│           Pool<T>::create / destroy                  │
├─────────────────────────────────────────────────────┤
│  ThreadCache (thread_local, 无锁)                    │
│  每线程维护 32768 个大小类的自由链表                     │
│  命中 → O(1) 直接返回                                 │
├─────────────────────────────────────────────────────┤
│  CentralCache (全局单例, 自旋锁)                      │
│  批量分配/回收，Span 追踪，延迟归还                      │
├─────────────────────────────────────────────────────┤
│  PageCache (全局单例, 互斥锁)                         │
│  4KB 页粒度管理，空闲 Span 合并                        │
├─────────────────────────────────────────────────────┤
│  操作系统 (VirtualAlloc / mmap)                      │
└─────────────────────────────────────────────────────┘
```

## 项目结构

```
GLMemoryPool/
├── include/                    # 头文件
│   ├── Common.h                #   常量定义、BlockHeader、SizeClassT 模板
│   ├── ThreadCache.h           #   线程本地缓存（thread_local 单例）
│   ├── CentralCache.h          #   中心缓存（原子操作 + 自旋锁）
│   ├── PageCache.h             #   页缓存（4KB 页管理）
│   └── Pool.h                  #   类型安全的池化分配器 Pool<T>
├── src/                        # 实现文件
│   ├── ThreadCache.cpp         #   分配/释放、批量获取、阈值回收
│   ├── CentralCache.cpp        #   Span 切割、延迟归还、空闲合并
│   └── PageCache.cpp           #   系统内存申请、Span 管理
├── tests/                      # 测试
│   ├── UnitTest.cpp            #   单元测试
│   └── PerformanceTest.cpp     #   性能基准测试（vs malloc）
├── sample/                     # 示例
│   ├── Component.h             #   ECS 组件（Transform、RigidBody）
│   ├── Entity.h                #   实体（持有组件）
│   ├── Scene.h                 #   场景管理器（线程安全）
│   ├── EventQueue.h            #   事件队列（碰撞事件）
│   └── GameEngineSample.cpp    #   游戏引擎模拟（三阶段演示）
├── CMakeLists.txt              # 构建配置
└── README.md
```

## 构建与运行

### 环境要求

- CMake ≥ 3.10
- C++17 兼容编译器（MSVC 2017+、GCC 7+、Clang 5+）

### 编译

```bash
cmake -B build -G Ninja
cmake --build build
```

### 运行

```bash
# 单元测试
./build/unit_test

# 性能测试
./build/perf_test

# 游戏引擎示例
./build/game_engine_sample
```

## 快速上手

### 基础用法

```cpp
#include "Pool.h"

using namespace GL_memoryPool;

struct MyObject {
    int    id;
    float  data[4];
    MyObject(int i) : id(i), data{} {}
};

// 从内存池分配并构造
auto* obj = Pool<MyObject>::create(42);

// 使用对象...
obj->data[0] = 3.14f;

// 析构并归还内存到池
Pool<MyObject>::destroy(obj);
```

### 直接使用 ThreadCache

```cpp
#include "ThreadCache.h"

using namespace GL_memoryPool;

// 分配 64 字节
void* ptr = ThreadCache::getInstance()->allocate(64);

// 使用内存...

// 释放（需传入相同大小）
ThreadCache::getInstance()->deallocate(ptr, 64);
```

### 自定义对齐和大小类

```cpp
#include "Common.h"

using namespace GL_memoryPool;

// 16 字节对齐，最大 128KB
using SizeClass16 = SizeClassT<16, 128 * 1024>;

constexpr size_t aligned = SizeClass16::roundUp(100);    // == 112
constexpr size_t index   = SizeClass16::getIndex(100);   // == 6
constexpr size_t slots   = SizeClass16::FreeListSize;    // == 8192
```

## 示例输出

```
=== Game Engine Memory Pool Sample ===

Object sizes (bytes | aligned):
  Transform:      56 -> 56
  RigidBody:      48 -> 48
  Entity:         40 -> 40
  CollisionEvent: 8 -> 8

[Phase 1] Concurrent threads (100 frames)
  Elapsed:  3257 us
  Entities: 500
  Events:   2000

[Phase 2] Main-loop update
  Processed: 2000 events
  Entities:  2000
  Elapsed:   2651 us

[Phase 3] Level transition
  Destroyed: 2000  (563 us)
  Rebuilt:   500  (659 us)
  (Pool reuses memory -- no new system allocation)

=== Done ===
```

Phase 3 验证了内存池的核心价值：销毁后立即重建时，池直接从自由链表复用已释放的内存块，无需再向操作系统申请。

## 核心参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `ALIGNMENT` | 8 字节 | 最小分配对齐 |
| `MAX_BYTES` | 256 KB | 池管理的最大分配大小，超过则走 `malloc` |
| `FREE_LIST_SIZE` | 32768 | 大小类数量（`MAX_BYTES / ALIGNMENT`） |
| `PAGE_SIZE` | 4096 | PageCache 页粒度 |
| 回收阈值 | 256 | ThreadCache 单链表超过此长度时归还 CentralCache |

## 适用场景

| 场景 | 原因 |
|------|------|
| 多线程网络服务器 | 连接/请求对象高频创建销毁，大小各异 |
| 游戏引擎对象管理 | Entity/Component 多种大小、多线程更新 |
| 数据库引擎 | 行缓冲、查询节点、索引节点频繁分配 |
| 编译器 / 解释器 | AST 节点、Token 等多种小对象 |

## 许可证

MIT License
