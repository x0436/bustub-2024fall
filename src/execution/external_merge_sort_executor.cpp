//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"
#include <iostream>
#include <optional>
#include <vector>
#include "common/config.h"
#include "execution/plans/sort_plan.h"

namespace bustub {

template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      cmp_(plan->GetOrderBy()),
      plan_(plan),
      child_executor_(std::move(std::move(child_executor))) {}

template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  Tuple child_tuple;
  RID child_rid;
  
  auto first_page_id = exec_ctx_->bpm_->NewPage();
  auto first_guard = exec_ctx_->bpm_->WritePage(first_page_id);
  auto cur_page = first_guard.AsMut<SortPage>();

  vector<MergeSortRun> runs;
  runs[0].pages_.push_back(first_page_id);

  // 从子算子获取tuple加入到sortPage, sortPage满了就下一个
  while (child_executor_->Next(child_tuple, child_rid)) {
    if (cur_page is full) {
      create a new page cur_page = new_page
    }
    insert tuple into cur_page
  }
  // 全部插入完毕
  // pass0 内部排序
  for each run in runs
    run.pages_[0].Sort(plan_->order_bys_);
  // 现在runs中每个单独的run所指page内部都是有序的
  MergeSort
}

void ExternalMergeSortExecutor<K>::MergeSort(int left, int right, std::vector<MergeSortRun> &runs) {
  if (left >= right) {
    return;
  }
  int mid = left + (right - left) / 2;
  int i = left;
  int j = mid + 1;
  MergeSort(left, mid, runs);
  MergeSort(mid + 1, right, runs);

  std::vector<MergeSortRun> temp;
  // 每轮循环取最小的tuple到temp里
  while (i <= mid && j <= right) {
    // 将这两个区间的runs 合并
  }
  // ...
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(Tuple *tuple, RID *rid) -> bool {
  // 遍历唯一的run, 用迭代器输出
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
