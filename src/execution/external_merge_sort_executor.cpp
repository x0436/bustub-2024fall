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
      plan_(plan),
      cmp_(plan->GetOrderBy()),
      child_executor_(std::move(child_executor)),
      bpm_(exec_ctx->GetBufferPoolManager()) {}

template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  // 初始化子算子
  child_executor_->Init();

  runs_.clear();

  const auto &schema = plan_->OutputSchema();
  const auto &order_bys = plan_->GetOrderBy();

  Tuple child_tuple;
  RID child_rid;

  // 准备第一页与对应的 SortPage
  page_id_t cur_pid = bpm_->NewPage();
  auto cur_guard = bpm_->WritePage(cur_pid);
  auto cur_page = cur_guard.AsMut<SortPage>();
  cur_page->Init(schema);

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    if (!cur_page->InsertTuple(child_tuple)) {
      cur_page->Sort(order_bys, schema);
      {
        std::vector<page_id_t> pages;
        pages.push_back(cur_pid);
        runs_.emplace_back(std::move(pages), bpm_);
      }
      cur_pid = bpm_->NewPage();
      cur_guard = bpm_->WritePage(cur_pid);
      cur_page = cur_guard.AsMut<SortPage>();
      cur_page->Init(schema);
      bool ok = cur_page->InsertTuple(child_tuple);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on a fresh page");
    }
  }

  if (cur_page->GetSize() > 0) {
    cur_page->Sort(order_bys, schema);
    std::vector<page_id_t> pages;
    pages.push_back(cur_pid);
    runs_.emplace_back(std::move(pages), bpm_);
  }
  cur_guard.Drop();  // 归并前释放写锁，避免自死锁

  // 归并
  while (runs_.size() > 1) {
    runs_ = MergePass(runs_);
  }

  // NEW: 空结果保护
  if (runs_.empty()) {
    empty_ = true;
    return;
  }
  empty_ = false;

  // 初始化最终游标
  it_ = runs_[0].Begin();
  end_ = runs_[0].End();
}
template <size_t K>
auto ExternalMergeSortExecutor<K>::MergeTwoRuns(const MergeSortRun &a, const MergeSortRun &b) -> MergeSortRun {
  // 新建输出 run 的第一页
  auto out_pid = bpm_->NewPage();
  auto out_guard = bpm_->WritePage(out_pid);
  auto out_page = out_guard.AsMut<SortPage>();
  out_page->Init(plan_->OutputSchema());

  std::vector<page_id_t> out_pages;
  out_pages.push_back(out_pid);

  // 输入迭代器
  auto it_a = a.Begin();
  auto end_a = a.End();
  auto it_b = b.Begin();
  auto end_b = b.End();

  // 归并（优化：缓存当前元素的排序键）
  Tuple ta;
  Tuple tb;
  std::optional<SortKey> ka;
  std::optional<SortKey> kb;
  if (it_a != end_a) {
    ta = *it_a;
    ka = GenerateSortKey(ta, plan_->GetOrderBy(), plan_->OutputSchema());
  }
  if (it_b != end_b) {
    tb = *it_b;
    kb = GenerateSortKey(tb, plan_->GetOrderBy(), plan_->OutputSchema());
  }

  while (it_a != end_a && it_b != end_b) {
    const bool pick_a = cmp_({*ka, ta}, {*kb, tb});
    Tuple to_write = pick_a ? ta : tb;
    if (!out_page->InsertTuple(to_write)) {
      // 当前页满，换新页
      out_pid = bpm_->NewPage();
      out_guard = bpm_->WritePage(out_pid);
      out_page = out_guard.AsMut<SortPage>();
      out_page->Init(plan_->OutputSchema());
      out_pages.push_back(out_pid);
      bool ok = out_page->InsertTuple(to_write);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on fresh page");
    }
    if (pick_a) {
      ++it_a;
      if (it_a != end_a) {
        ta = *it_a;
        ka = GenerateSortKey(ta, plan_->GetOrderBy(), plan_->OutputSchema());
      } else {
        ka.reset();
      }
    } else {
      ++it_b;
      if (it_b != end_b) {
        tb = *it_b;
        kb = GenerateSortKey(tb, plan_->GetOrderBy(), plan_->OutputSchema());
      } else {
        kb.reset();
      }
    }
  }

  // 剩余 a
  while (it_a != end_a) {
    Tuple t = *it_a;
    if (!out_page->InsertTuple(t)) {
      out_pid = bpm_->NewPage();
      out_guard = bpm_->WritePage(out_pid);
      out_page = out_guard.AsMut<SortPage>();
      out_page->Init(plan_->OutputSchema());
      out_pages.push_back(out_pid);
      bool ok = out_page->InsertTuple(t);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on fresh page");
    }
    ++it_a;
  }

  // 剩余 b
  while (it_b != end_b) {
    Tuple t = *it_b;
    if (!out_page->InsertTuple(t)) {
      out_pid = bpm_->NewPage();
      out_guard = bpm_->WritePage(out_pid);
      out_page = out_guard.AsMut<SortPage>();
      out_page->Init(plan_->OutputSchema());
      out_pages.push_back(out_pid);
      bool ok = out_page->InsertTuple(t);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on fresh page");
    }
    ++it_b;
  }

  // 关键优化：释放输入 runs 的页，避免占满缓冲池导致后续 I/O 慢/超时
  for (auto pid : a.GetPages()) {
    bpm_->DeletePage(pid);
  }
  for (auto pid : b.GetPages()) {
    bpm_->DeletePage(pid);
  }

  return {MergeSortRun(std::move(out_pages), bpm_)};
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MergePass(const std::vector<MergeSortRun> &runs) -> std::vector<MergeSortRun> {
  std::vector<MergeSortRun> next;
  next.reserve((runs.size() + 1) / 2);
  for (size_t i = 0; i + 1 < runs.size(); i += 2) {
    next.emplace_back(MergeTwoRuns(runs[i], runs[i + 1]));
  }
  if (runs.size() % 2 == 1) {
    next.emplace_back(runs.back());  // 奇数个，最后一个直接带入下一轮
  }
  return next;
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(Tuple *tuple, RID *rid) -> bool {
  if (empty_) {
    return false;
  }
  if (it_ == end_) {
    for (const auto &page_id : runs_[0].GetPages()) {
      bpm_->DeletePage(page_id);
    }
    return false;
  }
  Tuple t = *it_;
  *tuple = t;
  *rid = t.GetRid();  // 你未存 RID 时这里是默认 RID()
  ++it_;
  return true;
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
