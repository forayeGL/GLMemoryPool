#pragma once
#include "ThreadCache.h"

namespace GL_memoryPool
{

	// MemoryPool作为对外接口，封装了ThreadCache的分配和释放方法
    class MemoryPool
    {
    public:
        static void* allocate(size_t size)
        {
            return ThreadCache::getInstance()->allocate(size);
        }

        static void deallocate(void* ptr, size_t size)
        {
            ThreadCache::getInstance()->deallocate(ptr, size);
        }
    };

} // namespace GL_memoryPool