#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace gatekeeper::storage
{

template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class HashTable
{
public:
    struct Node
    {
        Key key;
        Value value;
        Node* next{nullptr};
    };

    explicit HashTable(std::size_t initial_capacity = 16, float max_load_factor = 0.75f)
        : buckets_(initial_capacity < 8 ? 8 : initial_capacity, nullptr),
          size_(0),
          max_load_factor_(max_load_factor > 0.1f ? max_load_factor : 0.75f)
    {
    }

    ~HashTable()
    {
        Clear();
    }

    HashTable(const HashTable& other)
        : buckets_(other.buckets_.size(), nullptr),
          size_(0),
          max_load_factor_(other.max_load_factor_)
    {
        for (const auto* bucket : other.buckets_)
        {
            for (const auto* curr = bucket; curr != nullptr; curr = curr->next)
            {
                Insert(curr->key, curr->value);
            }
        }
    }

    HashTable& operator=(const HashTable& other)
    {
        if (this != &other)
        {
            HashTable temp(other);
            Swap(temp);
        }
        return *this;
    }

    HashTable(HashTable&& other) noexcept
        : buckets_(std::move(other.buckets_)),
          size_(other.size_),
          max_load_factor_(other.max_load_factor_)
    {
        other.size_ = 0;
    }

    HashTable& operator=(HashTable&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            buckets_ = std::move(other.buckets_);
            size_ = other.size_;
            max_load_factor_ = other.max_load_factor_;
            other.size_ = 0;
        }
        return *this;
    }

    void Swap(HashTable& other) noexcept
    {
        buckets_.swap(other.buckets_);
        std::swap(size_, other.size_);
        std::swap(max_load_factor_, other.max_load_factor_);
    }

    std::pair<Value*, bool> Insert(Key key, Value value)
    {
        if (static_cast<float>(size_ + 1) > static_cast<float>(buckets_.size()) * max_load_factor_)
        {
            Resize(buckets_.size() * 2);
        }

        const auto bucket_index = HashKey(key);
        Node* curr = buckets_[bucket_index];
        while (curr != nullptr)
        {
            if (KeyEqual{}(curr->key, key))
            {
                curr->value = std::move(value);
                return {&curr->value, false};
            }
            curr = curr->next;
        }

        auto* new_node = new Node{std::move(key), std::move(value), buckets_[bucket_index]};
        buckets_[bucket_index] = new_node;
        ++size_;
        return {&new_node->value, true};
    }

    Value* Find(const Key& key)
    {
        if (buckets_.empty())
        {
            return nullptr;
        }
        const auto bucket_index = HashKey(key);
        Node* curr = buckets_[bucket_index];
        while (curr != nullptr)
        {
            if (KeyEqual{}(curr->key, key))
            {
                return &curr->value;
            }
            curr = curr->next;
        }
        return nullptr;
    }

    const Value* Find(const Key& key) const
    {
        if (buckets_.empty())
        {
            return nullptr;
        }
        const auto bucket_index = HashKey(key);
        const Node* curr = buckets_[bucket_index];
        while (curr != nullptr)
        {
            if (KeyEqual{}(curr->key, key))
            {
                return &curr->value;
            }
            curr = curr->next;
        }
        return nullptr;
    }

    bool Erase(const Key& key)
    {
        if (buckets_.empty())
        {
            return false;
        }
        const auto bucket_index = HashKey(key);
        Node* curr = buckets_[bucket_index];
        Node* prev = nullptr;

        while (curr != nullptr)
        {
            if (KeyEqual{}(curr->key, key))
            {
                if (prev != nullptr)
                {
                    prev->next = curr->next;
                }
                else
                {
                    buckets_[bucket_index] = curr->next;
                }
                delete curr;
                --size_;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    void Clear()
    {
        for (auto*& bucket : buckets_)
        {
            Node* curr = bucket;
            while (curr != nullptr)
            {
                Node* next = curr->next;
                delete curr;
                curr = next;
            }
            bucket = nullptr;
        }
        size_ = 0;
    }

    void Resize(std::size_t new_capacity)
    {
        if (new_capacity <= size_)
        {
            return;
        }
        std::vector<Node*> new_buckets(new_capacity, nullptr);
        for (auto* bucket : buckets_)
        {
            Node* curr = bucket;
            while (curr != nullptr)
            {
                Node* next = curr->next;
                const auto new_index = Hash{}(curr->key) % new_capacity;
                curr->next = new_buckets[new_index];
                new_buckets[new_index] = curr;
                curr = next;
            }
        }
        buckets_ = std::move(new_buckets);
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] std::size_t Capacity() const noexcept
    {
        return buckets_.size();
    }

    [[nodiscard]] bool Empty() const noexcept
    {
        return size_ == 0;
    }

    [[nodiscard]] float LoadFactor() const noexcept
    {
        return buckets_.empty() ? 0.0f : static_cast<float>(size_) / static_cast<float>(buckets_.size());
    }

    [[nodiscard]] float MaxLoadFactor() const noexcept
    {
        return max_load_factor_;
    }

    template <typename Fn>
    void ForEach(Fn&& fn)
    {
        for (auto* bucket : buckets_)
        {
            for (auto* curr = bucket; curr != nullptr; curr = curr->next)
            {
                fn(curr->key, curr->value);
            }
        }
    }

    template <typename Fn>
    void ForEach(Fn&& fn) const
    {
        for (const auto* bucket : buckets_)
        {
            for (const auto* curr = bucket; curr != nullptr; curr = curr->next)
            {
                fn(curr->key, curr->value);
            }
        }
    }

private:
    std::size_t HashKey(const Key& key) const
    {
        return Hash{}(key) % buckets_.size();
    }

    std::vector<Node*> buckets_;
    std::size_t size_{0};
    float max_load_factor_{0.75f};
};

}
