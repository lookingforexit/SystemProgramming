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
    static_assert(t >= 1, "invalid t for b-star tree");

public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

private:
    static constexpr size_t minimum_keys_in_node = 2 * t - 1;
    static constexpr size_t maximum_keys_in_node = 3 * t - 1;

    struct bstree_node
    {
        std::vector<tree_data_type> _keys;
        std::vector<bstree_node*> _pointers;

        bstree_node()
        {
            _keys.reserve(maximum_keys_in_node + 1);
            _pointers.reserve(maximum_keys_in_node + 2);
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
    size_t _size;

    bool key_less(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }

    bool key_equal(const tkey& lhs, const tkey& rhs) const
    {
        return !key_less(lhs, rhs) && !key_less(rhs, lhs);
    }

    size_t find_key_index(const bstree_node* node, const tkey& key) const
    {
        size_t index = 0;
        while (index < node->_keys.size() && key_less(node->_keys[index].first, key))
        {
            ++index;
        }

        return index;
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

    void free_node(bstree_node* node) noexcept
    {
        if (node != nullptr)
        {
            pp_allocator<bstree_node>(_allocator).delete_object(node);
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

    bstree_node* clone_subtree(const bstree_node* node)
    {
        if (node == nullptr)
        {
            return nullptr;
        }

        bstree_node* clone = allocate_node();
        try
        {
            clone->_keys = node->_keys;
            for (bstree_node* child : node->_pointers)
            {
                clone->_pointers.push_back(clone_subtree(child));
            }
        }
        catch (...)
        {
            free_subtree(clone);
            throw;
        }

        return clone;
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

    void split_root_overflowed_node()
    {
        bstree_node* old_root = _root;
        bstree_node* right = allocate_node();
        const size_t middle_index = old_root->_keys.size() / 2;
        tree_data_type promoted = std::move(old_root->_keys[middle_index]);

        for (size_t i = middle_index + 1; i < old_root->_keys.size(); ++i)
        {
            right->_keys.push_back(std::move(old_root->_keys[i]));
        }

        if (!old_root->is_leaf())
        {
            for (size_t i = middle_index + 1; i < old_root->_pointers.size(); ++i)
            {
                right->_pointers.push_back(old_root->_pointers[i]);
            }
            old_root->_pointers.resize(middle_index + 1);
        }

        old_root->_keys.resize(middle_index);

        bstree_node* new_root = allocate_node();
        new_root->_keys.push_back(std::move(promoted));
        new_root->_pointers.push_back(old_root);
        new_root->_pointers.push_back(right);
        _root = new_root;
    }

    void redistribute_children_pair(bstree_node* parent, size_t left_child_index)
    {
        bstree_node* left = parent->_pointers[left_child_index];
        bstree_node* right = parent->_pointers[left_child_index + 1];

        std::vector<tree_data_type> all_keys;
        all_keys.reserve(left->_keys.size() + 1 + right->_keys.size());
        std::move(left->_keys.begin(), left->_keys.end(), std::back_inserter(all_keys));
        all_keys.push_back(std::move(parent->_keys[left_child_index]));
        std::move(right->_keys.begin(), right->_keys.end(), std::back_inserter(all_keys));

        std::vector<bstree_node*> all_pointers;
        if (!left->is_leaf())
        {
            all_pointers.reserve(left->_pointers.size() + right->_pointers.size());
            all_pointers.insert(all_pointers.end(), left->_pointers.begin(), left->_pointers.end());
            all_pointers.insert(all_pointers.end(), right->_pointers.begin(), right->_pointers.end());
        }

        const size_t left_keys_count = all_keys.size() / 2;

        left->_keys.assign(
                std::make_move_iterator(all_keys.begin()),
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(left_keys_count)));
        parent->_keys[left_child_index] = std::move(all_keys[left_keys_count]);
        right->_keys.assign(
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(left_keys_count + 1)),
                std::make_move_iterator(all_keys.end()));

        if (!all_pointers.empty())
        {
            left->_pointers.assign(
                    all_pointers.begin(),
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1));
            right->_pointers.assign(
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1),
                    all_pointers.end());
        }
    }

    void split_children_into_three(bstree_node* parent, size_t left_child_index)
    {
        bstree_node* left = parent->_pointers[left_child_index];
        bstree_node* right = parent->_pointers[left_child_index + 1];
        bstree_node* middle = allocate_node();

        std::vector<tree_data_type> all_keys;
        all_keys.reserve(left->_keys.size() + 1 + right->_keys.size());
        std::move(left->_keys.begin(), left->_keys.end(), std::back_inserter(all_keys));
        all_keys.push_back(std::move(parent->_keys[left_child_index]));
        std::move(right->_keys.begin(), right->_keys.end(), std::back_inserter(all_keys));

        std::vector<bstree_node*> all_pointers;
        if (!left->is_leaf())
        {
            all_pointers.reserve(left->_pointers.size() + right->_pointers.size());
            all_pointers.insert(all_pointers.end(), left->_pointers.begin(), left->_pointers.end());
            all_pointers.insert(all_pointers.end(), right->_pointers.begin(), right->_pointers.end());
        }

        const size_t left_keys_count = minimum_keys_in_node;
        const size_t middle_keys_count = minimum_keys_in_node;
        const size_t second_separator_index = left_keys_count + 1 + middle_keys_count;

        left->_keys.assign(
                std::make_move_iterator(all_keys.begin()),
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(left_keys_count)));
        tree_data_type first_separator = std::move(all_keys[left_keys_count]);
        middle->_keys.assign(
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(left_keys_count + 1)),
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(second_separator_index)));
        tree_data_type second_separator = std::move(all_keys[second_separator_index]);
        right->_keys.assign(
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(second_separator_index + 1)),
                std::make_move_iterator(all_keys.end()));

        if (!all_pointers.empty())
        {
            left->_pointers.assign(
                    all_pointers.begin(),
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1));
            middle->_pointers.assign(
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1),
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1 + middle_keys_count + 1));
            right->_pointers.assign(
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1 + middle_keys_count + 1),
                    all_pointers.end());
        }

        parent->_keys[left_child_index] = std::move(first_separator);
        parent->_keys.insert(
                parent->_keys.begin() + static_cast<ptrdiff_t>(left_child_index + 1),
                std::move(second_separator));
        parent->_pointers.insert(
                parent->_pointers.begin() + static_cast<ptrdiff_t>(left_child_index + 1),
                middle);
    }

    void handle_child_overflow_after_insert(bstree_node* parent, size_t child_index)
    {
        bstree_node* child = parent->_pointers[child_index];
        if (child->_keys.size() <= maximum_keys_in_node)
        {
            return;
        }

        if (child_index + 1 < parent->_pointers.size() &&
            parent->_pointers[child_index + 1]->_keys.size() < maximum_keys_in_node)
        {
            redistribute_children_pair(parent, child_index);
            return;
        }

        if (child_index > 0 &&
            parent->_pointers[child_index - 1]->_keys.size() < maximum_keys_in_node)
        {
            redistribute_children_pair(parent, child_index - 1);
            return;
        }

        if (child_index + 1 < parent->_pointers.size())
        {
            split_children_into_three(parent, child_index);
            return;
        }

        split_children_into_three(parent, child_index - 1);
    }

    bool insert_with_local_rebalance(bstree_node* node, tree_data_type&& data)
    {
        const size_t index = find_key_index(node, data.first);

        if (node->is_leaf())
        {
            node->_keys.insert(node->_keys.begin() + static_cast<ptrdiff_t>(index), std::move(data));
            return node->_keys.size() > maximum_keys_in_node;
        }

        if (insert_with_local_rebalance(node->_pointers[index], std::move(data)))
        {
            handle_child_overflow_after_insert(node, index);
        }

        return node->_keys.size() > maximum_keys_in_node;
    }

    tree_data_type largest_pair_in_subtree(const bstree_node* node) const
    {
        const bstree_node* current = node;
        while (!current->is_leaf())
        {
            current = current->_pointers.back();
        }

        return current->_keys.back();
    }

    tree_data_type smallest_pair_in_subtree(const bstree_node* node) const
    {
        const bstree_node* current = node;
        while (!current->is_leaf())
        {
            current = current->_pointers.front();
        }

        return current->_keys.front();
    }

    void merge_child_nodes(bstree_node* parent, size_t left_child_index)
    {
        bstree_node* left = parent->_pointers[left_child_index];
        bstree_node* right = parent->_pointers[left_child_index + 1];

        left->_keys.push_back(std::move(parent->_keys[left_child_index]));
        for (tree_data_type& item : right->_keys)
        {
            left->_keys.push_back(std::move(item));
        }

        for (bstree_node* child : right->_pointers)
        {
            left->_pointers.push_back(child);
        }

        parent->_keys.erase(parent->_keys.begin() + static_cast<ptrdiff_t>(left_child_index));
        parent->_pointers.erase(parent->_pointers.begin() + static_cast<ptrdiff_t>(left_child_index + 1));
        free_node(right);
    }

    void merge_three_children_into_two(bstree_node* parent, size_t left_child_index)
    {
        bstree_node* left = parent->_pointers[left_child_index];
        bstree_node* middle = parent->_pointers[left_child_index + 1];
        bstree_node* right = parent->_pointers[left_child_index + 2];
        std::vector<tree_data_type> all_keys;
        all_keys.reserve(left->_keys.size() + middle->_keys.size() + right->_keys.size() + 2);
        std::move(left->_keys.begin(), left->_keys.end(), std::back_inserter(all_keys));
        all_keys.push_back(std::move(parent->_keys[left_child_index]));
        std::move(middle->_keys.begin(), middle->_keys.end(), std::back_inserter(all_keys));
        all_keys.push_back(std::move(parent->_keys[left_child_index + 1]));
        std::move(right->_keys.begin(), right->_keys.end(), std::back_inserter(all_keys));

        std::vector<bstree_node*> all_pointers;
        if (!left->is_leaf())
        {
            all_pointers.reserve(left->_pointers.size() + middle->_pointers.size() + right->_pointers.size());
            all_pointers.insert(all_pointers.end(), left->_pointers.begin(), left->_pointers.end());
            all_pointers.insert(all_pointers.end(), middle->_pointers.begin(), middle->_pointers.end());
            all_pointers.insert(all_pointers.end(), right->_pointers.begin(), right->_pointers.end());
        }

        const size_t left_keys_count = all_keys.size() / 2;

        left->_keys.assign(
                std::make_move_iterator(all_keys.begin()),
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(left_keys_count)));
        parent->_keys[left_child_index] = std::move(all_keys[left_keys_count]);
        right->_keys.assign(
                std::make_move_iterator(all_keys.begin() + static_cast<ptrdiff_t>(left_keys_count + 1)),
                std::make_move_iterator(all_keys.end()));

        if (!all_pointers.empty())
        {
            left->_pointers.assign(
                    all_pointers.begin(),
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1));
            right->_pointers.assign(
                    all_pointers.begin() + static_cast<ptrdiff_t>(left_keys_count + 1),
                    all_pointers.end());
        }

        parent->_keys.erase(parent->_keys.begin() + static_cast<ptrdiff_t>(left_child_index + 1));
        parent->_pointers.erase(parent->_pointers.begin() + static_cast<ptrdiff_t>(left_child_index + 1));
        free_node(middle);
    }

    void prepare_child_for_erase(bstree_node* parent, size_t child_index)
    {
        if (child_index > 0 && parent->_pointers[child_index - 1]->_keys.size() > minimum_keys_in_node)
        {
            redistribute_children_pair(parent, child_index - 1);
            return;
        }

        if (child_index + 1 < parent->_pointers.size() && parent->_pointers[child_index + 1]->_keys.size() > minimum_keys_in_node)
        {
            redistribute_children_pair(parent, child_index);
            return;
        }

        if (child_index + 2 < parent->_pointers.size())
        {
            merge_three_children_into_two(parent, child_index);
            return;
        }

        if (child_index >= 2)
        {
            merge_three_children_into_two(parent, child_index - 2);
            return;
        }

        if (child_index + 1 < parent->_pointers.size())
        {
            merge_child_nodes(parent, child_index);
        }
        else
        {
            merge_child_nodes(parent, child_index - 1);
        }
    }

    void prepare_children_around_key_for_erase(bstree_node* parent, size_t key_index)
    {
        if (key_index + 2 < parent->_pointers.size())
        {
            merge_three_children_into_two(parent, key_index);
            return;
        }

        if (key_index >= 1)
        {
            merge_three_children_into_two(parent, key_index - 1);
            return;
        }

        merge_child_nodes(parent, key_index);
    }

    bool erase_key_from_node(bstree_node* node, const tkey& key)
    {
        size_t index = find_key_index(node, key);

        if (index < node->_keys.size() && key_equal(node->_keys[index].first, key))
        {
            if (node->is_leaf())
            {
                node->_keys.erase(node->_keys.begin() + static_cast<ptrdiff_t>(index));
                return true;
            }

            if (node->_pointers[index]->_keys.size() > minimum_keys_in_node)
            {
                node->_keys[index] = largest_pair_in_subtree(node->_pointers[index]);
                return erase_key_from_node(node->_pointers[index], node->_keys[index].first);
            }

            if (node->_pointers[index + 1]->_keys.size() > minimum_keys_in_node)
            {
                node->_keys[index] = smallest_pair_in_subtree(node->_pointers[index + 1]);
                return erase_key_from_node(node->_pointers[index + 1], node->_keys[index].first);
            }

            prepare_children_around_key_for_erase(node, index);
            return erase_key_from_node(node, key);
        }

        if (node->is_leaf())
        {
            return false;
        }

        if (node->_pointers[index]->_keys.size() == minimum_keys_in_node)
        {
            prepare_child_for_erase(node, index);
            index = find_key_index(node, key);
        }

        return erase_key_from_node(node->_pointers[index], key);
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
            return pair_as_value((*_path.top().first)->_keys[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value((*_path.top().first)->_keys[_index]);
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
            return pair_as_value((*_path.top().first)->_keys[_index]);
        }

        pointer operator->() const noexcept
        {
            return &pair_as_value((*_path.top().first)->_keys[_index]);
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
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
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
        : compare(static_cast<const compare&>(other)),
          _allocator(other._allocator.select_on_container_copy_construction()),
          _root(nullptr),
          _size(other._size)
    {
        _root = clone_subtree(other._root);
    }

    BS_tree(BS_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(other._allocator),
          _root(other._root),
          _size(other._size)
    {
        other._root = nullptr;
        other._size = 0;
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
        _allocator = other._allocator;
        _root = other._root;
        _size = other._size;
        other._root = nullptr;
        other._size = 0;
        return *this;
    }

    ~BS_tree() noexcept
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

    bstree_iterator begin()
    {
        if (_root == nullptr)
        {
            return end();
        }
        return bstree_iterator(build_begin_path(), 0);
    }

    bstree_iterator end()
    {
        if (_root == nullptr)
        {
            return bstree_iterator();
        }

        auto path = build_end_path();
        return bstree_iterator(path, (*path.top().first)->_keys.size());
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
        if (_root == nullptr)
        {
            return cend();
        }
        return bstree_const_iterator(build_begin_path(), 0);
    }

    bstree_const_iterator cend() const
    {
        if (_root == nullptr)
        {
            return bstree_const_iterator();
        }

        auto path = build_end_path();
        return bstree_const_iterator(path, (*path.top().first)->_keys.size());
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
        return rbegin();
    }

    bstree_const_reverse_iterator crend() const
    {
        return rend();
    }

    size_t size() const noexcept
    {
        return _size;
    }

    bool empty() const noexcept
    {
        return _size == 0;
    }

    bstree_iterator find(const tkey& key)
    {
        if (_root == nullptr)
        {
            return end();
        }

        iterator_path path;
        bstree_node** slot = &_root;
        path.push({slot, 0});

        while (*slot != nullptr)
        {
            bstree_node* node = *slot;
            size_t index = find_key_index(node, key);
            if (index < node->_keys.size() && key_equal(node->_keys[index].first, key))
            {
                return bstree_iterator(path, index);
            }

            if (node->is_leaf())
            {
                return end();
            }

            slot = &node->_pointers[index];
            path.push({slot, index});
        }

        return end();
    }

    bstree_const_iterator find(const tkey& key) const
    {
        if (_root == nullptr)
        {
            return cend();
        }

        const_iterator_path path;
        auto slot = reinterpret_cast<bstree_node* const*>(&_root);
        path.push({slot, 0});

        while (*slot != nullptr)
        {
            bstree_node* node = *slot;
            size_t index = find_key_index(node, key);
            if (index < node->_keys.size() && key_equal(node->_keys[index].first, key))
            {
                return bstree_const_iterator(path, index);
            }

            if (node->is_leaf())
            {
                return cend();
            }

            slot = &node->_pointers[index];
            path.push({slot, index});
        }

        return cend();
    }

    bstree_iterator lower_bound(const tkey& key)
    {
        for (auto it = begin(), last = end(); it != last; ++it)
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
        for (auto it = cbegin(), last = cend(); it != last; ++it)
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
        for (auto it = begin(), last = end(); it != last; ++it)
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
        for (auto it = cbegin(), last = cend(); it != last; ++it)
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
        return find(key) != cend();
    }

    void clear() noexcept
    {
        free_subtree(_root);
        _root = nullptr;
        _size = 0;
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
        tkey inserted_key = data.first;

        if (auto it = find(inserted_key); it != end())
        {
            return {it, false};
        }

        if (_root == nullptr)
        {
            _root = allocate_node();
            _root->_keys.push_back(std::move(data));
            ++_size;
            return {begin(), true};
        }

        if (insert_with_local_rebalance(_root, std::move(data)))
        {
            split_root_overflowed_node();
        }

        ++_size;
        return {find(inserted_key), true};
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
        if (auto it = find(data.first); it != end())
        {
            it->second = std::move(data.second);
            return it;
        }
        return emplace(std::move(data)).first;
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
        auto current = find(key);
        if (_root == nullptr || current == end())
        {
            return end();
        }

        auto next = current;
        ++next;

        if (!erase_key_from_node(_root, key))
        {
            return end();
        }

        --_size;
        if (_root->_keys.empty())
        {
            bstree_node* old_root = _root;
            if (_root->is_leaf())
            {
                _root = nullptr;
            }
            else
            {
                _root = _root->_pointers.front();
                old_root->_pointers.clear();
            }
            free_node(old_root);
        }

        if (next == end())
        {
            return end();
        }
        return find(next->first);
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
