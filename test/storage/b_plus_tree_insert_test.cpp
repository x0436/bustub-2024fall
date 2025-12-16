//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// b_plus_tree_insert_test.cpp
//
// Identification: test/storage/b_plus_tree_insert_test.cpp
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdio>

#include "buffer/buffer_pool_manager.h"
#include "gtest/gtest.h"
#include "storage/disk/disk_manager_memory.h"
#include "storage/index/b_plus_tree.h"
#include "test_util.h"  // NOLINT

namespace bustub {

using bustub::DiskManagerUnlimitedMemory;

TEST(BPlusTreeTests, MyTest) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 3, 3);
  GenericKey<8> index_key;
  RID rid;

  std::vector<int64_t> keys = {1, 2, 3};
  for (auto key : keys) {
    std::cerr << "Insert: " << key << "\n";
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);  // 右移32位就是0, 即 page_id = 0
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
  }

  bool is_present;
  std::vector<RID> rids;
  std::cerr << "----------------"
            << "\n";

  for (auto key : keys) {
    std::cerr << "Search: " << key << "\n";
    rids.clear();
    index_key.SetFromInteger(key);
    is_present = tree.GetValue(index_key, &rids);

    EXPECT_EQ(is_present, true);
    EXPECT_EQ(rids.size(), 1);  // 这什么东西
    EXPECT_EQ(rids[0].GetPageId(), 0);
    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);
  }
  delete bpm;
}

TEST(BPlusTreeTests, DuplicateTest) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 3, 3);
  GenericKey<8> index_key;
  RID rid;

  std::vector<int64_t> keys = {1, 2, 2};
  for (auto key : keys) {
    std::cerr << "Insert: " << key << "\n";
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);  // 右移32位就是0, 即 page_id = 0
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
  }

  bool is_present;
  std::vector<RID> rids;
  std::cerr << "----------------"
            << "\n";

  for (auto key : keys) {
    std::cerr << "Search: " << key << "\n";
    rids.clear();
    index_key.SetFromInteger(key);
    is_present = tree.GetValue(index_key, &rids);

    EXPECT_EQ(is_present, true);
    EXPECT_EQ(rids.size(), 1);  // 这什么东西
    EXPECT_EQ(rids[0].GetPageId(), 0);
    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);
  }
  delete bpm;
}

TEST(BPlusTreeTests, MyTest100) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  // 增加 page 数量，避免频繁分配失败
  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(200, disk_manager.get());  // 200 个 page

  // allocate header_page
  page_id_t page_id = bpm->NewPage();

  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 10,
                                                           10);  // leaf 和 internal page max size 设为10

  GenericKey<8> index_key;
  RID rid;

  // 插入 100 个 key
  std::vector<int64_t> keys;
  for (int64_t i = 1; i <= 100; i++) {
    keys.push_back(i);
  }

  for (auto key : keys) {
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);  // page_id = 0
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
  }

  // 检查每个 key 是否存在
  bool is_present;
  std::vector<RID> rids;

  for (auto key : keys) {
    rids.clear();
    index_key.SetFromInteger(key);
    is_present = tree.GetValue(index_key, &rids);

    EXPECT_EQ(is_present, true);
    EXPECT_EQ(rids.size(), 1);          // 每个 key 应该对应一个 RID
    EXPECT_EQ(rids[0].GetPageId(), 0);  // page_id
    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);  // slot num
  }

  delete bpm;
}

TEST(BPlusTreeTests, BasicInsertTest) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());  // 50个page
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  GenericKey<8> index_key;
  RID rid;

  int64_t key = 42;
  int64_t value = key & 0xFFFFFFFF;
  rid.Set(static_cast<int32_t>(key), value);
  index_key.SetFromInteger(key);
  tree.Insert(index_key, rid);

  auto root_page_id = tree.GetRootPageId();
  auto root_page_guard = bpm->ReadPage(root_page_id);
  auto root_page = root_page_guard.As<BPlusTreePage>();
  ASSERT_NE(root_page, nullptr);
  ASSERT_TRUE(root_page->IsLeafPage());

  auto root_as_leaf = root_page_guard.As<BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>>();
  ASSERT_EQ(root_as_leaf->GetSize(), 1);
  ASSERT_EQ(comparator(root_as_leaf->KeyAt(0), index_key), 0);

  delete bpm;
}

TEST(BPlusTreeTests, InsertTest1NoIterator) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  GenericKey<8> index_key;
  RID rid;

  std::vector<int64_t> keys = {1, 2, 3, 4, 5};
  for (auto key : keys) {
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);  // 右移32位就是0, 即 page_id = 0
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
  }

  bool is_present;
  std::vector<RID> rids;

  for (auto key : keys) {
    rids.clear();
    index_key.SetFromInteger(key);
    is_present = tree.GetValue(index_key, &rids);

    EXPECT_EQ(is_present, true);
    EXPECT_EQ(rids.size(), 1);
    EXPECT_EQ(rids[0].GetPageId(), 0);
    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);
  }
  delete bpm;
}

TEST(BPlusTreeTests, InsertTest2) {
  // create KeyComparator and index schema
  auto key_schema = ParseCreateStatement("a bigint");
  GenericComparator<8> comparator(key_schema.get());

  auto disk_manager = std::make_unique<DiskManagerUnlimitedMemory>();
  auto *bpm = new BufferPoolManager(50, disk_manager.get());
  // allocate header_page
  page_id_t page_id = bpm->NewPage();
  // create b+ tree
  BPlusTree<GenericKey<8>, RID, GenericComparator<8>> tree("foo_pk", page_id, bpm, comparator, 2, 3);
  GenericKey<8> index_key;
  RID rid;

  std::vector<int64_t> keys = {5, 4, 3, 2, 1};
  for (auto key : keys) {
    std::cerr << "Insert: " << key << "\n";
    int64_t value = key & 0xFFFFFFFF;
    rid.Set(static_cast<int32_t>(key >> 32), value);
    index_key.SetFromInteger(key);
    tree.Insert(index_key, rid);
  }

  std::vector<RID> rids;
  for (auto key : keys) {
    std::cerr << "Search: " << key << "\n";
    rids.clear();
    index_key.SetFromInteger(key);
    tree.GetValue(index_key, &rids);
    EXPECT_EQ(rids.size(), 1);

    int64_t value = key & 0xFFFFFFFF;
    EXPECT_EQ(rids[0].GetSlotNum(), value);
  }

  int64_t start_key = 1;
  int64_t current_key = start_key;
  std::cerr << "Traversing the tree ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
  for (auto iter = tree.Begin(); iter != tree.End(); ++iter) {
    auto pair = *iter;
    auto location = pair.second;

    // std::cerr << "current_key is " << pair.first << "\n"; // 全是1
    std::cerr << "current_key is " << (*iter).first << "\n";
    // auto key = pair.first;
    // std::cerr << "current_key is " << key << "\n";

    EXPECT_EQ(location.GetPageId(), 0);
    EXPECT_EQ(location.GetSlotNum(), current_key);
    current_key = current_key + 1;
  }

  EXPECT_EQ(current_key, keys.size() + 1);

  start_key = 3;
  current_key = start_key;
  index_key.SetFromInteger(start_key);
  std::cerr << "start the traversal at key 3 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
  for (auto iterator = tree.Begin(index_key); !iterator.IsEnd(); ++iterator) {
    auto location = (*iterator).second;
    std::cerr << "current_key is " << (*iterator).first << "\n";
    EXPECT_EQ(location.GetPageId(), 0);
    EXPECT_EQ(location.GetSlotNum(), current_key);
    current_key = current_key + 1;
  }
  delete bpm;
}
}  // namespace bustub
