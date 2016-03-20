#pragma once

#include <atomic>
#include <cstdint>

template <typename T> class LockFreeMPMCQueue
{
    public:
	explicit LockFreeMPMCQueue( size_t size )
	    : m_data( new T[size] ), m_size( size ), m_head_1( 0 ), m_head_2( 0 ), m_tail_1( 0 ), m_tail_2( 0 )
	{
	}

	virtual ~LockFreeMPMCQueue() { delete[] m_data; }

	// non-copyable
	LockFreeMPMCQueue( const LockFreeMPMCQueue<T>& ) = delete;
	LockFreeMPMCQueue( const LockFreeMPMCQueue<T>&& ) = delete;
	LockFreeMPMCQueue<T>& operator=( const LockFreeMPMCQueue<T>& ) = delete;
	LockFreeMPMCQueue<T>& operator=( const LockFreeMPMCQueue<T>&& ) = delete;

	bool try_enqueue( const T& value )
	{
		const std::uint64_t head = m_head_2.load( std::memory_order_relaxed );
		std::uint64_t tail = m_tail_1.load( std::memory_order_relaxed );

		const std::uint64_t count = tail - head;

		// count could be greater than size if between the reading of head, and the reading of tail, both head
		// and tail have been advanced
		if( count >= m_size )
		{
			return false;
		}

		if( !std::atomic_compare_exchange_strong_explicit(
			&m_tail_1, &tail, tail + 1, std::memory_order_relaxed, std::memory_order_relaxed ) )
		{
			return false;
		}

		m_data[tail % m_size] = value;

		while( m_tail_2.load( std::memory_order_relaxed ) != tail )
		{
			std::this_thread::yield();
		}

		// Release - read/write before can't be reordered with writes after
		// Make sure the write of the value to m_data is
		// not reordered past the write to m_tail_2
		std::atomic_thread_fence( std::memory_order_release );
		m_tail_2.store( tail + 1, std::memory_order_relaxed );

		return true;
	}

	bool try_dequeue( T& out )
	{
		const std::uint64_t tail = m_tail_2.load( std::memory_order_relaxed );
		std::uint64_t head = m_head_1.load( std::memory_order_relaxed );

		if( head >= tail )
		{
			return false;
		}

		if( !std::atomic_compare_exchange_strong_explicit(
			&m_head_1, &head, head + 1, std::memory_order_relaxed, std::memory_order_relaxed ) )
		{
			return false;
		}

		out = m_data[head % m_size];

		while( m_head_2.load( std::memory_order_relaxed ) != head )
		{
			std::this_thread::yield();
		}

		// Release - read/write before can't be reordered with writes after
		// Make sure the read of value from m_data is
		// not reordered past the write to m_head_2
		std::atomic_thread_fence( std::memory_order_release );
		m_head_2.store( head + 1, std::memory_order_relaxed );

		return true;
	}

	size_t capacity() const { return m_size; }

    private:
	T* m_data;
	size_t m_size;

	// Make sure each index is on its own cache line
	char pad1[60];
	std::atomic<std::uint64_t> m_head_1;
	char pad2[60];
	std::atomic<std::uint64_t> m_head_2;
	char pad3[60];
	std::atomic<std::uint64_t> m_tail_1;
	char pad4[60];
	std::atomic<std::uint64_t> m_tail_2;
};
