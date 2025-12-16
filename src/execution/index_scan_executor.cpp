//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"

namespace bustub {
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  auto catalog = exec_ctx_->GetCatalog();
  index_info_ = catalog->GetIndex(plan_->GetIndexOid()).get();
  table_info_ = catalog->GetTable(plan_->table_oid_).get();
  tree_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());
  iterator_ = std::make_unique<BPlusTreeIndexIteratorForTwoIntegerColumn>(tree_->GetBeginIterator());
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto filter_expr = plan_->filter_predicate_;

  if (iterator_->IsEnd()) {
    return false;
  }
  while (!iterator_->IsEnd()) {
    auto pair = **iterator_;
    *rid = pair.second;
    auto tuple_pair = table_info_->table_->GetTuple(*rid);
    ++(*iterator_);

    if (!tuple_pair.first.is_deleted_ && filter_expr) {
      auto value = filter_expr->Evaluate(&tuple_pair.second, GetOutputSchema());
      if (!value.IsNull() && value.GetAs<bool>()) {
        *tuple = tuple_pair.second;
        return true;
      }
    } else if (!tuple_pair.first.is_deleted_) {
      *tuple = tuple_pair.second;
      return true;
    }
  }

  return false;
}

}  // namespace bustub
