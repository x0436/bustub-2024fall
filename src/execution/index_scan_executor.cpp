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
  iterator_ = std::make_unique<BPlusTreeIndexIteratorForTwoIntegerColumn>(tree_->GetBeginIterator());  // 索引迭代器
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto filter_expr = plan_->filter_predicate_;

  if (iterator_->IsEnd()) {
    return false;
  }

  auto txn = exec_ctx_->GetTransaction();
  auto txn_mgr = exec_ctx_->GetTransactionManager();
  const Schema *schema = &table_info_->schema_;

  while (!iterator_->IsEnd()) {
    auto pair = **iterator_;
    auto cur_rid = pair.second;
    *rid = cur_rid;
    auto tuple_pair = table_info_->table_->GetTuple(*rid);
    ++(*iterator_);

    // 获取 undo link
    auto undo_link = txn_mgr->GetUndoLink(cur_rid);
    // 收集 undo logs
    auto undo_logs_opt = CollectUndoLogs(cur_rid, tuple_pair.first, tuple_pair.second, undo_link, txn, txn_mgr);
    if (!undo_logs_opt.has_value()) {
      continue;  // 对当前事务不可见，跳过
    }
    // 重建 tuple
    std::optional<Tuple> visible_tuple = ReconstructTuple(schema, tuple_pair.second, tuple_pair.first, *undo_logs_opt);
    if (!visible_tuple.has_value()) {
      continue;  // 被删除，不可见
    }

    // 如果该 tuple 没有被标记为删除，并且有过滤谓词
    if (filter_expr) {
      // 计算谓词表达式的值
      auto value = filter_expr->Evaluate(&(*visible_tuple), GetOutputSchema());
      // 如果谓词结果为 true，则返回该 tuple
      if (!value.IsNull() && value.GetAs<bool>()) {
        *tuple = *visible_tuple;
        return true;
      }
      // 如果没有谓词，只要 tuple 没有被删除就返回
    } else {
      *tuple = *visible_tuple;
      return true;
    }
  }

  return false;
}

}  // namespace bustub
