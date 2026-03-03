#pragma once
#include <cstddef>
#include <atomic>
#include <array>

namespace GL_memoryPool 
{
// 对齐数和大小定义
constexpr size_t ALIGNMENT = 8;
// 256KB是一个合理的最大分配大小，超过这个大小的请求直接使用系统分配
constexpr size_t MAX_BYTES = 256 * 1024; 
constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT; // ALIGNMENT等于指针void*的大小

// 内存块头部信息
struct BlockHeader
{
    size_t size; // 内存块大小
    bool   inUse; // 使用标志
    BlockHeader* next; // 指向下一个内存块
};

// 大小类管理
class SizeClass 
{
public:
	// 将请求的字节数向上取整到ALIGNMENT的倍数
    static size_t roundUp(size_t bytes)
    {
        return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    static size_t getIndex(size_t bytes)
    {   
		// 确保bytes至少为ALIGNMENT，否则可能会导致索引为负数或越界
        bytes = std::max(bytes, ALIGNMENT);
        // 向上取整后-1
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
};

} // namespace GL_memoryPool