//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>

#include "execution/executors/update_executor.h"

namespace bustub {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {
  // As of Fall 2022, you DON'T need to implement update executor to have perfect score in project 3 / project 4.
}

void UpdateExecutor::Init() {
  child_executor_->Init();
  auto catalog = exec_ctx_->GetCatalog();
  auto table_info = catalog->GetTable(plan_->GetTableOid());
  table_info_ = table_info.get();
  auto table_heap = table_info->table_.get();
  Tuple child_tuple;
  RID child_rid;
  cnt_ = 0;
  auto txn = exec_ctx_->GetTransaction();
  auto txn_mgr = exec_ctx_->GetTransactionManager();
  auto schema = child_executor_->GetOutputSchema();
  auto table_schema = table_info->schema_;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    // 删除
    auto tuple_meta = table_heap->GetTupleMeta(child_rid);
    IsWriteWriteConflict(txn, tuple_meta);  // 在修改前检查是否有写写冲突

    // 没有写写冲突
    auto undolink_opt = txn_mgr->GetUndoLink(child_rid);
    UndoLink undolink = undolink_opt.has_value() ? undolink_opt.value() : UndoLink();
    auto undolog_opt = txn_mgr->GetUndoLogOptional(undolink);
    bool has_log = undolog_opt.has_value();
    auto ts = tuple_meta.ts_;
    UndoLog undolog;

    // new_tuple
    std::vector<Value> vals;
    vals.reserve(schema.GetColumnCount());
    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
      auto &expr = plan_->target_expressions_[i];
      vals.push_back(expr->Evaluate(&child_tuple, schema));
    }
    Tuple new_tuple(vals, &schema);

    bool is_current_txn_insert =
        (undolink.prev_txn_ == INVALID_TXN_ID) && (tuple_meta.ts_ == txn->GetTransactionTempTs());

    if (!is_current_txn_insert) {
      if (!has_log || undolink.prev_txn_ != txn->GetTransactionId()) {
        // 第一次 update，追加 undo log
        undolog = GenerateNewUndoLog(&schema, &child_tuple, &new_tuple, ts, undolink);
        auto new_link = txn->AppendUndoLog(undolog);
        txn_mgr->UpdateUndoLink(child_rid, new_link);
      } else {
        // 已有 undo log，合并并覆盖
        undolog = GenerateUpdatedUndoLog(&schema, &child_tuple, &new_tuple, undolog_opt.value());
        txn->ModifyUndoLog(undolink.prev_log_idx_, undolog);
      }
    }
    txn->AppendWriteSet(table_info->oid_, child_rid);

    tuple_meta.ts_ = txn->GetTransactionTempTs();
    tuple_meta.is_deleted_ = false;

    table_heap->UpdateTupleInPlace(tuple_meta, new_tuple, child_rid);
    cnt_++;

    // 更新该表相关的所有索引
    for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
      auto index = index_info->index_.get();
      std::vector<Value> key_values{};
      key_values.reserve(index->GetIndexColumnCount());

      auto key1 = child_tuple.KeyFromTuple(table_schema, *index->GetKeySchema(), index->GetKeyAttrs());
      index->DeleteEntry(key1, child_rid, exec_ctx_->GetTransaction());  // 删除

      auto key2 = new_tuple.KeyFromTuple(table_schema, *index->GetKeySchema(),
                                         index->GetKeyAttrs());          // 感觉子算子的schema和表的schema一样
      index->InsertEntry(key2, child_rid, exec_ctx_->GetTransaction());  // 插入
    }
  }
}

auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  if (finished_) {
    return false;
  }
  std::vector<Value> values{};
  values.reserve(1);
  values.emplace_back(TypeId::INTEGER, cnt_);
  *tuple = Tuple{values, &GetOutputSchema()};
  finished_ = true;
  return true;
}

}  // namespace bustub
