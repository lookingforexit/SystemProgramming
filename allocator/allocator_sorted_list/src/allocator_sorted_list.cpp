#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"

auto allocator_sorted_list::get_parent_alloc() const
{
    return static_cast<memory_resource**>(_trusted_memory);
}

auto allocator_sorted_list::get_fit_mode() const
{
    return reinterpret_cast<fit_mode*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*));
}

auto allocator_sorted_list::get_full_size() const
{
    return reinterpret_cast<size_t*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(fit_mode));
}

auto allocator_sorted_list::get_mutex() const
{
    return reinterpret_cast<std::mutex*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(fit_mode) + sizeof(size_t));
}

auto allocator_sorted_list::get_head() const
{
    return reinterpret_cast<void**>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(fit_mode) + sizeof(size_t) + sizeof(std::mutex));
}

void allocator_sorted_list::insert_free_block(void* block) const
{
    void* curr = *get_head();
    void* prev = nullptr;

    while (curr != nullptr && curr < block)
    {
        prev = curr;
        curr = *reinterpret_cast<void**>(static_cast<char*>(curr) + sizeof(size_t));
    }

    void** new_next_ptr = reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
    *new_next_ptr = curr;

    if (prev == nullptr)
    {
        *get_head() = block;
    } else
    {
        void** prev_next_ptr = reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t));
        *prev_next_ptr = block;
    }
}

allocator_sorted_list::~allocator_sorted_list()
{
    if (_trusted_memory != nullptr)
    {
        get_mutex()->~mutex();

        auto parent_alloc = *get_parent_alloc();
        if (parent_alloc != nullptr)
        {
            parent_alloc->deallocate(_trusted_memory, *get_full_size());
        } else
        {
            ::operator delete(_trusted_memory);
        }
    }
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator != nullptr)
    {
        _trusted_memory = parent_allocator->allocate(space_size);
    } else
    {
        _trusted_memory = ::operator new(space_size);
    }

    *get_parent_alloc() = parent_allocator;
    *get_fit_mode() = allocate_fit_mode;
    *get_full_size() = space_size;
    new (get_mutex()) std::mutex();

    auto start = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    size_t blocks_size = space_size - allocator_metadata_size;

    auto size_ptr = reinterpret_cast<size_t*>(start);
    *size_ptr = blocks_size & ~size_t(1);

    auto next = reinterpret_cast<void**>(start + sizeof(size_t));
    *next = nullptr;

    *get_head() = start;
}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    size_t size)
{
    std::lock_guard lock(*get_mutex());
    std::size_t needed = size + sizeof(size_t);

    void* found_block = nullptr;
    void* found_prev = nullptr;

    switch (*get_fit_mode())
    {
    case fit_mode::first_fit:
        {
            void* block = *get_head();
            void* prev = nullptr;

            while (block != nullptr)
            {
                size_t block_size = *static_cast<size_t*>(block) & ~size_t(1);
                if (block_size >= needed)
                {
                    found_block = block;
                    found_prev = prev;
                    break;
                }

                prev = block;
                block = *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
            }
            break;
        }
    case fit_mode::the_best_fit:
        {
            void* block = *get_head();
            void* prev = nullptr;
            void* best_block = nullptr;
            void* best_prev = nullptr;
            size_t best_size = SIZE_MAX;

            while (block != nullptr)
            {
                size_t block_size = *static_cast<size_t*>(block) & ~size_t(1);
                if (block_size >= needed && block_size < best_size)
                {
                    best_block = block;
                    best_prev = prev;
                    best_size = block_size;
                }

                prev = block;
                block = *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
            }

            found_block = best_block;
            found_prev = best_prev;
            break;
        }
    case fit_mode::the_worst_fit:
        {
            void* block = *get_head();
            void* prev = nullptr;
            void* worst_block = nullptr;
            void* worst_prev = nullptr;
            size_t worst_size = 0;

            while (block != nullptr)
            {
                size_t block_size = *static_cast<size_t*>(block) & ~size_t(1);
                if (block_size >= needed && block_size > worst_size)
                {
                    worst_block = block;
                    worst_prev = prev;
                    worst_size = block_size;
                }

                prev = block;
                block = *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
            }

            found_block = worst_block;
            found_prev = worst_prev;
            break;
        }
    }

    if (found_block == nullptr)
    {
        throw std::bad_alloc();
    }

    size_t block_size = *static_cast<size_t*>(found_block) & ~size_t(1);

    void* next_block = *reinterpret_cast<void**>(static_cast<char*>(found_block) + sizeof(size_t));
    if (found_prev != nullptr)
    {
        void** prev_next_block = reinterpret_cast<void**>(static_cast<char*>(found_prev) + sizeof(size_t));
        *prev_next_block = next_block;
    } else
    {
        *get_head() = next_block;
    }

    size_t remainder = block_size - needed;
    if (remainder >= block_metadata_size)
    {
        void* new_free_block = static_cast<char*>(found_block) + needed;
        *static_cast<size_t*>(new_free_block) = remainder;
        *reinterpret_cast<void**>(static_cast<char*>(new_free_block) + sizeof(size_t)) = nullptr;
        insert_free_block(new_free_block);
    }

    size_t allocated_size = remainder >= block_metadata_size ? needed : block_size;
    *static_cast<size_t*>(found_block) = allocated_size | 1;

    return static_cast<char*>(found_block) + sizeof(size_t);
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto p = dynamic_cast<const allocator_sorted_list*>(&other);
    return p != nullptr && p->_trusted_memory == _trusted_memory;
}

void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    std::lock_guard lock(*get_mutex());

    if (at == nullptr)
    {
        return;
    }

    char* block_st = static_cast<char*>(at) - sizeof(size_t);
    char* trusted_end = static_cast<char*>(_trusted_memory) + *get_full_size();
    if (block_st < static_cast<char*>(_trusted_memory) || block_st >= trusted_end)
    {
        throw std::invalid_argument("block bounds are invalid");
    }

    auto size_ptr = reinterpret_cast<size_t*>(block_st);
    size_t raw_size = *size_ptr;
    if (!(raw_size & 1))
    {
        throw std::invalid_argument("block is free");
    }

    size_t block_size = raw_size & ~size_t(1);
    *size_ptr = block_size;

    void** free_head = get_head();
    void* prev = nullptr;
    void* curr = *free_head;
    while (curr != nullptr && curr < block_st)
    {
        prev = curr;
        curr = *reinterpret_cast<void**>(static_cast<char*>(curr) + sizeof(size_t));
    }

    if (curr != nullptr && block_st + block_size == curr)
    {
        auto curr_size_ptr = static_cast<size_t*>(curr);
        size_t curr_size = *curr_size_ptr & ~size_t(1);
        block_size += curr_size;
        *size_ptr = block_size;

        void* next_curr = *reinterpret_cast<void**>(static_cast<char*>(curr) + sizeof(size_t));
        if (prev != nullptr)
        {
            *reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t)) = next_curr;
        } else
        {
            *free_head = next_curr;
        }

        curr = next_curr;
    }

    if (prev != nullptr)
    {
        char* prev_st = static_cast<char*>(prev);
        auto prev_size_ptr = reinterpret_cast<size_t*>(prev_st);
        size_t prev_size = *prev_size_ptr & ~size_t(1);
        if (prev_st + prev_size == block_st)
        {
            prev_size += block_size;
            *prev_size_ptr = prev_size;

            *reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t)) = curr;
            return;
        }
    }

    *reinterpret_cast<void**>(block_st + sizeof(size_t)) = curr;

    if (prev != nullptr)
    {
        *reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t)) = block_st;
    } else
    {
        *free_head = block_st;
    }
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard lock(*get_mutex());
    *get_fit_mode() = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    std::lock_guard lock(*get_mutex());
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<block_info> result;

    char* mem_st = static_cast<char*>(_trusted_memory);
    char* mem_end = mem_st + *get_full_size();
    char* curr = mem_st + allocator_metadata_size;

    while (curr < mem_end)
    {
        bool is_free = (*reinterpret_cast<size_t*>(curr) & 1) == 0;
        size_t block_size = *reinterpret_cast<size_t*>(curr) & ~size_t(1);

        if (curr + block_size > mem_end)
        {
            break;
        }

        result.emplace_back(block_size, is_free);
        curr += block_size;
    }

    return result;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return {get_head()};
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() noexcept
{
    return {};
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return {const_cast<allocator_sorted_list*>(this)};
}

void allocator_sorted_list::sorted_iterator::set_trusted_memory(void* mem) noexcept
{
    _trusted_memory = mem;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    auto it = sorted_iterator();
    it.set_trusted_memory(const_cast<allocator_sorted_list*>(this));
    return it;
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return _free_ptr != other._free_ptr;
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr != nullptr)
    {
        _free_ptr = *reinterpret_cast<void**>(static_cast<char*>(_free_ptr) + sizeof(size_t));
    }
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    auto tmp = *this;
    ++*this;
    return tmp;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    if (_free_ptr == nullptr)
    {
        return 0;
    }

    size_t size = *static_cast<size_t*>(_free_ptr) & ~size_t(1);
    return size;
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    if (_free_ptr == nullptr)
    {
        return nullptr;
    }
    return static_cast<char*>(_free_ptr) + sizeof(size_t);
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() : _free_ptr(nullptr) {}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted) : _free_ptr(trusted) {}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr && _current_ptr == other._current_ptr;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (_free_ptr != nullptr)
    {
        _free_ptr = *reinterpret_cast<void**>(static_cast<char*>(_free_ptr) + sizeof(size_t));
    }
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    auto tmp = *this;
    ++*this;
    return tmp;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    if (_free_ptr == nullptr)
    {
        return 0;
    }

    size_t size = *static_cast<size_t*>(_free_ptr) & ~size_t(1);
    return size;
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    if (_free_ptr == nullptr)
    {
        return nullptr;
    }
    return static_cast<char*>(_free_ptr) + sizeof(size_t);
}

allocator_sorted_list::sorted_iterator::sorted_iterator() : _free_ptr(nullptr), _current_ptr(nullptr), _trusted_memory(nullptr) {}

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted) : _free_ptr(nullptr), _current_ptr(nullptr), _trusted_memory(trusted)
{
    if (_trusted_memory != nullptr)
    {
        auto alloc = static_cast<allocator_sorted_list*>(trusted);
        char* mem_st = static_cast<char*>(alloc->_trusted_memory);
        _current_ptr = mem_st + allocator_sorted_list::allocator_metadata_size;
        if (_current_ptr >= mem_st + *alloc->get_full_size())
        {
            _current_ptr = nullptr;
        }
    }
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    if (_current_ptr == nullptr)
    {
        return false;
    }

    bool is_free = (*static_cast<size_t*>(_current_ptr) & 1) == 0;
    return is_free;
}
