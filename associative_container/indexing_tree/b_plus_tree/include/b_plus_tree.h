#ifndef SYS_PROG_B_PLUS_TREE_H
#define SYS_PROG_B_PLUS_TREE_H

#include <algorithm>
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include <associative_container.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BP_tree final : private compare
{
    static_assert(t >= 2, "invalid t for b-plus tree");

public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:
    static constexpr size_t minimum_leaf_keys = t - 1;
    static constexpr size_t maximum_leaf_keys = 2 * t - 1;
    static constexpr size_t minimum_children = t;
    static constexpr size_t maximum_children = 2 * t;

    struct bptree_node
    {
        bool _is_leaf;
        bptree_node* _parent;
        bptree_node* _next;
        bptree_node* _prev;
        std::vector<tree_data_type*> _entries;
        std::vector<tkey> _keys;
        std::vector<bptree_node*> _children;

        explicit bptree_node(bool is_leaf)
            : _is_leaf(is_leaf), _parent(nullptr), _next(nullptr), _prev(nullptr)
        {
            if (is_leaf)
            {
                _entries.reserve(maximum_leaf_keys + 1);
            }
            else
            {
                _keys.reserve(maximum_children);
                _children.reserve(maximum_children + 1);
            }
        }
    };

    pp_allocator<value_type> _allocator;
    bptree_node* _root;
    bptree_node* _head;
    size_t _size;

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

    tree_data_type* allocate_entry(const tree_data_type& value)
    {
        return pp_allocator<tree_data_type>(_allocator).template new_object<tree_data_type>(value);
    }

    tree_data_type* allocate_entry(tree_data_type&& value)
    {
        return pp_allocator<tree_data_type>(_allocator).template new_object<tree_data_type>(std::move(value));
    }

    void free_entry(tree_data_type* entry) noexcept
    {
        if (entry != nullptr)
        {
            pp_allocator<tree_data_type>(_allocator).delete_object(entry);
        }
    }

    bptree_node* allocate_node(bool is_leaf)
    {
        return new bptree_node(is_leaf);
    }

    void free_node(bptree_node* node) noexcept
    {
        delete node;
    }

    void free_subtree(bptree_node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }

        if (node->_is_leaf)
        {
            for (tree_data_type* entry : node->_entries)
            {
                free_entry(entry);
            }
        }
        else
        {
            for (bptree_node* child : node->_children)
            {
                free_subtree(child);
            }
        }

        free_node(node);
    }

    const tkey& first_key(const bptree_node* node) const
    {
        const bptree_node* current = node;
        while (!current->_is_leaf)
        {
            current = current->_children.front();
        }

        return current->_entries.front()->first;
    }

    void rebuild_internal_keys(bptree_node* node)
    {
        if (node == nullptr || node->_is_leaf)
        {
            return;
        }

        node->_keys.clear();
        for (size_t i = 1; i < node->_children.size(); ++i)
        {
            node->_keys.push_back(first_key(node->_children[i]));
        }
    }

    void refresh_keys_upward(bptree_node* node)
    {
        while (node != nullptr)
        {
            rebuild_internal_keys(node);
            node = node->_parent;
        }
    }

    size_t child_index_in_parent(const bptree_node* node) const
    {
        const bptree_node* parent = node->_parent;
        for (size_t i = 0; i < parent->_children.size(); ++i)
        {
            if (parent->_children[i] == node)
            {
                return i;
            }
        }

        throw std::logic_error("b-plus tree parent-child relationship is broken");
    }

    size_t lower_bound_index_in_leaf(const bptree_node* leaf, const tkey& key) const
    {
        size_t left = 0;
        size_t right = leaf->_entries.size();
        while (left < right)
        {
            size_t middle = left + (right - left) / 2;
            if (key_less(leaf->_entries[middle]->first, key))
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

    bptree_node* leftmost_leaf() const noexcept
    {
        bptree_node* node = _root;
        while (node != nullptr && !node->_is_leaf)
        {
            node = node->_children.front();
        }
        return node;
    }

    bptree_node* find_leaf(const tkey& key) const noexcept
    {
        bptree_node* node = _root;
        while (node != nullptr && !node->_is_leaf)
        {
            size_t child_index = 0;
            while (child_index < node->_keys.size() && !key_less(key, node->_keys[child_index]))
            {
                ++child_index;
            }
            node = node->_children[child_index];
        }

        return node;
    }

    void insert_child_after(bptree_node* parent, bptree_node* left_child, bptree_node* right_child)
    {
        size_t index = child_index_in_parent(left_child);
        parent->_children.insert(parent->_children.begin() + static_cast<ptrdiff_t>(index + 1), right_child);
        right_child->_parent = parent;
        rebuild_internal_keys(parent);
    }

    void split_internal_if_needed(bptree_node* node)
    {
        while (node != nullptr && node->_children.size() > maximum_children)
        {
            size_t left_child_count = t;
            auto* right = allocate_node(false);

            right->_children.assign(node->_children.begin() + static_cast<ptrdiff_t>(left_child_count), node->_children.end());
            for (bptree_node* child : right->_children)
            {
                child->_parent = right;
            }

            node->_children.erase(node->_children.begin() + static_cast<ptrdiff_t>(left_child_count), node->_children.end());
            rebuild_internal_keys(node);
            rebuild_internal_keys(right);

            if (node->_parent == nullptr)
            {
                auto* new_root = allocate_node(false);
                new_root->_children.push_back(node);
                new_root->_children.push_back(right);
                node->_parent = new_root;
                right->_parent = new_root;
                rebuild_internal_keys(new_root);
                _root = new_root;
                return;
            }

            bptree_node* parent = node->_parent;
            insert_child_after(parent, node, right);
            node = parent;
        }
    }

    void split_leaf_if_needed(bptree_node* leaf)
    {
        if (leaf->_entries.size() <= maximum_leaf_keys)
        {
            refresh_keys_upward(leaf->_parent);
            return;
        }

        auto* right = allocate_node(true);
        size_t split_index = t;

        right->_entries.assign(leaf->_entries.begin() + static_cast<ptrdiff_t>(split_index), leaf->_entries.end());
        leaf->_entries.erase(leaf->_entries.begin() + static_cast<ptrdiff_t>(split_index), leaf->_entries.end());

        right->_next = leaf->_next;
        right->_prev = leaf;
        if (leaf->_next != nullptr)
        {
            leaf->_next->_prev = right;
        }
        leaf->_next = right;

        if (_head == nullptr || _head == leaf->_next)
        {
            _head = leftmost_leaf();
        }

        if (leaf->_parent == nullptr)
        {
            auto* new_root = allocate_node(false);
            new_root->_children.push_back(leaf);
            new_root->_children.push_back(right);
            leaf->_parent = new_root;
            right->_parent = new_root;
            rebuild_internal_keys(new_root);
            _root = new_root;
            _head = leftmost_leaf();
            return;
        }

        bptree_node* parent = leaf->_parent;
        insert_child_after(parent, leaf, right);
        split_internal_if_needed(parent);
        _head = leftmost_leaf();
    }

    void rebalance_internal_after_erase(bptree_node* node)
    {
        while (node != nullptr)
        {
            if (node == _root)
            {
                if (!_root->_is_leaf && _root->_children.size() == 1)
                {
                    bptree_node* old_root = _root;
                    _root = _root->_children.front();
                    _root->_parent = nullptr;
                    old_root->_children.clear();
                    free_node(old_root);
                }
                rebuild_internal_keys(_root);
                return;
            }

            if (node->_children.size() >= minimum_children)
            {
                refresh_keys_upward(node);
                return;
            }

            bptree_node* parent = node->_parent;
            size_t index = child_index_in_parent(node);
            bptree_node* left = index > 0 ? parent->_children[index - 1] : nullptr;
            bptree_node* right = index + 1 < parent->_children.size() ? parent->_children[index + 1] : nullptr;

            if (left != nullptr && left->_children.size() > minimum_children)
            {
                node->_children.insert(node->_children.begin(), left->_children.back());
                left->_children.back()->_parent = node;
                left->_children.pop_back();
                rebuild_internal_keys(left);
                refresh_keys_upward(node);
                return;
            }

            if (right != nullptr && right->_children.size() > minimum_children)
            {
                node->_children.push_back(right->_children.front());
                right->_children.front()->_parent = node;
                right->_children.erase(right->_children.begin());
                rebuild_internal_keys(right);
                refresh_keys_upward(node);
                return;
            }

            if (left != nullptr)
            {
                left->_children.insert(left->_children.end(), node->_children.begin(), node->_children.end());
                for (bptree_node* child : node->_children)
                {
                    child->_parent = left;
                }
                node->_children.clear();
                parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
                free_node(node);
                rebuild_internal_keys(left);
                rebuild_internal_keys(parent);
                node = parent;
                continue;
            }

            right->_children.insert(right->_children.begin(), node->_children.begin(), node->_children.end());
            for (bptree_node* child : node->_children)
            {
                child->_parent = right;
            }
            node->_children.clear();
            parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
            free_node(node);
            rebuild_internal_keys(right);
            rebuild_internal_keys(parent);
            node = parent;
        }
    }

    void rebalance_leaf_after_erase(bptree_node* leaf)
    {
        if (leaf == _root)
        {
            if (leaf->_entries.empty())
            {
                free_node(_root);
                _root = nullptr;
                _head = nullptr;
            }
            return;
        }

        if (leaf->_entries.size() >= minimum_leaf_keys)
        {
            refresh_keys_upward(leaf->_parent);
            return;
        }

        bptree_node* parent = leaf->_parent;
        size_t index = child_index_in_parent(leaf);
        bptree_node* left = index > 0 ? parent->_children[index - 1] : nullptr;
        bptree_node* right = index + 1 < parent->_children.size() ? parent->_children[index + 1] : nullptr;

        if (left != nullptr && left->_entries.size() > minimum_leaf_keys)
        {
            leaf->_entries.insert(leaf->_entries.begin(), left->_entries.back());
            left->_entries.pop_back();
            refresh_keys_upward(parent);
            return;
        }

        if (right != nullptr && right->_entries.size() > minimum_leaf_keys)
        {
            leaf->_entries.push_back(right->_entries.front());
            right->_entries.erase(right->_entries.begin());
            refresh_keys_upward(parent);
            return;
        }

        if (left != nullptr)
        {
            left->_entries.insert(left->_entries.end(), leaf->_entries.begin(), leaf->_entries.end());
            left->_next = leaf->_next;
            if (leaf->_next != nullptr)
            {
                leaf->_next->_prev = left;
            }
            parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
            free_node(leaf);
            rebuild_internal_keys(parent);
            rebalance_internal_after_erase(parent);
            _head = leftmost_leaf();
            return;
        }

        right->_entries.insert(right->_entries.begin(), leaf->_entries.begin(), leaf->_entries.end());
        right->_prev = leaf->_prev;
        if (leaf->_prev != nullptr)
        {
            leaf->_prev->_next = right;
        }
        else
        {
            _head = right;
        }
        parent->_children.erase(parent->_children.begin() + static_cast<ptrdiff_t>(index));
        free_node(leaf);
        rebuild_internal_keys(parent);
        rebalance_internal_after_erase(parent);
        _head = leftmost_leaf();
    }

    void insert_new_entry(tree_data_type* entry)
    {
        if (_root == nullptr)
        {
            _root = allocate_node(true);
            _root->_entries.push_back(entry);
            _head = _root;
            ++_size;
            return;
        }

        bptree_node* leaf = find_leaf(entry->first);
        size_t index = lower_bound_index_in_leaf(leaf, entry->first);
        leaf->_entries.insert(leaf->_entries.begin() + static_cast<ptrdiff_t>(index), entry);
        ++_size;
        split_leaf_if_needed(leaf);
    }

public:
    class bptree_iterator;
    class bptree_const_iterator;

    class bptree_iterator final
    {
        bptree_node* _node;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bptree_iterator;

        friend class BP_tree;
        friend class bptree_const_iterator;

        explicit bptree_iterator(bptree_node* node = nullptr, size_t index = 0)
            : _node(node), _index(index)
        {
        }

        reference operator*() const noexcept
        {
            return pair_as_value(*_node->_entries[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(*_node->_entries[_index]);
        }

        self& operator++()
        {
            if (_node == nullptr)
            {
                return *this;
            }

            ++_index;
            if (_index >= _node->_entries.size())
            {
                _node = _node->_next;
                _index = 0;
            }

            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _node == other._node && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t current_node_keys_count() const noexcept
        {
            return _node == nullptr ? 0 : _node->_entries.size();
        }

        size_t index() const noexcept
        {
            return _index;
        }
    };

    class bptree_const_iterator final
    {
        const bptree_node* _node;
        size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ptrdiff_t;
        using self = bptree_const_iterator;

        friend class BP_tree;
        friend class bptree_iterator;

        explicit bptree_const_iterator(const bptree_node* node = nullptr, size_t index = 0)
            : _node(node), _index(index)
        {
        }

        bptree_const_iterator(const bptree_iterator& it) noexcept
            : _node(it._node), _index(it._index)
        {
        }

        reference operator*() const noexcept
        {
            return pair_as_value(*_node->_entries[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value(*_node->_entries[_index]);
        }

        self& operator++()
        {
            if (_node == nullptr)
            {
                return *this;
            }

            ++_index;
            if (_index >= _node->_entries.size())
            {
                _node = _node->_next;
                _index = 0;
            }

            return *this;
        }

        self operator++(int)
        {
            self copy(*this);
            ++(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept
        {
            return _node == other._node && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        size_t current_node_keys_count() const noexcept
        {
            return _node == nullptr ? 0 : _node->_entries.size();
        }

        size_t index() const noexcept
        {
            return _index;
        }
    };

    explicit BP_tree(const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : compare(cmp), _allocator(alloc), _root(nullptr), _head(nullptr), _size(0)
    {
    }

    explicit BP_tree(pp_allocator<value_type> alloc, const compare& comp = compare())
        : BP_tree(comp, alloc)
    {
    }

    template<input_iterator_for_pair<tkey, tvalue> iterator>
    explicit BP_tree(iterator begin_it, iterator end_it, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BP_tree(cmp, alloc)
    {
        for (; begin_it != end_it; ++begin_it)
        {
            emplace(begin_it->first, begin_it->second);
        }
    }

    BP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<value_type> alloc = pp_allocator<value_type>())
        : BP_tree(cmp, alloc)
    {
        for (const auto& item : data)
        {
            emplace(item.first, item.second);
        }
    }

    BP_tree(const BP_tree& other)
        : BP_tree(static_cast<const compare&>(other), other._allocator.select_on_container_copy_construction())
    {
        for (auto it = other.cbegin(); it != other.cend(); ++it)
        {
            emplace(it->first, it->second);
        }
    }

    BP_tree(BP_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(other._allocator),
          _root(other._root),
          _head(other._head),
          _size(other._size)
    {
        other._root = nullptr;
        other._head = nullptr;
        other._size = 0;
    }

    BP_tree& operator=(const BP_tree& other)
    {
        if (this == &other)
        {
            return *this;
        }

        BP_tree copy(other);
        *this = std::move(copy);
        return *this;
    }

    BP_tree& operator=(BP_tree&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        clear();
        static_cast<compare&>(*this) = std::move(static_cast<compare&>(other));
        _allocator = other._allocator;
        _root = other._root;
        _head = other._head;
        _size = other._size;
        other._root = nullptr;
        other._head = nullptr;
        other._size = 0;
        return *this;
    }

    ~BP_tree() noexcept
    {
        clear();
    }

    tvalue& at(const tkey& key)
    {
        auto it = find(key);
        if (it == end())
        {
            throw std::out_of_range("key not found");
        }
        return it->second;
    }

    const tvalue& at(const tkey& key) const
    {
        auto it = find(key);
        if (it == cend())
        {
            throw std::out_of_range("key not found");
        }
        return it->second;
    }

    tvalue& operator[](const tkey& key)
    {
        return emplace(key, tvalue()).first->second;
    }

    tvalue& operator[](tkey&& key)
    {
        return emplace(std::move(key), tvalue()).first->second;
    }

    bptree_iterator begin()
    {
        return _head == nullptr ? end() : bptree_iterator(_head, 0);
    }

    bptree_iterator end()
    {
        return bptree_iterator(nullptr, 0);
    }

    bptree_const_iterator begin() const
    {
        return cbegin();
    }

    bptree_const_iterator end() const
    {
        return cend();
    }

    bptree_const_iterator cbegin() const
    {
        return _head == nullptr ? cend() : bptree_const_iterator(_head, 0);
    }

    bptree_const_iterator cend() const
    {
        return bptree_const_iterator(nullptr, 0);
    }

    size_t size() const noexcept
    {
        return _size;
    }

    bool empty() const noexcept
    {
        return _size == 0;
    }

    bptree_iterator find(const tkey& key)
    {
        bptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return end();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size() && key_equal(leaf->_entries[index]->first, key))
        {
            return bptree_iterator(leaf, index);
        }

        return end();
    }

    bptree_const_iterator find(const tkey& key) const
    {
        bptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return cend();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size() && key_equal(leaf->_entries[index]->first, key))
        {
            return bptree_const_iterator(leaf, index);
        }

        return cend();
    }

    bptree_iterator lower_bound(const tkey& key)
    {
        bptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return end();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size())
        {
            return bptree_iterator(leaf, index);
        }

        return leaf->_next == nullptr ? end() : bptree_iterator(leaf->_next, 0);
    }

    bptree_const_iterator lower_bound(const tkey& key) const
    {
        bptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return cend();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index < leaf->_entries.size())
        {
            return bptree_const_iterator(leaf, index);
        }

        return leaf->_next == nullptr ? cend() : bptree_const_iterator(leaf->_next, 0);
    }

    bptree_iterator upper_bound(const tkey& key)
    {
        auto it = lower_bound(key);
        if (it != end() && key_equal(it->first, key))
        {
            ++it;
        }
        return it;
    }

    bptree_const_iterator upper_bound(const tkey& key) const
    {
        auto it = lower_bound(key);
        if (it != cend() && key_equal(it->first, key))
        {
            ++it;
        }
        return it;
    }

    bool contains(const tkey& key) const
    {
        return find(key) != cend();
    }

    void clear() noexcept
    {
        free_subtree(_root);
        _root = nullptr;
        _head = nullptr;
        _size = 0;
    }

    std::pair<bptree_iterator, bool> insert(const tree_data_type& data)
    {
        return emplace(data.first, data.second);
    }

    std::pair<bptree_iterator, bool> insert(tree_data_type&& data)
    {
        return emplace(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    std::pair<bptree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        if (auto it = find(data.first); it != end())
        {
            return {it, false};
        }

        tree_data_type* entry = allocate_entry(std::move(data));
        try
        {
            insert_new_entry(entry);
        }
        catch (...)
        {
            free_entry(entry);
            throw;
        }

        return {find(entry->first), true};
    }

    bptree_iterator insert_or_assign(const tree_data_type& data)
    {
        return emplace_or_assign(data.first, data.second);
    }

    bptree_iterator insert_or_assign(tree_data_type&& data)
    {
        return emplace_or_assign(std::move(data.first), std::move(data.second));
    }

    template <typename ...Args>
    bptree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        if (auto it = find(data.first); it != end())
        {
            it->second = std::move(data.second);
            return it;
        }

        return emplace(std::move(data)).first;
    }

    bptree_iterator erase(bptree_iterator pos)
    {
        if (pos == end())
        {
            return end();
        }

        tkey key = pos->first;
        bool has_next = false;
        tkey next_key{};
        auto next = pos;
        ++next;
        if (next != end())
        {
            has_next = true;
            next_key = next->first;
        }

        erase(key);
        return has_next ? find(next_key) : end();
    }

    bptree_iterator erase(bptree_const_iterator pos)
    {
        if (pos == cend())
        {
            return end();
        }

        tkey key = pos->first;
        bool has_next = false;
        tkey next_key{};
        auto next = pos;
        ++next;
        if (next != cend())
        {
            has_next = true;
            next_key = next->first;
        }

        erase(key);
        return has_next ? find(next_key) : end();
    }

    bptree_iterator erase(bptree_iterator beg, bptree_iterator en)
    {
        while (beg != en)
        {
            beg = erase(beg);
        }
        return en;
    }

    bptree_iterator erase(bptree_const_iterator beg, bptree_const_iterator en)
    {
        while (beg != en)
        {
            beg = bptree_const_iterator(erase(beg));
        }
        if (en == cend())
        {
            return end();
        }
        return find(en->first);
    }

    bptree_iterator erase(const tkey& key)
    {
        bptree_node* leaf = find_leaf(key);
        if (leaf == nullptr)
        {
            return end();
        }

        size_t index = lower_bound_index_in_leaf(leaf, key);
        if (index >= leaf->_entries.size() || !key_equal(leaf->_entries[index]->first, key))
        {
            return end();
        }

        bool has_next = false;
        tkey next_key{};
        if (index + 1 < leaf->_entries.size())
        {
            has_next = true;
            next_key = leaf->_entries[index + 1]->first;
        }
        else if (leaf->_next != nullptr)
        {
            has_next = true;
            next_key = leaf->_next->_entries.front()->first;
        }

        tree_data_type* removed = leaf->_entries[index];
        leaf->_entries.erase(leaf->_entries.begin() + static_cast<ptrdiff_t>(index));
        free_entry(removed);
        --_size;

        rebalance_leaf_after_erase(leaf);
        return has_next ? find(next_key) : end();
    }
};

template<std::input_iterator iterator, comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
    std::size_t t = 5, typename U>
BP_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BP_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
BP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BP_tree<tkey, tvalue, compare, t>;

#endif
