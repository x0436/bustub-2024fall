//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/insert_executor.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  child_executor_->Init();
  auto catalog = exec_ctx_->GetCatalog();
  auto table_info = catalog->GetTable(plan_->GetTableOid());
  auto table_heap = table_info->table_.get();
  Tuple child_tuple;
  RID child_rid;
  cnt_ = 0;
  
  auto txn = exec_ctx_->GetTransaction(); // 获取事务
  auto txn_mgr = exec_ctx_->GetTransactionManager();

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    TupleMeta tuple_meta;
    tuple_meta.ts_ = txn->GetTransactionTempTs(); // 时间戳更新为 事务临时时间戳
    tuple_meta.is_deleted_ = false;
    auto key_rid = table_heap->InsertTuple(tuple_meta, child_tuple, exec_ctx_->GetLockManager(),
                                           exec_ctx_->GetTransaction(), plan_->GetTableOid());
    txn->AppendWriteSet(table_info->oid_, *key_rid); // 插入tuple后, 更新事务的写集合
    txn_mgr->UpdateUndoLink(*key_rid, UndoLink(), nullptr); // 记录该tuple到版本库, 放置一个无效的UndoLink
    cnt_++;

    // 更新该表相关的所有索引
    for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
      auto index = index_info->index_.get();
      std::vector<Value> key_values{};
      key_values.reserve(index->GetIndexColumnCount());

      auto key =
          child_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(), index->GetKeyAttrs());
      index->InsertEntry(key, *key_rid, exec_ctx_->GetTransaction());
    }
  }

}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
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
