// 固定容量环形缓冲：单线程使用（数据在主线程 append，UI 也在主线程读）。
#pragma once

#include <QtGlobal>

#include <vector>

namespace smd {

template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer(size_t capacity = 0) { resize(capacity); }

    void resize(size_t capacity)
    {
        m_data.assign(capacity, T{});
        m_head = 0;          // 下一个写入位置
        m_size = 0;
        m_total = 0;
    }

    void clear()
    {
        m_head = 0;
        m_size = 0;
        m_total = 0;
    }

    void append(const T &value)
    {
        if (m_data.empty())
            return;
        m_data[m_head] = value;
        m_head = (m_head + 1) % m_data.size();
        if (m_size < m_data.size())
            ++m_size;
        ++m_total;
    }

    size_t capacity() const { return m_data.size(); }
    size_t size() const { return m_size; }

    /// 缓冲区中已有样本总数（含被覆盖的历史）
    quint64 total() const { return m_total; }

    /// 缓冲区第 i 个样本（0 = 最旧）对应的绝对序号
    quint64 absoluteIndex(size_t i) const { return m_total - m_size + i; }

    /// 绝对序号 idx 是否还在缓冲区内
    bool contains(quint64 idx) const
    {
        return m_size > 0 && idx >= (m_total - m_size) && idx < m_total;
    }

    T at(size_t i) const
    {
        const size_t start = (m_head + m_data.size() - m_size) % m_data.size();
        return m_data[(start + i) % m_data.size()];
    }

private:
    std::vector<T> m_data;
    size_t m_head = 0;     // 下一个写位置
    size_t m_size = 0;     // 已用样本数
    quint64 m_total = 0;   // 累计写入样本数
};

} // namespace smd
