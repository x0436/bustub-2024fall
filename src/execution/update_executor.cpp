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
  auto txn = exec_ctx_->GetTransaction();
  auto txn_mgr = exec_ctx_->GetTransactionManager();
  auto schema = child_executor_->GetOutputSchema();
  auto table_schema = table_info->schema_;

  // 获得主键的下标
  auto indexes = catalog->GetTableIndexes(table_info->name_);
  std::vector<uint32_t> pk_col_idxs;
  for (const auto &index_info : indexes) {
    if (index_info->is_primary_key_) {
      pk_col_idxs = index_info->index_->GetKeyAttrs();
      break;
    }
  }
  // 判断主键列是否被更新
  bool update_pk = false;
  for (auto pk_idx : pk_col_idxs) {
    auto &expr = plan_->target_expressions_[pk_idx];
    auto col_expr = dynamic_cast<const ColumnValueExpression *>(expr.get());
    if (col_expr == nullptr || col_expr->GetColIdx() != pk_idx) {
      update_pk = true;
      break;
    }
  }

  // 1. 收集所有 tuple
  std::vector<std::pair<Tuple, RID>> tuples;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    tuples.emplace_back(child_tuple, child_rid);
  }
  cnt_ = 0;


  for (const auto &[child_tuple, child_rid] : tuples) {
    auto tuple_meta = table_heap->GetTupleMeta(child_rid);
    IsWriteWriteConflict(txn, tuple_meta);  // 在修改前检查是否有写写冲突

    // new_tuple
    std::vector<Value> vals;
    vals.reserve(schema.GetColumnCount());
    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
      auto &expr = plan_->target_expressions_[i];
      vals.push_back(expr->Evaluate(&child_tuple, schema));
    }
    Tuple new_tuple(vals, &schema);

    if (update_pk) {
      // 如果更新的是主键
      Modify(&child_tuple, tuple_meta, nullptr, exec_ctx_, table_info.get());  // 删除base_tuple
      // 插入 new_tuple, 注意下面写错了, 不是纯插入, 可能要复活
      TupleMeta new_tuple_meta;
      new_tuple_meta.ts_ = txn->GetTransactionTempTs();  // 时间戳更新为 事务临时时间戳
      new_tuple_meta.is_deleted_ = false;
      auto key_rid = table_heap->InsertTuple(new_tuple_meta, new_tuple, exec_ctx_->GetLockManager(),
                                             exec_ctx_->GetTransaction(), plan_->GetTableOid());
      txn->AppendWriteSet(table_info->oid_, *key_rid);
      txn_mgr->UpdateUndoLink(*key_rid, UndoLink(), nullptr);  // 记录该tuple到版本库, 放置一个无效的UndoLink

      cnt_++;
      // 是否需要更新索引
    } else {
      // 如果更新的不是主键
      Modify(&child_tuple, tuple_meta, &new_tuple, exec_ctx_, table_info.get());
      cnt_++;

      // 更新该表相关的所有索引
      for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
        auto index = index_info->index_.get();
        auto key1 = child_tuple.KeyFromTuple(table_schema, *index->GetKeySchema(), index->GetKeyAttrs());
        index->DeleteEntry(key1, child_rid, exec_ctx_->GetTransaction());  // 删除

        auto key2 = new_tuple.KeyFromTuple(table_schema, *index->GetKeySchema(),
                                           index->GetKeyAttrs());          // 感觉子算子的schema和表的schema一样
        index->InsertEntry(key2, child_rid, exec_ctx_->GetTransaction());  // 插入
      }
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
