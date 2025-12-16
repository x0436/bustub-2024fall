//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  auto catalog = exec_ctx_->GetCatalog();
  auto table_info = catalog->GetTable(plan_->GetTableOid());
  if (!table_info) {
    throw bustub::Exception("table not found");
  }
  table_heap_ = table_info->table_.get();
  iterator_ = std::make_unique<TableIterator>(table_heap_->MakeIterator());  // 存
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto filter_expr = plan_->filter_predicate_;

  if (!iterator_ || iterator_->IsEnd()) {
    return false;
  }
  while (!iterator_->IsEnd()) {
    auto pair = iterator_->GetTuple();
    auto cur_rid = iterator_->GetRID();
    ++(*iterator_);
    if (!pair.first.is_deleted_ && filter_expr) {
      auto value = filter_expr->Evaluate(&pair.second, GetOutputSchema());
      if (!value.IsNull() && value.GetAs<bool>()) {
        *tuple = pair.second;
        *rid = cur_rid;
        return true;
      }
    } else if (!pair.first.is_deleted_) {
      *tuple = pair.second;
      *rid = cur_rid;
      return true;
    }
  }

  return false;
}

}  // namespace bustub
