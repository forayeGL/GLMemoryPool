/*
 * PageCache —— 三级内存池架构的最底层
 *
 * 整体架构：ThreadCache(线程本地) → CentralCache(全局共享) → PageCache(页级管理) → OS
 *
 * PageCache 的职责：
 *   以"页(4KB)"为粒度向操作系统申请大块内存，并以 Span（连续多页）为单位
 *   向上层 CentralCache 提供内存。当上层归还 Span 时，PageCache 负责回收，
 *   并尝试将地址相邻的空闲 Span 合并，以减少外部碎片。
 *
 * 核心数据结构（见 PageCache.h）：
 *   freeSpans_  —— map<页数, Span*链表头>
 *       按页数分桶管理空闲 Span，同一页数的多个 Span 通过 next 指针串成单链表。
 *       分配时使用 lower_bound 做"最佳适配"查找（Best-Fit），保证找到的 Span
 *       页数 >= 需求，从而减少碎片。
 *
 *   spanMap_    —— map<起始地址, Span*>
 *       记录所有"已分配出去"的 Span 的地址映射，回收时据此定位 Span 对象。
 *       注意：Span 被放回 freeSpans_ 后，其条目仍保留在 spanMap_ 中，
 *       这使得合并时可以通过地址直接找到相邻 Span。
 *
 * 线程安全：
 *   PageCache 是全局单例，CentralCache 的多个线程可能并发调用 allocateSpan /
 *   deallocateSpan，因此所有公共方法均通过 mutex_ 加锁保护。
 */

#include "../include/PageCache.h"
#include <sys/mman.h>
#include <cstring>

namespace GL_memoryPool
{

/*
 * allocateSpan —— 分配一个包含 numPages 页的连续内存块
 *
 * 流程：
 *   1. 在 freeSpans_ 中用 lower_bound 查找页数 >= numPages 的最小空闲 Span
 *   2. 若找到：
 *      a) 从对应的空闲链表中摘除该 Span
 *      b) 若 Span 页数 > numPages，将多余部分切割成新 Span 放回 freeSpans_
 *      c) 将使用部分记录到 spanMap_，返回其起始地址
 *   3. 若未找到任何足够大的空闲 Span：
 *      调用 systemAlloc 向操作系统申请新内存，创建 Span 后记录并返回
 *
 * 难点 - Span 分割：
 *   当找到的空闲 Span 比请求大时，需要将其一分为二：
 *   - 前半部分（numPages 页）交给调用者
 *   - 后半部分构造新 Span，起始地址 = 原地址 + numPages * PAGE_SIZE
 *     新 Span 按其剩余页数插入 freeSpans_ 对应桶中
 *   这里必须精确计算偏移地址，否则会导致内存重叠或泄漏。
 */
void* PageCache::allocateSpan(size_t numPages)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 第一步：Best-Fit 查找 —— 找到第一个页数 >= numPages 的空闲桶
    auto it = freeSpans_.lower_bound(numPages);
    if (it != freeSpans_.end())
    {
        // 取该桶链表的头节点
        Span* span = it->second;

        // 从链表中摘除头节点：若后面还有节点则更新头指针，否则整个桶删除
        if (span->next)
        {
            freeSpans_[it->first] = span->next;
        }
        else
        {
            freeSpans_.erase(it);
        }

        // 第二步：分割 —— 当 Span 比需求大时，将多余页切割成新的 Span 放回空闲池
        if (span->numPages > numPages) 
        {
            Span* newSpan = new Span;
            // 新 Span 的起始地址 = 原 Span 起始 + 分配出去的部分
            newSpan->pageAddr = static_cast<char*>(span->pageAddr) + 
                                numPages * PAGE_SIZE;
            newSpan->numPages = span->numPages - numPages;
            newSpan->next = nullptr;

            // 将新 Span 以头插法放入对应页数的空闲链表
            auto& list = freeSpans_[newSpan->numPages];
            newSpan->next = list;
            list = newSpan;

            // 原 Span 收缩为实际分配的大小
            span->numPages = numPages;
        }

        // 第三步：登记到 spanMap_，供后续回收时通过地址反查 Span 对象
        spanMap_[span->pageAddr] = span;
        return span->pageAddr;
    }

    // 空闲池中没有足够大的 Span，向操作系统申请全新的内存
    void* memory = systemAlloc(numPages);
    if (!memory) return nullptr;

    Span* span = new Span;
    span->pageAddr = memory;
    span->numPages = numPages;
    span->next = nullptr;

    spanMap_[memory] = span;
    return memory;
}

/*
 * deallocateSpan —— 回收一个起始地址为 ptr、大小为 numPages 页的 Span
 *
 * 流程：
 *   1. 通过 spanMap_ 查找 ptr 对应的 Span 对象，找不到则说明不是本层分配的内存
 *   2. 尝试向高地址方向合并（Forward Coalescing）：
 *      计算相邻 Span 的理论起始地址 nextAddr = ptr + numPages * PAGE_SIZE，
 *      在 spanMap_ 中查找是否存在该地址对应的 Span
 *   3. 若存在相邻 Span，还需验证它是否处于"空闲"状态（在 freeSpans_ 中），
 *      只有空闲的 Span 才能合并——正在被上层使用的 Span 绝不能合并
 *   4. 验证通过后，从 freeSpans_ 中摘除相邻 Span，合并页数，清理 spanMap_ 条目
 *   5. 将最终的 Span（可能已合并变大）插入 freeSpans_ 对应桶
 *
 * 难点 - 合并时必须区分"已分配"与"空闲"：
 *   spanMap_ 记录了所有曾经分配出去的 Span（含正在使用和已回收的），
 *   因此仅凭 spanMap_ 找到相邻 Span 不能直接合并。
 *   必须进一步确认该 Span 存在于 freeSpans_ 对应页数的链表中，
 *   才能证明它是空闲的。确认后还需将其从链表中正确摘除（区分头节点和中间节点），
 *   否则链表会断裂或出现悬空指针。
 *
 * 局限性：
 *   当前仅实现了向高地址合并（Forward），未实现向低地址合并（Backward）。
 *   若低地址方向也有空闲 Span 紧邻，它们不会被合并，可能导致碎片累积。
 */
void PageCache::deallocateSpan(void* ptr, size_t numPages)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 通过地址反查 Span 对象，找不到则说明该内存非 PageCache 分配，直接返回
    auto it = spanMap_.find(ptr);
    if (it == spanMap_.end()) return;

    Span* span = it->second;

    // ---- 尝试向高地址方向合并相邻的空闲 Span ----

    // 计算紧邻的下一个 Span 的理论起始地址
    void* nextAddr = static_cast<char*>(ptr) + numPages * PAGE_SIZE;
    auto nextIt = spanMap_.find(nextAddr);

    if (nextIt != spanMap_.end())
    {
        Span* nextSpan = nextIt->second;

        // 关键判断：nextSpan 虽然存在于 spanMap_，但它可能正在被上层使用，
        // 只有当它同时存在于 freeSpans_ 空闲链表中时，才是真正空闲、可合并的。
        // 下面在 freeSpans_[nextSpan->numPages] 链表中查找并摘除 nextSpan。
        bool found = false;
        auto& nextList = freeSpans_[nextSpan->numPages];

        // 情况 1：nextSpan 恰好是链表头节点，直接将头指针指向下一个
        if (nextList == nextSpan)
        {
            nextList = nextSpan->next;
            found = true;
        }
        // 情况 2：nextSpan 在链表中间或尾部，需要遍历找到其前驱节点来摘除
        else if (nextList)
        {
            Span* prev = nextList;
            while (prev->next)
            {
                if (prev->next == nextSpan)
                {
                    prev->next = nextSpan->next;
                    found = true;
                    break;
                }
                prev = prev->next;
            }
        }
        // 情况 3：nextList 为空或遍历完未找到，说明 nextSpan 正在被使用，不合并

        if (found)
        {
            // 合并：当前 Span 吞并 nextSpan 的页数
            span->numPages += nextSpan->numPages;
            // 从 spanMap_ 中移除被合并的条目，避免悬空映射
            spanMap_.erase(nextAddr);
            delete nextSpan;
        }
    }

    // 将回收的 Span（可能已与后方合并变大）以头插法放入对应页数的空闲链表
    auto& list = freeSpans_[span->numPages];
    span->next = list;
    list = span;
}

/*
 * systemAlloc —— 向操作系统申请 numPages 页的连续内存
 *
 * 使用 mmap 而非 malloc/new 的原因：
 *   1. mmap 直接向 OS 申请虚拟内存页，返回页对齐的地址，天然满足 PAGE_SIZE 对齐要求
 *   2. 绕过 C 库的堆管理器，避免内存池与标准堆管理器之间的冲突和额外开销
 *   3. MAP_ANONYMOUS 表示不映射任何文件，纯粹用于申请匿名内存
 *   4. MAP_PRIVATE 表示写时复制，内存修改对其他进程不可见
 *
 * 注意：mmap 在 Linux 下可用，Windows 平台需替换为 VirtualAlloc。
 */
void* PageCache::systemAlloc(size_t numPages)
{
    size_t size = numPages * PAGE_SIZE;

    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;

    // mmap 的 MAP_ANONYMOUS 在 Linux 上保证返回零初始化的页，
    // 此处显式 memset 是防御性编程，确保跨平台一致性
    memset(ptr, 0, size);
    return ptr;
}

} // namespace GL_memoryPool