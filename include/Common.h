#pragma once
#include <cstddef>
#include <atomic>
#include <array>

namespace GL_memoryPool 
{
// 默认对齐数和大小定义
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

// 模板化大小类管理，支持自定义对齐数和最大分配大小
template <size_t Align = ALIGNMENT, size_t MaxBytes = MAX_BYTES>
class SizeClassT
{
	static_assert(Align > 0 && (Align & (Align - 1)) == 0,
				  "Align must be a positive power of 2");
	static_assert(MaxBytes >= Align,
				  "MaxBytes must be >= Align");

public:
	static constexpr size_t AlignValue     = Align;
	static constexpr size_t MaxBytesValue  = MaxBytes;
	static constexpr size_t FreeListSize   = MaxBytes / Align;

	// 将请求的字节数向上取整到Align的倍数
	static constexpr size_t roundUp(size_t bytes)
	{
		return (bytes + Align - 1) & ~(Align - 1);
	}

	static constexpr size_t getIndex(size_t bytes)
	{   
		// 确保bytes至少为Align，否则可能会导致索引为负数或越界
		bytes = (bytes < Align) ? Align : bytes;
		// 向上取整后-1
		return (bytes + Align - 1) / Align - 1;
	}
};

// 默认类型别名，保持向后兼容
using SizeClass = SizeClassT<>;

} // namespace GL_memoryPool