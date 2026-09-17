#include "gatekeeper/storage/entry.h"
#include "gatekeeper/storage/hash_table.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

struct ConstantHash
{
    std::size_t operator()(const std::string&) const
    {
        return 42;
    }
};

void TestBasicOperations()
{
    gatekeeper::storage::HashTable<std::string, int> table(8);
    Require(table.Empty(), "table should be empty");
    Require(table.Size() == 0, "size should be 0");

    auto [val1, ins1] = table.Insert("apple", 10);
    Require(ins1, "insert apple failed");
    Require(*val1 == 10, "wrong value for apple");

    auto [val2, ins2] = table.Insert("banana", 20);
    Require(ins2, "insert banana failed");
    Require(table.Size() == 2, "size should be 2");

    auto* found = table.Find("apple");
    Require(found != nullptr && *found == 10, "find apple failed");

    auto* missing = table.Find("cherry");
    Require(missing == nullptr, "found non-existent key");

    auto [val_upd, ins_upd] = table.Insert("apple", 15);
    Require(!ins_upd, "update should return false for inserted");
    Require(*val_upd == 15, "value not updated");
    Require(table.Size() == 2, "size changed on update");

    Require(table.Erase("apple"), "erase apple failed");
    Require(!table.Erase("apple"), "erase missing key returned true");
    Require(table.Find("apple") == nullptr, "erased key still found");
    Require(table.Size() == 1, "wrong size after erase");
}

void TestCollisions()
{
    gatekeeper::storage::HashTable<std::string, std::string, ConstantHash> table(16);
    table.Insert("k1", "v1");
    table.Insert("k2", "v2");
    table.Insert("k3", "v3");

    Require(table.Size() == 3, "collision size wrong");
    Require(*table.Find("k1") == "v1", "collision find k1 wrong");
    Require(*table.Find("k2") == "v2", "collision find k2 wrong");
    Require(*table.Find("k3") == "v3", "collision find k3 wrong");

    Require(table.Erase("k2"), "erase middle node in chain failed");
    Require(table.Find("k2") == nullptr, "k2 should be erased");
    Require(*table.Find("k1") == "v1", "k1 corrupted");
    Require(*table.Find("k3") == "v3", "k3 corrupted");
}

void TestResizeAndLoadFactor()
{
    gatekeeper::storage::HashTable<int, int> table(8, 0.75f);
    Require(table.Capacity() == 8, "initial capacity wrong");

    for (int i = 0; i < 20; ++i)
    {
        table.Insert(i, i * 10);
    }

    Require(table.Size() == 20, "wrong size after bulk insert");
    Require(table.Capacity() >= 32, "table did not resize");
    Require(table.LoadFactor() <= table.MaxLoadFactor(), "load factor exceeded max");

    for (int i = 0; i < 20; ++i)
    {
        auto* v = table.Find(i);
        Require(v != nullptr && *v == i * 10, "data corrupted after resize");
    }
}

void TestCopyAndMove()
{
    gatekeeper::storage::HashTable<std::string, int> original;
    original.Insert("one", 1);
    original.Insert("two", 2);

    gatekeeper::storage::HashTable<std::string, int> copy = original;
    Require(copy.Size() == 2, "copy size mismatch");
    Require(*copy.Find("one") == 1, "copy data mismatch");

    gatekeeper::storage::HashTable<std::string, int> moved = std::move(original);
    Require(moved.Size() == 2, "moved size mismatch");
    Require(*moved.Find("two") == 2, "moved data mismatch");
}

void TestEntryMetadata()
{
    gatekeeper::storage::EntryMetadata meta{gatekeeper::storage::DataType::String, 1000, 2000};
    Require(!meta.IsExpired(1500), "should not be expired at 1500");
    Require(meta.IsExpired(2000), "should be expired at 2000");
    Require(meta.IsExpired(2500), "should be expired at 2500");

    gatekeeper::storage::EntryMetadata persistent{gatekeeper::storage::DataType::String, 1000, 0};
    Require(!persistent.IsExpired(999999), "persistent key should never expire");
}

}

int main()
{
    try
    {
        TestBasicOperations();
        TestCollisions();
        TestResizeAndLoadFactor();
        TestCopyAndMove();
        TestEntryMetadata();
        std::cout << "HashTable tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
