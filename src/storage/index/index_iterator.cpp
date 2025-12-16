/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(page_id_t leaf_page_id, int offset, BufferPoolManager *bpm)
    : cur_page_id_(leaf_page_id), offset_(offset), bpm_(bpm) {}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return cur_page_id_ == INVALID_PAGE_ID && offset_ == 0; }

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  // std::cerr << "page id is: " << cur_page_id_ << ", offset is " << offset_ << "\n";
  auto guard = bpm_->WritePage(cur_page_id_);
  auto *cur_page = guard.AsMut<B_PLUS_TREE_LEAF_PAGE_TYPE>();
  key_ = cur_page->KeyAt(offset_);
  value_ = cur_page->ValueAt(offset_);
  return {key_, value_};
}

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  auto cur_guard = bpm_->WritePage(cur_page_id_);
  auto *cur_page = cur_guard.AsMut<B_PLUS_TREE_LEAF_PAGE_TYPE>();
  offset_++;
  // std::cerr << "page id: " << cur_page_id_ << ", size is " << cur_page->GetSize() << " next page id is " <<
  // cur_page->GetNextPageId() << "\n";
  if (offset_ >= cur_page->GetSize()) {
    cur_page_id_ = cur_page->GetNextPageId();
    offset_ = 0;
  }
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;
template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;
template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;
template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;
template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;
}  // namespace bustub
