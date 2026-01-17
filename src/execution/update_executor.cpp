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
  // auto txn_mgr = exec_ctx_->GetTransactionManager();
  auto schema = child_executor_->GetOutputSchema();
  auto table_schema = table_info->schema_;

  // 获得主键的下标
  std::vector<uint32_t> pk_col_idxs;
  Index *pk_index;  // 唯一的主键索引
  for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
    if (index_info->is_primary_key_) {
      pk_index = index_info->index_.get();
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

  // 收集所有 tuple
  std::vector<std::pair<Tuple, RID>> tuples;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    tuples.emplace_back(child_tuple, child_rid);
  }
  cnt_ = 0;
  // 如果要更新主键, 先把所有tuple都删除
  if (update_pk) {
    for (const auto &[child_tuple, child_rid] : tuples) {
      auto tuple_meta = table_heap->GetTupleMeta(child_rid);
      Modify(&child_tuple, tuple_meta, nullptr, exec_ctx_, table_info.get());
    }
  }

  // 遍历存储的tuple
  for (const auto &[child_tuple, child_rid] : tuples) {
    auto tuple_meta = table_heap->GetTupleMeta(child_rid);

    // new_tuple
    std::vector<Value> vals;
    vals.reserve(schema.GetColumnCount());
    for (uint32_t i = 0; i < schema.GetColumnCount(); ++i) {
      auto &expr = plan_->target_expressions_[i];
      vals.push_back(expr->Evaluate(&child_tuple, schema));
    }
    Tuple new_tuple(vals, &schema);

    // 1 更新主键
    if (update_pk) {
      // 查主键索引中是否有重复key
      auto key = new_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *pk_index->GetKeySchema(),
                                        pk_index->GetKeyAttrs());
      std::vector<RID> result;
      pk_index->ScanKey(key, &result, txn);
      if (!result.empty()) {
        // 1.1 重复key存在, 复活
        auto old_rid = result.front();
        auto old_meta = table_heap->GetTuple(old_rid).first;
        auto old_tuple = table_heap->GetTuple(old_rid);
        Modify(&old_tuple.second, old_meta, &new_tuple, exec_ctx_, table_info.get());
        // 更新索引, 除开主键索引外都要更新
        for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
          auto index = index_info->index_.get();
          auto key1 = old_tuple.second.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(),
                                                    index->GetKeyAttrs());
          auto key2 =
              new_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(), index->GetKeyAttrs());
          if (index_info->is_primary_key_) {
            continue;
          } else {
            index->DeleteEntry(key1, old_rid, exec_ctx_->GetTransaction());
            index->InsertEntry(key2, old_rid, exec_ctx_->GetTransaction());
          }
        }
      } else {
        // 1.2 不存在重复key, 直接插入
        TupleMeta new_tuple_meta;
        new_tuple_meta.ts_ = txn->GetTransactionTempTs();  // 时间戳更新为 事务临时时间戳
        new_tuple_meta.is_deleted_ = false;
        auto key_rid = table_heap->InsertTuple(new_tuple_meta, new_tuple, exec_ctx_->GetLockManager(),
                                               exec_ctx_->GetTransaction(), plan_->GetTableOid());
        txn->AppendWriteSet(table_info->oid_, *key_rid);
        // txn_mgr->UpdateUndoLink(*key_rid, UndoLink(), nullptr);
        // 插入索引
        for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
          auto index = index_info->index_.get();
          auto key =
              new_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(), index->GetKeyAttrs());
          // 主键索引插入时要检测冲突
          if (index_info->is_primary_key_) {
            if (!index->InsertEntry(key, *key_rid, exec_ctx_->GetTransaction())) {
              txn->SetTainted();
              throw ExecutionException("(2)unique key conflict");
            }
          } else {
            index->InsertEntry(key, *key_rid, exec_ctx_->GetTransaction());
          }
        }
      }
    } else {
      // 2 更新的不是主键, 直接更新
      Modify(&child_tuple, tuple_meta, &new_tuple, exec_ctx_, table_info.get());

      // 更新该表相关的所有索引
      for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
        if (index_info->is_primary_key_) {
          continue;
        }
        auto index = index_info->index_.get();
        auto key1 = child_tuple.KeyFromTuple(table_schema, *index->GetKeySchema(), index->GetKeyAttrs());
        index->DeleteEntry(key1, child_rid, exec_ctx_->GetTransaction());  // 删除

        auto key2 = new_tuple.KeyFromTuple(table_schema, *index->GetKeySchema(),
                                           index->GetKeyAttrs());          // 感觉子算子的schema和表的schema一样
        index->InsertEntry(key2, child_rid, exec_ctx_->GetTransaction());  // 插入
      }
    }
    cnt_++;
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
