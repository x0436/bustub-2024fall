//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/page/b_plus_tree_internal_page.cpp
//
// Copyright (c) 2018-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <sstream>

#include "common/exception.h"
#include "storage/page/b_plus_tree_internal_page.h"

namespace bustub {
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/
/*
 * Init method after creating a new internal page
 * Including set page type, set current size, and set max page size
 */
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(page_id_t page_id, int max_size) {
  SetPageId(page_id);
  SetMaxSize(max_size);
  SetSize(0);
  SetPageType(IndexPageType::INTERNAL_PAGE);
}
/*
 * Helper method to get/set the key associated with input "index" (a.k.a
 * array offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType { return key_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) { key_array_[index] = key; }
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) { page_id_array_[index] = value; }

/*
 * Helper method to get the value associated with input "index" (a.k.a array
 * offset)
 */
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType { return page_id_array_[index]; }

INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(KeyType key, const KeyComparator &comparator) const -> int {
  int left = 1;
  int right = GetSize() - 1;
  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], key) > 0) {
      right = mid - 1;
    } else {
      left = mid + 1;
    }
  }
  return left;  // 没有判断key是否在leaf page里
}
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Insert(KeyType key, ValueType value, const KeyComparator &comparator) {
  int index = Lookup(key, comparator);
  auto size = GetSize();
  for (int i = size - 1; i >= index; i--) {
    key_array_[i + 1] = key_array_[i];
    page_id_array_[i + 1] = page_id_array_[i];
  }
  key_array_[index] = key;
  page_id_array_[index] = value;
  SetSize(size + 1);
}
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyArray() -> KeyType * { return key_array_; }
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueArray() -> page_id_t * { return page_id_array_; }
INDEX_TEMPLATE_ARGUMENTS
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(KeyType key, const KeyComparator &comparator) -> bool {
  int left = 1;
  int right = GetSize() - 1;  // lower bound
  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (comparator(key_array_[mid], key) < 0) {
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }

  int pos = left;
  int size = GetSize();
  if (pos == size || comparator(KeyAt(pos), key) != 0) {
    return false;
  }
  // for (int i = pos; i <= size - 2; ++i) {
  //   key_array_[i] = key_array_[i + 1];
  //   page_id_array_[i] = page_id_array_[i + 1];
  // }
  std::move(key_array_ + pos + 1, key_array_ + size, key_array_ + pos);
  std::move(page_id_array_ + pos + 1, page_id_array_ + size, page_id_array_ + pos);
  SetSize(size - 1);
  return true;
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastTo(B_PLUS_TREE_INTERNAL_PAGE_TYPE *recipient) {
  int a_size = GetSize();
  int b_size = recipient->GetSize();

  // for (int i = b_size - 1; i >= 0; i--) { // 后移一位
  //   recipient->key_array_[i + 1] = recipient->key_array_[i];
  //   recipient->page_id_array_[i + 1] = recipient->page_id_array_[i];
  // }
  // recipient 的 [0, b_size) 右移 1 位, 目标为 [1, b_size+1)
  std::move_backward(recipient->key_array_, recipient->key_array_ + b_size, recipient->key_array_ + b_size + 1);
  std::move_backward(recipient->page_id_array_, recipient->page_id_array_ + b_size,
                     recipient->page_id_array_ + b_size + 1);

  std::copy(key_array_ + a_size - 1, key_array_ + a_size, recipient->key_array_);  // 加到最前面
  std::copy(page_id_array_ + a_size - 1, page_id_array_ + a_size, recipient->page_id_array_);

  SetSize(a_size - 1);
  recipient->SetSize(b_size + 1);
}

INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstTo(B_PLUS_TREE_INTERNAL_PAGE_TYPE *recipient) {
  int a_size = GetSize();
  int b_size = recipient->GetSize();

  std::copy(key_array_, key_array_ + 1, recipient->key_array_ + b_size);  // 加到末尾
  std::copy(page_id_array_, page_id_array_ + 1, recipient->page_id_array_ + b_size);

  // 左移: 把 [1, a_size) 移到 [0, a_size-1)
  std::move(key_array_ + 1, key_array_ + a_size, key_array_);
  std::move(page_id_array_ + 1, page_id_array_ + a_size, page_id_array_);
  SetSize(a_size - 1);
  recipient->SetSize(b_size + 1);
}
INDEX_TEMPLATE_ARGUMENTS
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(B_PLUS_TREE_INTERNAL_PAGE_TYPE *recipient) {
  int a_size = GetSize();
  int b_size = recipient->GetSize();

  std::copy(key_array_, key_array_ + a_size, recipient->key_array_ + b_size);
  std::copy(page_id_array_, page_id_array_ + a_size, recipient->page_id_array_ + b_size);
  SetSize(0);
  recipient->SetSize(a_size + b_size);
}

// valuetype for internalNode should be page id_t
template class BPlusTreeInternalPage<GenericKey<4>, page_id_t, GenericComparator<4>>;
template class BPlusTreeInternalPage<GenericKey<8>, page_id_t, GenericComparator<8>>;
template class BPlusTreeInternalPage<GenericKey<16>, page_id_t, GenericComparator<16>>;
template class BPlusTreeInternalPage<GenericKey<32>, page_id_t, GenericComparator<32>>;
template class BPlusTreeInternalPage<GenericKey<64>, page_id_t, GenericComparator<64>>;
}  // namespace bustub
