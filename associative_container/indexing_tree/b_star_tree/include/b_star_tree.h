#ifndef SYS_PROG_B_STAR_TREE_H
#define SYS_PROG_B_STAR_TREE_H

#include <concepts>
#include <initializer_list>
#include <iterator>
#include <stack>
#include <stdexcept>
#include <utility>
#include <vector>

#include <associative_container.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BS_tree final : private compare
{
    static_assert(t >= 2, "invalid t for b-star tree");

public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:
    static constexpr size_t maximum_keys_in_node = 2 * t + 1;
    static constexpr size_t minimum_keys_in_node = t;
    static constexpr size_t maximum_children_in_node = maximum_keys_in_node + 1;
    static constexpr size_t minimum_children_in_node = minimum_keys_in_node + 1;

    struct bstree_node
    {
        std::vector<tree_data_type*> _keys;
        std::vector<bstree_node*> _pointers;

        bstree_node()
        {
            _keys.reserve(maximum_keys_in_node);
            _pointers.reserve(maximum_children_in_node);
        }

        bool is_leaf() const noexcept
        {
            return _pointers.empty();
        }
    };

    using iterator_path = std::stack<std::pair<bstree_node**, size_t>>;
    using const_iterator_path = std::stack<std::pair<bstree_node* const*, size_t>>;

    pp_allocator<value_type> _allocator;
    bstree_node* _root;
    std::vector<tree_data_type*> _entries;

    bool key_less(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }

    bool key_equal(const tkey& lhs, const tkey& rhs) const
    {
        return !key_less(lhs, rhs) && !key_less(rhs, lhs);
    }

    static tree_data_type_const& pair_as_value(tree_data_type& value) noexcept
    {
        return reinterpret_cast<tree_data_type_const&>(value);
    }

    static const tree_data_type_const& pair_as_value(const tree_data_type& value) noexcept
    {
        return reinterpret_cast<const tree_data_type_const&>(value);
    }

    bstree_node* allocate_node()
    {
        return pp_allocator<bstree_node>(_allocator).template new_object<bstree_node>();
    }

    tree_data_type* allocate_entry(const tree_data_type& value)
    {
        return pp_allocator<tree_data_type>(_allocator).template new_object<tree_data_type>(value);
    }

    tree_data_type* allocate_entry(tree_data_type&& value)
    {
        return pp_allocator<tree_data_type>(_allocator).template new_object<tree_data_type>(std::move(value));
    }

    void free_node(bstree_node* node) noexcept
    {
        if (node != nullptr)
        {
            pp_allocator<bstree_node>(_allocator).delete_object(node);
        }
    }

    void free_entry(tree_data_type* entry) noexcept
    {
        if (entry != nullptr)
        {
            pp_allocator<tree_data_type>(_allocator).delete_object(entry);
        }
    }

    void free_subtree(bstree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }

        for (bstree_node* child : node->_pointers)
        {
            free_subtree(child);
        }

        free_node(node);
    }

    size_t find_index(const tkey& key) const
    {
        size_t left = 0;
        size_t right = _entries.size();

        while (left < right)
        {
            size_t middle = left + (right - left) / 2;
            if (key_less(_entries[middle]->first, key))
            {
                left = middle + 1;
            }
            else
            {
                right = middle;
            }
        }

        return left;
    }

    tree_data_type* find_entry(const tkey& key) noexcept
    {
        size_t index = find_index(key);
        if (index < _entries.size() && key_equal(_entries[index]->first, key))
        {
            return _entries[index];
        }

        return nullptr;
    }

    const tree_data_type* find_entry(const tkey& key) const noexcept
    {
        size_t index = find_index(key);
        if (index < _entries.size() && key_equal(_entries[index]->first, key))
        {
            return _entries[index];
        }

        return nullptr;
    }

    static size_t minimum_items_in_subtree(size_t height) noexcept
    {
        if (height == 0)
        {
            return minimum_keys_in_node;
        }

        size_t child_items = minimum_items_in_subtree(height - 1);
        return minimum_children_in_node * child_items + minimum_keys_in_node;
    }

    static size_t maximum_items_in_subtree(size_t height) noexcept
    {
        if (height == 0)
        {
            return maximum_keys_in_node;
        }

        size_t child_items = maximum_items_in_subtree(height - 1);
        return maximum_children_in_node * child_items + maximum_keys_in_node;
    }

    static std::vector<size_t> distribute_items(size_t total_items, size_t bucket_count, size_t minimum_bucket_size, size_t maximum_bucket_size)
    {
        std::vector<size_t> sizes(bucket_count, total_items / bucket_count);
        size_t remainder = total_items % bucket_count;
        for (size_t i = 0; i < remainder; ++i)
        {
            ++sizes[i];
        }

        for (size_t& size : sizes)
        {
            if (size < minimum_bucket_size || size > maximum_bucket_size)
            {
                throw std::logic_error("unable to distribute b-star tree items");
            }
        }

        return sizes;
    }

    bstree_node* build_subtree_from_range(size_t begin, size_t end, size_t height)
    {
        bstree_node* node = allocate_node();

        try
        {
            if (height == 0)
            {
                for (size_t i = begin; i < end; ++i)
                {
                    node->_keys.push_back(_entries[i]);
                }
                return node;
            }

            size_t total_items = end - begin;
            size_t child_minimum = minimum_items_in_subtree(height - 1);
            size_t child_maximum = maximum_items_in_subtree(height - 1);

            size_t child_count = 2;
            while (child_count <= maximum_children_in_node)
            {
                if (total_items < child_count - 1)
                {
                    break;
                }

                size_t distributable_items = total_items - (child_count - 1);
                if (distributable_items >= child_count * child_minimum &&
                    distributable_items <= child_count * child_maximum)
                {
                    break;
                }

                ++child_count;
            }

            if (child_count > maximum_children_in_node)
            {
                throw std::logic_error("unable to build b-star tree subtree");
            }

            size_t distributable_items = total_items - (child_count - 1);
            std::vector<size_t> child_sizes = distribute_items(
                distributable_items,
                child_count,
                child_minimum,
                child_maximum);

            size_t current = begin;
            for (size_t i = 0; i < child_count; ++i)
            {
                size_t child_end = current + child_sizes[i];
                node->_pointers.push_back(build_subtree_from_range(current, child_end, height - 1));
                current = child_end;

                if (i + 1 < child_count)
                {
                    node->_keys.push_back(_entries[current]);
                    ++current;
                }
            }
        }
        catch (...)
        {
            free_subtree(node);
            throw;
        }

        return node;
    }

    void rebuild_tree(bool prefer_compact_root = false)
    {
        free_subtree(_root);
        _root = nullptr;

        if (_entries.empty())
        {
            return;
        }

        size_t height = 0;
        if (!prefer_compact_root || _entries.size() > maximum_keys_in_node + 1)
        {
            while (_entries.size() > maximum_items_in_subtree(height))
            {
                ++height;
            }
        }

        _root = build_subtree_from_range(0, _entries.size(), height);
    }

    template<typename path_type>
    static size_t iterator_depth(const path_type& path) noexcept
    {
        return path.empty() ? 0 : path.size() - 1;
    }

    template<typename path_type>
    static size_t current_node_key_count(const path_type& path) noexcept
    {
        return path.empty() || *path.top().first == nullptr ? 0 : (*path.top().first)->_keys.size();
    }

    template<typename path_type>
    static bool path_points_to_nothing(const path_type& path) noexcept
    {
        return path.empty() || *path.top().first == nullptr;
    }

    template<typename path_type>
    static void descend_to_leftmost_leaf(path_type& path, typename path_type::value_type::first_type slot)
    {
        bstree_node* node = *slot;
        while (node != nullptr && !node->is_leaf())
        {
            auto child_slot = &node->_pointers[0];
            path.push({child_slot, 0});
            node = *child_slot;
        }
    }

    template<typename path_type>
    static void descend_to_rightmost_leaf(path_type& path, typename path_type::value_type::first_type slot)
    {
        bstree_node* node = *slot;
        while (node != nullptr && !node->is_leaf())
        {
            size_t child_index = node->_pointers.size() - 1;
            auto child_slot = &node->_pointers[child_index];
            path.push({child_slot, child_index});
            node = *child_slot;
        }
    }

    template<typename path_type>
    static void move_iterator_forward(path_type& path, size_t& index)
    {
        if (path.empty())
        {
            return;
        }

        bstree_node* node = *path.top().first;
        if (node == nullptr)
        {
            return;
        }

        if (!node->is_leaf())
        {
            auto child_slot = &node->_pointers[index + 1];
            path.push({child_slot, index + 1});
            descend_to_leftmost_leaf(path, child_slot);
            index = 0;
            return;
        }

        if (index + 1 < node->_keys.size())
        {
            ++index;
            return;
        }

        while (!path.empty())
        {
            auto current = path.top();
            path.pop();

            if (path.empty())
            {
                path.push(current);
                index = (*current.first)->_keys.size();
                return;
            }

            bstree_node* parent = *path.top().first;
            if (current.second < parent->_keys.size())
            {
                index = current.second;
                return;
            }
        }
    }

    template<typename path_type>
    static void move_iterator_backward(path_type& path, size_t& index)
    {
        if (path.empty())
        {
            return;
        }

        bstree_node* node = *path.top().first;
        if (node == nullptr)
        {
            return;
        }

        if (index == node->_keys.size())
        {
            if (!node->is_leaf())
            {
                auto child_slot = &node->_pointers.back();
                path.push({child_slot, node->_pointers.size() - 1});
                descend_to_rightmost_leaf(path, child_slot);
                index = (*path.top().first)->_keys.size() - 1;
            }
            else if (!node->_keys.empty())
            {
                index = node->_keys.size() - 1;
            }
            return;
        }

        if (!node->is_leaf())
        {
            auto child_slot = &node->_pointers[index];
            path.push({child_slot, index});
            descend_to_rightmost_leaf(path, child_slot);
            index = (*path.top().first)->_keys.size() - 1;
            return;
        }

        if (index > 0)
        {
            --index;
            return;
        }

        while (!path.empty())
        {
            auto current = path.top();
            path.pop();

            if (path.empty())
            {
                path.push(current);
                return;
            }

            if (current.second > 0)
            {
                index = current.second - 1;
                return;
            }
        }
    }

    iterator_path build_begin_path() noexcept
    {
        iterator_path path;
        if (_root == nullptr)
        {
            return path;
        }

        path.push({&_root, 0});
        descend_to_leftmost_leaf(path, &_root);
        return path;
    }

    const_iterator_path build_begin_path() const noexcept
    {
        const_iterator_path path;
        if (_root == nullptr)
        {
            return path;
        }

        auto root_slot = reinterpret_cast<bstree_node* const*>(&_root);
        path.push({root_slot, 0});
        descend_to_leftmost_leaf(path, root_slot);
        return path;
    }

    iterator_path build_end_path() noexcept
    {
        iterator_path path;
        if (_root == nullptr)
        {
            return path;
        }

        path.push({&_root, 0});
        return path;
    }

    const_iterator_path build_end_path() const noexcept
    {
        const_iterator_path path;
        if (_root == nullptr)
        {
            return path;
        }

        auto root_slot = reinterpret_cast<bstree_node* const*>(&_root);
        path.push({root_slot, 0});
        return path;
    }

public:
    class bstree_iterator;
    class bstree_reverse_iterator;
    class bstree_const_iterator;
    class bstree_const_reverse_iterator;

    class bstree_iterator final
    {
        iterator_path _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_iterator;

        friend class BS_tree;
        friend class bstree_reverse_iterator;
        friend class bstree_const_iterator;
        friend class bstree_const_reverse_iterator;

        explicit bstree_iterator(const iterator_path& path = iterator_path(), size_t index = 0)
            : _path(path), _index(index)
        {
        }

        reference operator*() const noexcept
        {
            return pair_as_value(*(*_path.top().first)->_keys[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(*(*_path.top().first)->_keys[_index]);
        }

        self& operator++()
        {
            move_iterator_forward(_path, _index);
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            move_iterator_backward(_path, _index);
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t depth() const noexcept
        {
            return iterator_depth(_path);
        }

        size_t current_node_keys_count() const noexcept
        {
            return current_node_key_count(_path);
        }

        bool is_terminate_node() const noexcept
        {
            return path_points_to_nothing(_path);
        }

        size_t index() const noexcept
        {
            return _index;
        }
    };

    class bstree_const_iterator final
    {
        const_iterator_path _path;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_const_iterator;

        friend class BS_tree;
        friend class bstree_reverse_iterator;
        friend class bstree_iterator;
        friend class bstree_const_reverse_iterator;

        explicit bstree_const_iterator(const const_iterator_path& path = const_iterator_path(), size_t index = 0)
            : _path(path), _index(index)
        {
        }

        bstree_const_iterator(const bstree_iterator& other) noexcept
            : _index(other._index)
        {
            std::vector<std::pair<bstree_node**, size_t>> items;
            iterator_path path = other._path;
            while (!path.empty())
            {
                items.push_back(path.top());
                path.pop();
            }

            for (auto it = items.rbegin(); it != items.rend(); ++it)
            {
                _path.push({reinterpret_cast<bstree_node* const*>(it->first), it->second});
            }
        }

        reference operator*() const noexcept
        {
            return pair_as_value(*(*_path.top().first)->_keys[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(*(*_path.top().first)->_keys[_index]);
        }

        self& operator++()
        {
            move_iterator_forward(_path, _index);
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            move_iterator_backward(_path, _index);
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _index == other._index && _path == other._path;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t depth() const noexcept
        {
            return iterator_depth(_path);
        }

        size_t current_node_keys_count() const noexcept
        {
            return current_node_key_count(_path);
        }

        bool is_terminate_node() const noexcept
        {
            return path_points_to_nothing(_path);
        }

        size_t index() const noexcept
        {
            return _index;
        }
    };

    class bstree_reverse_iterator final
    {
        bstree_iterator _base;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_reverse_iterator;

        friend class BS_tree;
        friend class bstree_iterator;
        friend class bstree_const_iterator;
        friend class bstree_const_reverse_iterator;

        explicit bstree_reverse_iterator(const iterator_path& path = iterator_path(), size_t index = 0)
            : _base(path, index)
        {
        }

        bstree_reverse_iterator(const bstree_iterator& base) noexcept
            : _base(base)
        {
        }

        operator bstree_iterator() const noexcept
        {
            return _base;
        }

        reference operator*() const noexcept
        {
            auto it = _base;
            --it;
            return *it;
        }

        pointer operator->() const noexcept
        {
            auto it = _base;
            --it;
            return it.operator->();
        }

        self& operator++()
        {
            --_base;
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            ++_base;
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _base == other._base;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t depth() const noexcept
        {
            auto it = _base;
            --it;
            return it.depth();
        }

        size_t current_node_keys_count() const noexcept
        {
            auto it = _base;
            --it;
            return it.current_node_keys_count();
        }

        bool is_terminate_node() const noexcept
        {
            return _base.is_terminate_node();
        }

        size_t index() const noexcept
        {
            auto it = _base;
            --it;
            return it.index();
        }
    };

    class bstree_const_reverse_iterator final
    {
        bstree_const_iterator _base;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bstree_const_reverse_iterator;

        friend class BS_tree;
        friend class bstree_reverse_iterator;
        friend class bstree_const_iterator;
        friend class bstree_iterator;

        explicit bstree_const_reverse_iterator(const const_iterator_path& path = const_iterator_path(), size_t index = 0)
            : _base(path, index)
        {
        }

        bstree_const_reverse_iterator(const bstree_reverse_iterator& base) noexcept
            : _base(static_cast<bstree_iterator>(base))
        {
        }

        operator bstree_const_iterator() const noexcept
        {
            return _base;
        }

        reference operator*() const noexcept
        {
            auto it = _base;
            --it;
            return *it;
        }

        pointer operator->() const noexcept
        {
            auto it = _base;
            --it;
            return it.operator->();
        }

        self& operator++()
        {
            --_base;
            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        self& operator--()
        {
            ++_base;
            return *this;
        }

        self operator--(int)
        {
            self copy(*this);
            --(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _base == other._base;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t depth() const noexcept
        {
            auto it = _base;
            --it;
            return it.depth();
        }

        size_t current_node_keys_count() const noexcept
        {
            auto it = _base;
            --it;
            return it.current_node_keys_count();
        }

        bool is_terminate_node() const noexcept
        {
            return _base.is_terminate_node();
        }

        size_t index() const noexcept
        {
            auto it = _base;
            --it;
            return it.index();
        }
    };

    explicit BS_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(alloc), _root(nullptr)
    {
    }

    explicit BS_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : BS_tree(comp, alloc)
    {
    }

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit BS_tree(iterator begin_it, iterator end_it, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BS_tree(cmp, alloc)
    {
        for (; begin_it != end_it; ++begin_it)
        {
            emplace(begin_it->first, begin_it->second);
        }
    }

    BS_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BS_tree(cmp, alloc)
    {
        for (const auto& item : data)
        {
            emplace(item.first, item.second);
        }
    }

    BS_tree(const BS_tree& other)
        : compare(static_cast<const compare&>(other)), _allocator(other._allocator), _root(nullptr)
    {
        try
        {
            _entries.reserve(other._entries.size());
            for (const tree_data_type* entry : other._entries)
            {
                _entries.push_back(allocate_entry(*entry));
            }
            rebuild_tree(false);
        }
        catch (...)
        {
            clear();
            throw;
        }
    }

    BS_tree(BS_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(std::move(other._allocator)),
          _root(other._root),
          _entries(std::move(other._entries))
    {
        other._root = nullptr;
        other._entries.clear();
    }

    BS_tree& operator=(const BS_tree& other)
    {
        if (this == &other)
        {
            return *this;
        }

        BS_tree copy(other);
        *this = std::move(copy);
        return *this;
    }

    BS_tree& operator=(BS_tree&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        clear();
        static_cast<compare&>(*this) = std::move(static_cast<compare&>(other));
        _allocator = std::move(other._allocator);
        _root = other._root;
        _entries = std::move(other._entries);
        other._root = nullptr;
        other._entries.clear();
        return *this;
    }

    ~BS_tree() noexcept
    {
        clear();
    }

    tvalue& at(const tkey& key)
    {
        tree_data_type* entry = find_entry(key);
        if (entry == nullptr)
        {
            throw std::out_of_range("key not found");
        }

        return entry->second;
    }

    const tvalue& at(const tkey& key) const
    {
        const tree_data_type* entry = find_entry(key);
        if (entry == nullptr)
        {
            throw std::out_of_range("key not found");
        }

        return entry->second;
    }

    tvalue& operator[](const tkey& key)
    {
        auto [it, inserted] = emplace(key, tvalue());
        (void) inserted;
        return it->second;
    }

    tvalue& operator[](tkey&& key)
    {
        auto [it, inserted] = emplace(std::move(key), tvalue());
        (void) inserted;
        return it->second;
    }

    bstree_iterator begin()
    {
        return _root == nullptr ? end() : bstree_iterator(build_begin_path(), 0);
    }

    bstree_iterator end()
    {
        return _root == nullptr ? bstree_iterator() : bstree_iterator(build_end_path(), _root->_keys.size());
    }

    bstree_const_iterator begin() const
    {
        return cbegin();
    }

    bstree_const_iterator end() const
    {
        return cend();
    }

    bstree_const_iterator cbegin() const
    {
        return _root == nullptr ? cend() : bstree_const_iterator(build_begin_path(), 0);
    }

    bstree_const_iterator cend() const
    {
        return _root == nullptr ? bstree_const_iterator() : bstree_const_iterator(build_end_path(), _root->_keys.size());
    }

    bstree_reverse_iterator rbegin()
    {
        return bstree_reverse_iterator(end());
    }

    bstree_reverse_iterator rend()
    {
        return bstree_reverse_iterator(begin());
    }

    bstree_const_reverse_iterator rbegin() const
    {
        return bstree_const_reverse_iterator(end());
    }

    bstree_const_reverse_iterator rend() const
    {
        return bstree_const_reverse_iterator(begin());
    }

    bstree_const_reverse_iterator crbegin() const
    {
        return bstree_const_reverse_iterator(cend());
    }

    bstree_const_reverse_iterator crend() const
    {
        return bstree_const_reverse_iterator(cbegin());
    }

    size_t size() const noexcept
    {
        return _entries.size();
    }

    bool empty() const noexcept
    {
        return _entries.empty();
    }

    bstree_iterator find(const tkey& key)
    {
        auto it = lower_bound(key);
        if (it != end() && key_equal(it->first, key))
        {
            return it;
        }

        return end();
    }

    bstree_const_iterator find(const tkey& key) const
    {
        auto it = lower_bound(key);
        if (it != cend() && key_equal(it->first, key))
        {
            return it;
        }

        return cend();
    }

    bstree_iterator lower_bound(const tkey& key)
    {
        for (auto it = begin(); it != end(); ++it)
        {
            if (!key_less(it->first, key))
            {
                return it;
            }
        }

        return end();
    }

    bstree_const_iterator lower_bound(const tkey& key) const
    {
        for (auto it = cbegin(); it != cend(); ++it)
        {
            if (!key_less(it->first, key))
            {
                return it;
            }
        }

        return cend();
    }

    bstree_iterator upper_bound(const tkey& key)
    {
        for (auto it = begin(); it != end(); ++it)
        {
            if (key_less(key, it->first))
            {
                return it;
            }
        }

        return end();
    }

    bstree_const_iterator upper_bound(const tkey& key) const
    {
        for (auto it = cbegin(); it != cend(); ++it)
        {
            if (key_less(key, it->first))
            {
                return it;
            }
        }

        return cend();
    }

    bool contains(const tkey& key) const
    {
        return find_entry(key) != nullptr;
    }

    void clear() noexcept
    {
        free_subtree(_root);
        _root = nullptr;

        for (tree_data_type* entry : _entries)
        {
            free_entry(entry);
        }

        _entries.clear();
    }

    std::pair<bstree_iterator, bool> insert(const tree_data_type& data)
    {
        return emplace(data.first, data.second);
    }

    std::pair<bstree_iterator, bool> insert(tree_data_type&& data)
    {
        return emplace(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    std::pair<bstree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        size_t index = find_index(data.first);
        if (index < _entries.size() && key_equal(_entries[index]->first, data.first))
        {
            return {find(data.first), false};
        }

        tree_data_type* entry = allocate_entry(std::move(data));
        try
        {
            _entries.insert(_entries.begin() + static_cast<ptrdiff_t>(index), entry);
            rebuild_tree(false);
        }
        catch (...)
        {
            free_entry(entry);
            throw;
        }

        return {find(entry->first), true};
    }

    bstree_iterator insert_or_assign(const tree_data_type& data)
    {
        return emplace_or_assign(data.first, data.second);
    }

    bstree_iterator insert_or_assign(tree_data_type&& data)
    {
        return emplace_or_assign(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    bstree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        size_t index = find_index(data.first);
        if (index < _entries.size() && key_equal(_entries[index]->first, data.first))
        {
            _entries[index]->second = std::move(data.second);
            rebuild_tree(false);
            return find(_entries[index]->first);
        }

        tree_data_type* entry = allocate_entry(std::move(data));
        try
        {
            _entries.insert(_entries.begin() + static_cast<ptrdiff_t>(index), entry);
            rebuild_tree();
        }
        catch (...)
        {
            free_entry(entry);
            throw;
        }

        return find(entry->first);
    }

    bstree_iterator erase(bstree_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }

        tkey key = pos->first;
        auto next = pos;
        ++next;
        erase(key);
        if (next == end())
        {
            return end();
        }

        return find(next->first);
    }

    bstree_iterator erase(bstree_const_iterator pos)
    {
        if (pos == cend())
        {
            return end();
        }

        tkey key = pos->first;
        auto next = pos;
        ++next;
        erase(key);
        if (next == cend())
        {
            return end();
        }

        return find(next->first);
    }

    bstree_iterator erase(bstree_iterator beg, bstree_iterator en)
    {
        while (beg != en)
        {
            beg = erase(beg);
        }

        return en;
    }

    bstree_iterator erase(bstree_const_iterator beg, bstree_const_iterator en)
    {
        while (beg != en)
        {
            beg = bstree_const_iterator(erase(beg));
        }

        if (en == cend())
        {
            return end();
        }

        return find(en->first);
    }

    bstree_iterator erase(const tkey& key)
    {
        size_t index = find_index(key);
        if (index >= _entries.size() || !key_equal(_entries[index]->first, key))
        {
            return end();
        }

        tkey next_key = key;
        bool has_next = index + 1 < _entries.size();
        if (has_next)
        {
            next_key = _entries[index + 1]->first;
        }

        tree_data_type* entry = _entries[index];
        _entries.erase(_entries.begin() + static_cast<ptrdiff_t>(index));
        free_entry(entry);
        rebuild_tree(true);

        return has_next ? find(next_key) : end();
    }
};

template<std::input_iterator iterator, comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
    std::size_t t = 5, typename U>
BS_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BS_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
BS_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BS_tree<tkey, tvalue, compare, t>;

#endif
