//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_leaf_page.cpp
//
// Copyright (c) 2018-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <sstream>

#include "common/exception.h"
#include "common/rid.h"
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set next page id and set max size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(page_id_t page_id, int max_size) {
  SetPageId(page_id);
  SetMaxSize(max_size);
  SetSize(0);
  SetPageType(IndexPageType::LEAF_PAGE);
  SetNextPageId(INVALID_PAGE_ID);
}

/**
 * Helper methods to set/get next page id
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::GetNextPageId() const -> page_id_t { return next_page_id_; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::SetNextPageId(page_id_t next_page_id) {
  next_page_id_ = next_page_id;
  if (next_page_id != INVALID_PAGE_ID) {
    // std::cerr << "set page " << GetPageId() << "'s next page id to " << next_page_id << "\n";
  }
}

/*
 * Helper method to find and return the key associated with input "index" (a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType { return rid_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(KeyType key, const KeyComparator &comparator) const -> int {
  int left = 0;
  int right = GetSize() - 1;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], key) < 0) {
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }
  return left;  // 没有判断key是否在leaf page里
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(KeyType key, ValueType value, const KeyComparator &comparator) -> bool {
  int index = Lookup(key, comparator);
  auto size = GetSize();
  if (index < size && comparator(key_array_[index], key) == 0) {  // key重复
    return false;
  }

  for (int i = size - 1; i >= index; i--) {
    key_array_[i + 1] = key_array_[i];
    rid_array_[i + 1] = rid_array_[i];
  }
  key_array_[index] = key;
  rid_array_[index] = value;
  SetSize(size + 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(B_PLUS_TREE_LEAF_PAGE_TYPE *recipient) {
  // 我设置新页存min_size, 原页存 GetSize() - min_size;
  int min_size = GetMinSize();
  int start_pos = GetSize() - min_size;
  std::copy(key_array_ + start_pos, key_array_ + GetSize(), recipient->key_array_);
  std::copy(rid_array_ + start_pos, rid_array_ + GetSize(), recipient->rid_array_);
  recipient->SetSize(min_size);
  SetSize(start_pos);
}

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Remove(KeyType key, const KeyComparator &comparator) -> bool {
  int pos = Lookup(key, comparator);  // 二分找到大于等于key的元素下标
  int size = GetSize();
  if (pos == size || comparator(KeyAt(pos), key) != 0) {  // 没找到
    return false;
  }
  // for (int i = pos; i <= size - 2; ++i) {
  //   key_array_[i] = key_array_[i + 1];
  //   rid_array_[i] = rid_array_[i + 1];
  // }
  //
  std::move(key_array_ + pos + 1, key_array_ + size, key_array_ + pos);
  std::move(rid_array_ + pos + 1, rid_array_ + size, rid_array_ + pos);
  SetSize(size - 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastTo(B_PLUS_TREE_LEAF_PAGE_TYPE *recipient) {
  int a_size = GetSize();
  int b_size = recipient->GetSize();

  // for (int i = b_size - 1; i >= 0; i--) { // 后移一位
  //   recipient->key_array_[i + 1] = recipient->key_array_[i];
  //   recipient->rid_array_[i + 1] = recipient->rid_array_[i];
  // }
  // recipient 的 [0, b_size) 右移 1 位, 目标为 [1, b_size+1)
  std::move_backward(recipient->key_array_, recipient->key_array_ + b_size, recipient->key_array_ + b_size + 1);
  std::move_backward(recipient->rid_array_, recipient->rid_array_ + b_size, recipient->rid_array_ + b_size + 1);

  std::copy(key_array_ + a_size - 1, key_array_ + a_size, recipient->key_array_);  // 加到最前面
  std::copy(rid_array_ + a_size - 1, rid_array_ + a_size, recipient->rid_array_);

  SetSize(a_size - 1);
  recipient->SetSize(b_size + 1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstTo(B_PLUS_TREE_LEAF_PAGE_TYPE *recipient) {
  int a_size = GetSize();
  int b_size = recipient->GetSize();

  std::copy(key_array_, key_array_ + 1, recipient->key_array_ + b_size);  // 加到末尾
  std::copy(rid_array_, rid_array_ + 1, recipient->rid_array_ + b_size);

  // 左移: 把 [1, a_size) 移到 [0, a_size-1)
  std::move(key_array_ + 1, key_array_ + a_size, key_array_);
  std::move(rid_array_ + 1, rid_array_ + a_size, rid_array_);
  SetSize(a_size - 1);
  recipient->SetSize(b_size + 1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(B_PLUS_TREE_LEAF_PAGE_TYPE *recipient) {
  int a_size = GetSize();
  int b_size = recipient->GetSize();

  std::copy(key_array_, key_array_ + a_size, recipient->key_array_ + b_size);
  std::copy(rid_array_, rid_array_ + a_size, recipient->rid_array_ + b_size);
  SetSize(0);
  recipient->SetSize(a_size + b_size);
  // recipient->SetNextPageId(GetNextPageId());
}

template class BPlusTreeLeafPage<GenericKey<4>, RID, GenericComparator<4>>;
template class BPlusTreeLeafPage<GenericKey<8>, RID, GenericComparator<8>>;
template class BPlusTreeLeafPage<GenericKey<16>, RID, GenericComparator<16>>;
template class BPlusTreeLeafPage<GenericKey<32>, RID, GenericComparator<32>>;
template class BPlusTreeLeafPage<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
