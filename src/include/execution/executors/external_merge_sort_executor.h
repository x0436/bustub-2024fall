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

#include "buffer/buffer_pool_manager.h"

namespace bustub {

/**
 * Page to hold the intermediate data for external merge sort.
 *
 * Only fixed-length data will be supported in Fall 2024.
 */
class SortPage {
 public:
  void Init(const Schema &schema) {
    t_size_ = schema.GetInlinedStorageSize();  // 固定长度
    cur_size_ = 0;
    capacity_ = (BUSTUB_PAGE_SIZE - K_HEADER_SIZE) / t_size_;
  }

  auto InsertTuple(Tuple &tuple) -> bool {
    if (cur_size_ >= capacity_) {
      return false;
    }
    BUSTUB_ASSERT(tuple.GetLength() == t_size_, "Only fixed-length tuples are supported.");
    std::memcpy(SlotPtr(cur_size_), tuple.GetData(), t_size_);
    cur_size_++;
    return true;
  }

  auto GetTuple(size_t idx) const -> Tuple {
    if (idx >= cur_size_) {
      throw Exception("SortPage index out of bounds");
    }
    const char *data = SlotPtr(idx);
    return {Tuple(RID(), data, static_cast<uint32_t>(t_size_))};
  }

  auto GetSize() const -> size_t { return cur_size_; }
  auto IsFull() const -> bool { return cur_size_ == capacity_; }

  void Sort(const std::vector<OrderBy> &order_bys, const Schema &schema) {
    // 优化：一次性生成排序键 + 使用 std::sort
    std::vector<std::pair<SortKey, Tuple>> buf;
    buf.reserve(cur_size_);
    for (size_t i = 0; i < cur_size_; i++) {
      Tuple t = GetTuple(i);
      buf.emplace_back(GenerateSortKey(t, order_bys, schema), std::move(t));
    }
    TupleComparator cmp(order_bys);
    std::sort(buf.begin(), buf.end(), [&](const auto &a, const auto &b) {
      return cmp({a.first, a.second}, {b.first, b.second});
    });
    for (size_t i = 0; i < buf.size(); i++) {
      std::memcpy(SlotPtr(i), buf[i].second.GetData(), t_size_);
    }
  }

 private:
  static const size_t K_HEADER_SIZE;
  inline auto Base() -> char * { return reinterpret_cast<char *>(this); }
  inline auto Base() const -> const char * { return reinterpret_cast<const char *>(this); }
  inline auto SlotPtr(size_t idx) -> char * { return Base() + K_HEADER_SIZE + idx * t_size_; }
  inline auto SlotPtr(size_t idx) const -> const char * { return Base() + K_HEADER_SIZE + idx * t_size_; }

  size_t t_size_{0};
  size_t capacity_{0};
  size_t cur_size_{0};
};
inline const size_t SortPage::K_HEADER_SIZE = sizeof(SortPage);
/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 */
class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

  auto GetPageCount() const -> size_t { return pages_.size(); }
  auto GetPages() -> std::vector<page_id_t> & { return pages_; }
  auto GetPages() const -> const std::vector<page_id_t> & { return pages_; }
  /** Iterator for iterating on the sorted tuples in one run. */
  class Iterator {
    friend class MergeSortRun;

   public:
    Iterator() = default;
    Iterator(const MergeSortRun *run, page_id_t page_id, size_t offset, size_t index)
        : run_(run), page_id_(page_id), offset_(offset), index_(index), at_end_(false) {
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
      if (at_end_) {
        return *this;
      }
      {  // 同页前进，读一次当前页
        auto guard = run_->bpm_->ReadPage(page_id_);
        const auto *page = guard.As<SortPage>();
        size_t sz = page->GetSize();
        if (offset_ + 1 < sz) {
          offset_ += 1;
          tuple_ = page->GetTuple(offset_);
          return *this;
        }
      }  // 释放当前页锁

      // 跨页并跳过空页
      for (index_ += 1; index_ < run_->pages_.size(); index_ += 1) {
        page_id_ = run_->pages_[index_];
        offset_ = 0;
        auto guard = run_->bpm_->ReadPage(page_id_);
        const auto *page = guard.As<SortPage>();
        if (page->GetSize() > 0) {
          tuple_ = page->GetTuple(0);
          return *this;
        }
      }
      at_end_ = true;
      return *this;
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
      if (at_end_ && other.at_end_) {
        return true;
      }
      return run_ == other.run_ && page_id_ == other.page_id_ && offset_ == other.offset_ && index_ == other.index_ &&
             at_end_ == other.at_end_;
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
    bool at_end_{true};
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
  auto Begin() -> Iterator {
    if (pages_.empty()) {
      Iterator it(this);
      it.at_end_ = true;
      return it;
    }
    return {Iterator(this, pages_[0], 0, 0)};
  }

  /**
   * Get an iterator pointing to the end of the sorted run, i.e. the position after the last tuple.
   *
   * TODO: Implement this method.
   */
  auto End() -> Iterator {
    Iterator it(this);
    it.at_end_ = true;
    return it;
  }

  auto Begin() const -> Iterator { return const_cast<MergeSortRun *>(this)->Begin(); }
  auto End() const -> Iterator { return const_cast<MergeSortRun *>(this)->End(); }

 private:
  /** The page IDs of the sort pages that store the sorted tuples. */
  std::vector<page_id_t> pages_;
  /**
   * The buffer pool manager used to read sort pages. The buffer pool manager is responsible for
   * deleting the sort pages when they are no longer needed.
   */
  [[maybe_unused]] BufferPoolManager *bpm_;
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
  auto MergeTwoRuns(const MergeSortRun &a, const MergeSortRun &b) -> MergeSortRun;
  auto MergePass(const std::vector<MergeSortRun> &runs) -> std::vector<MergeSortRun>;
  /** The sort plan node to be executed */
  const SortPlanNode *plan_;

  /** Compares tuples based on the order-bys */
  TupleComparator cmp_;
  std::unique_ptr<AbstractExecutor> child_executor_;
  std::vector<MergeSortRun> runs_;
  /** TODO: You will want to add your own private members here. */
  MergeSortRun::Iterator it_;
  MergeSortRun::Iterator end_;
  bool empty_{false};  // NEW: 标记是否无输出
  BufferPoolManager *bpm_;
};

}  // namespace bustub
