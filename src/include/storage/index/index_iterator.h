//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/index_iterator.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <utility>
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
 public:
  // you may define your own constructor based on your member variables
  IndexIterator(page_id_t leaf_page_id, int offset, BufferPoolManager *bpm);
  ~IndexIterator();  // NOLINT

  auto IsEnd() -> bool;

  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool {
    return cur_page_id_ == itr.cur_page_id_ && offset_ == itr.offset_;
  }

  auto operator!=(const IndexIterator &itr) const -> bool {
    return !(cur_page_id_ == itr.cur_page_id_ && offset_ == itr.offset_);
  }

 private:  // private
  // add your own private member variables here
  page_id_t cur_page_id_;
  int offset_;
  BufferPoolManager *bpm_;
  KeyType key_;
  ValueType value_;
};

}  // namespace bustub
