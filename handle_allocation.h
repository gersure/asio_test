#pragma once

#include <boost/asio.hpp>

using boost::asio::ip::tcp;

// Class to manage the memory to be used for handler-based custom allocation.
// It contains a single block of memory which may be returned for allocation
// requests. If the memory is in use when an allocation request is made, the
// allocator delegates allocation to the global heap.
class handler_memory
{
public:
    handler_memory()
            : in_use_(false)
    {
    }

    handler_memory(const handler_memory&) = delete;
    handler_memory& operator=(const handler_memory&) = delete;

    void* allocate(std::size_t size)
    {
        if (!in_use_ && size < sizeof(storage_))
        {
            in_use_ = true;
            return &storage_;
        }
        else
        {
            return ::operator new(size);
        }
    }

    void deallocate(void* pointer)
    {
        if (pointer == &storage_)
        {
            in_use_ = false;
        }
        else
        {
            ::operator delete(pointer);
        }
    }

private:
    // Storage space used for handler-based custom memory allocation.
    typename std::aligned_storage<1024>::type storage_;

    // Whether the handler-based custom allocation storage has been used.
    bool in_use_;
};

// The allocator to be associated with the handler objects. This allocator only
// needs to satisfy the C++11 minimal allocator requirements.
template <typename T>
class handler_allocator
{
public:
    using value_type = T;

    explicit handler_allocator(handler_memory& mem)
            : memory_(mem)
    {
    }

    template <typename U>
    handler_allocator(const handler_allocator<U>& other) noexcept
            : memory_(other.memory_)
    {
    }

    bool operator==(const handler_allocator& other) const noexcept
    {
        return &memory_ == &other.memory_;
    }

    bool operator!=(const handler_allocator& other) const noexcept
    {
        return &memory_ != &other.memory_;
    }

    T* allocate(std::size_t n) const
    {
        return static_cast<T*>(memory_.allocate(sizeof(T) * n));
    }

    void deallocate(T* p, std::size_t /*n*/) const
    {
        return memory_.deallocate(p);
    }

private:
    template <typename> friend class handler_allocator;

    // The underlying memory.
    handler_memory& memory_;
};

// Wrapper class template for handler objects to allow handler memory
// allocation to be customised. The allocator_type type and get_allocator()
// member function are used by the asynchronous operations to obtain the
// allocator. Calls to operator() are forwarded to the encapsulated handler.
template <typename Handler>
class custom_alloc_handler
{
public:
    using allocator_type = handler_allocator<Handler>;

    custom_alloc_handler(handler_memory& m, Handler h)
            : memory_(m),
              handler_(h)
    {
    }

    allocator_type get_allocator() const noexcept
    {
        return allocator_type(memory_);
    }

    template <typename ...Args>
    void operator()(Args&&... args)
    {
        handler_(std::forward<Args>(args)...);
    }

private:
    handler_memory& memory_;
    Handler handler_;
};

// Helper function to wrap a handler object to add custom allocation.
template <typename Handler>
inline custom_alloc_handler<Handler> make_custom_alloc_handler(
        handler_memory& m, Handler h)
{
    return custom_alloc_handler<Handler>(m, h);
}

