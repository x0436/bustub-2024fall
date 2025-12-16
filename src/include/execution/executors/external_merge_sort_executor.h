//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * Page to hold the intermediate data for external merge sort.
 *
 * Only fixed-length data will be supported in Fall 2024.
 */
class SortPage {
 public:
  /**
   * TODO: Define and implement the methods for reading data from and writing data to the sort
   * page. Feel free to add other helper methods.
   */
  SortPage(page_id_t page_id) : page_id_(page_id) {}
  // 将tuple插入 page
  bool InsertTuple(Tuple &tuple) {
    if (cur_size_ == capacity_) {
      return false;
    }
    auto target = reinterpret_cast<Tuple *>(SlotPtr(cur_size_));
    *target = tuple;
    cur_size_++;
  }
  Tuple GetTuple(size_t idx) const {
    if (idx >= capacity_) {
      throw Exception("SortPage index out of bounds");
    }
    auto ptr = reinterpret_cast<Tuple *>(SlotPtr(idx));
    return *ptr;
  }
  size_t GetSize() const { return cur_size_; }

  bool IsFull() { return cur_size_ == capacity_; }
  // 其实可以考虑写到构造函数里
  void Init(const Schema &schema) {
    t_size_ = schema.GetInlinedStorageSize();
    capacity_ = (BUFFER_POOL_SIZE - 16) / t_size_;
  }
  /* 调用TupleComparator对Page进行内部排序 */
  void Sort(const std::vector<OrderBy> &order_bys) {
    std::sort(SlotPtr(0), SlotPtr(capacity_), TupleComparator(order_bys));  // 左开右闭
  }

 private:
  /**
   * TODO: Define the private members. You may want to have some necessary metadata for
   * the sort page before the start of the actual data.
   */
  inline char *SlotPtr(size_t idx) const { return data_ + 16 + idx * t_size_; }  // ai教的
  size_t t_size_{0};                                                             // tuple的大小+metadata的大小
  size_t capacity_{0};
  size_t cur_size_{0};
  page_id_t page_id_{INVALID_PAGE_ID};
  char *data_{nullptr};
};

/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 */
class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

  auto GetPageCount() -> size_t { return pages_.size(); }

  /** Iterator for iterating on the sorted tuples in one run. */
  class Iterator {
    friend class MergeSortRun;

   public:
    Iterator(MergeSortRun *run, page_id_t page_id, size_t offset, size_t index)
        : run_(run), page_id_(page_id), offset_(offset), index_(index) {
      auto guard = run_->bpm_->ReadPage(page_id_);
      const auto *page = guard.As<SortPage>();
      tuple_ = page->GetTuple(offset_);
    }

    /**
     * Advance the iterator to the next tuple. If the current sort page is exhausted, move to the
     * next sort page.
     *
     * TODO: Implement this method.
     */
    auto operator++() -> Iterator & {
      offset_++;
      if (offset_ == run_->page_capacity_) {
        offset_ = 0;
        page_id_ = run_->pages_[index_ + 1];
        index_++;
      }
    }

    /**
     * Dereference the iterator to get the current tuple in the sorted run that the iterator is
     * pointing to.
     *
     * TODO: Implement this method.
     */
    auto operator*() -> Tuple { return tuple_; }

    /**
     * Checks whether two iterators are pointing to the same tuple in the same sorted run.
     *
     * TODO: Implement this method.
     */
    auto operator==(const Iterator &other) const -> bool {
      return run_ == other.run_ && page_id_ == other.page_id_ && offset_ == other.offset_;
    }

    /**
     * Checks whether two iterators are pointing to different tuples in a sorted run or iterating
     * on different sorted runs.
     *
     * TODO: Implement this method.
     */
    auto operator!=(const Iterator &other) const -> bool { return !(*this == other); }

   private:
    explicit Iterator(const MergeSortRun *run) : run_(run) {}

    /** The sorted run that the iterator is iterating on. */
    [[maybe_unused]] const MergeSortRun *run_;
    page_id_t page_id_;
    size_t offset_;
    Tuple tuple_;
    size_t index_;
    /**
     * TODO: Add your own private members here. You may want something to record your current
     * position in the sorted run. Also feel free to add additional constructors to initialize
     * your private members.
     */
  };

  /**
   * Get an iterator pointing to the beginning of the sorted run, i.e. the first tuple.
   *
   * TODO: Implement this method.
   */
  auto Begin() -> Iterator { return Iterator(this, pages_[0], 0, 0); }

  /**
   * Get an iterator pointing to the end of the sorted run, i.e. the position after the last tuple.
   *
   * TODO: Implement this method.
   */
  auto End() -> Iterator {
    page_id_t last_page_id = pages_[pages_.size() - 1];
    auto guard = bpm_->ReadPage(last_page_id);
    const auto *last_page = guard.As<SortPage>();
    // offset, index
    return Iterator(this, last_page_id, last_page->GetSize() - 1, pages_.size() - 1);
  }

 private:
  /** The page IDs of the sort pages that store the sorted tuples. */
  std::vector<page_id_t> pages_;
  /**
   * The buffer pool manager used to read sort pages. The buffer pool manager is responsible for
   * deleting the sort pages when they are no longer needed.
   */
  [[maybe_unused]] BufferPoolManager *bpm_;
  size_t page_capacity_;  // 由于只测定固定长度的数据, 所以变量放着方便使用
};

/**
 * ExternalMergeSortExecutor executes an external merge sort.
 *
 * In Fall 2024, only 2-way external merge sort is required.
 */
template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
 public:
  ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> &&child_executor);

  /** Initialize the external merge sort */
  void Init() override;

  /**
   * Yield the next tuple from the external merge sort.
   * @param[out] tuple The next tuple produced by the external merge sort.
   * @param[out] rid The next tuple RID produced by the external merge sort.
   * @return `true` if a tuple was produced, `false` if there are no more tuples
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the external merge sort */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  void MergeSort(int left, int right, std::vector<MergeSortRun> &runs);
  /** The sort plan node to be executed */
  const SortPlanNode *plan_;

  /** Compares tuples based on the order-bys */
  TupleComparator cmp_;
  std::unique_ptr<AbstractExecutor> child_executor_;
  vector<MergeSortRun> runs_;
  /** TODO: You will want to add your own private members here. */
};

}  // namespace bustub
