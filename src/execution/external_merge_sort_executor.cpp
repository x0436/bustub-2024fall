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
  // 初始化子算子
  child_executor_->Init();

  runs_.clear();

  const auto &schema = plan_->OutputSchema();
  const auto &order_bys = plan_->GetOrderBy();

  Tuple child_tuple;
  RID child_rid;

  // 准备第一页与对应的 SortPage
  page_id_t cur_pid = exec_ctx_->bpm_->NewPage();
  auto cur_guard = exec_ctx_->bpm_->WritePage(cur_pid);
  auto cur_page = cur_guard.AsMut<SortPage>();
  cur_page->Init(schema);

  // 不断从子算子拉取 tuple，塞满一页就排序并封装为一个 run
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    if (!cur_page->InsertTuple(child_tuple)) {
      // 当前页已满：先对页内数据排序，然后封装为一个 run
      cur_page->Sort(order_bys, schema);
      {
        std::vector<page_id_t> pages;
        pages.push_back(cur_pid);
        runs_.emplace_back(std::move(pages), exec_ctx_->bpm_);
      }

      // 新开一页继续写入
      cur_pid = exec_ctx_->bpm_->NewPage();
      cur_guard = exec_ctx_->bpm_->WritePage(cur_pid);
      cur_page = cur_guard.AsMut<SortPage>();
      cur_page->Init(schema);

      // 重新插入当前 tuple（新页应当能放下）
      bool ok = cur_page->InsertTuple(child_tuple);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on a fresh page");
    }
  }

  // 收尾：如果最后一页里有数据，也要排序并封装为一个 run
  if (cur_page->GetSize() > 0) {
    cur_page->Sort(order_bys, schema);
    std::vector<page_id_t> pages;
    pages.push_back(cur_pid);
    runs_.emplace_back(std::move(pages), exec_ctx_->bpm_);
  } else {
    // 若最后一页没写入任何 tuple，可交还该页（可选，取决于 bpm 接口是否需要显式回收）
    // exec_ctx_->bpm_->DeletePage(cur_pid);
  }

  // 多轮两路归并，直到只剩一个 run
  while (runs_.size() > 1) {
    runs_ = MergePass(runs_);
  }

  // 初始化最终 run 及成员迭代器
  if (!runs_.empty()) {
    final_run_ = std::move(runs_.front());
    it_ = final_run_.Begin();
    end_ = final_run_.End();
  } else {
    // 空输入情况下，设置为空迭代器
    final_run_ = MergeSortRun({}, exec_ctx_->bpm_);
    it_ = final_run_.End();
    end_ = final_run_.End();
  }
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::MergeTwoRuns(const MergeSortRun &a, const MergeSortRun &b) -> MergeSortRun {
  // 新建输出 run 的第一页
  auto out_pid = exec_ctx_->bpm_->NewPage();
  auto out_guard = exec_ctx_->bpm_->WritePage(out_pid);
  auto out_page = out_guard.AsMut<SortPage>();
  out_page->Init(plan_->OutputSchema());

  std::vector<page_id_t> out_pages;
  out_pages.push_back(out_pid);

  // 输入迭代器
  auto it_a = a.Begin();
  auto end_a = a.End();
  auto it_b = b.Begin();
  auto end_b = b.End();

  // 归并
  while (it_a != end_a && it_b != end_b) {
    Tuple ta = *it_a;
    Tuple tb = *it_b;

    const auto ka = GenerateSortKey(ta, plan_->GetOrderBy(), plan_->OutputSchema());
    const auto kb = GenerateSortKey(tb, plan_->GetOrderBy(), plan_->OutputSchema());
    const bool pick_a = cmp_({ka, ta}, {kb, tb});

    Tuple to_write = pick_a ? ta : tb;
    if (!out_page->InsertTuple(to_write)) {  // 这处理真好
      // 当前页满，换新页
      out_pid = exec_ctx_->bpm_->NewPage();
      out_guard = exec_ctx_->bpm_->WritePage(out_pid);
      out_page = out_guard.AsMut<SortPage>();
      out_page->Init(plan_->OutputSchema());
      out_pages.push_back(out_pid);
      // 重新插入
      bool ok = out_page->InsertTuple(to_write);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on fresh page");
    }

    if (pick_a) {
      ++it_a;
    } else {
      ++it_b;
    }
  }

  // 剩余 a
  while (it_a != end_a) {
    Tuple ta = *it_a;
    if (!out_page->InsertTuple(ta)) {
      out_pid = exec_ctx_->bpm_->NewPage();
      out_guard = exec_ctx_->bpm_->WritePage(out_pid);
      out_page = out_guard.AsMut<SortPage>();
      out_page->Init(plan_->OutputSchema());
      out_pages.push_back(out_pid);
      bool ok = out_page->InsertTuple(ta);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on fresh page");
    }
    ++it_a;
  }

  // 剩余 b
  while (it_b != end_b) {
    Tuple tb = *it_b;
    if (!out_page->InsertTuple(tb)) {
      out_pid = exec_ctx_->bpm_->NewPage();
      out_guard = exec_ctx_->bpm_->WritePage(out_pid);
      out_page = out_guard.AsMut<SortPage>();
      out_page->Init(plan_->OutputSchema());
      out_pages.push_back(out_pid);
      bool ok = out_page->InsertTuple(tb);
      BUSTUB_ASSERT(ok, "InsertTuple must succeed on fresh page");
    }
    ++it_b;
  }

  return MergeSortRun(std::move(out_pages), exec_ctx_->bpm_);
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

// 实现 Next：遍历最终唯一 run（假设 Init 后 runs_ 被归并到 1 个）
template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(Tuple *tuple, RID *rid) -> bool {
  if (it_ == end_) {
    return false;
  }
  Tuple t = *it_;
  *tuple = t;
  *rid = t.GetRid();
  ++it_;
  return true;
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
