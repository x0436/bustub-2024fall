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

  auto txn = exec_ctx_->GetTransaction();
  auto txn_mgr = exec_ctx_->GetTransactionManager();
  auto catalog = exec_ctx_->GetCatalog();
  auto table_info = catalog->GetTable(plan_->GetTableOid());
  const Schema *schema = &table_info->schema_;

  while (!iterator_->IsEnd()) {
    auto pair = iterator_->GetTuple();
    auto cur_rid = iterator_->GetRID();
    ++(*iterator_);

    // 获取 undo link
    auto undo_link = txn_mgr->GetUndoLink(cur_rid);
    // 收集 undo logs
    auto undo_logs_opt = CollectUndoLogs(cur_rid, pair.first, pair.second, undo_link, txn, txn_mgr);

    if (!undo_logs_opt.has_value()) {
      continue;  // 对当前事务不可见，跳过
    }
    // 重建 tuple
    std::optional<Tuple> visible_tuple = ReconstructTuple(schema, pair.second, pair.first, *undo_logs_opt);
    if (!visible_tuple.has_value()) {
      continue;  // 被删除，不可见
    }

    // 过滤
    if (filter_expr) {
      auto value = filter_expr->Evaluate(&(*visible_tuple), GetOutputSchema());
      if (!value.IsNull() && value.GetAs<bool>()) {
        *tuple = *visible_tuple;
        *rid = cur_rid;
        return true;
      }
    } else {
      *tuple = *visible_tuple;
      *rid = cur_rid;
      return true;
    }
  }

  return false;
}

}  // namespace bustub
