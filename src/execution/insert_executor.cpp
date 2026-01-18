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

  auto txn = exec_ctx_->GetTransaction();  // 获取事务
  // auto txn_mgr = exec_ctx_->GetTransactionManager();

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    bool skip_insert = false;  // 如果是插入删除的tuple, 成功后跳过本轮while
    // 拿到tuple之后去主键索引里找有没有重复的, 有就Tained
    for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
      if (index_info->is_primary_key_) {
        auto index = index_info->index_.get();
        auto key =
            child_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(), index->GetKeyAttrs());
        std::vector<RID> result;
        index->ScanKey(key, &result, txn);
        if (!result.empty()) {
          // 主键已存在
          auto old_rid = result.front();
          auto old_meta = table_heap->GetTuple(old_rid).first;
          if (old_meta.is_deleted_) {
            // 1.1 主键已存在且被删除, 执行"复活"操作
            auto old_tuple = table_heap->GetTuple(old_rid);
            Modify(old_rid, &child_tuple, exec_ctx_,
                   table_info.get());  // 这里为什么不填 nullptr 的
            skip_insert = true;
            // 1.2 复活也需要更新索引, 除开主键索引外都要更新
            for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
              auto index = index_info->index_.get();
              auto key1 = old_tuple.second.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(),
                                                        index->GetKeyAttrs());
              auto key2 = child_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(),
                                                   index->GetKeyAttrs());
              if (index_info->is_primary_key_) {
                continue;
              }
              index->DeleteEntry(key1, old_rid, exec_ctx_->GetTransaction());
              index->InsertEntry(key2, old_rid, exec_ctx_->GetTransaction());
            }
          } else {
            // 3 主键已存在, 但未被删除, 抛异常
            txn->SetTainted();
            throw ExecutionException("(1) unique key conflict");
          }
        }
        // 主键不存在
      }
    }
    if (skip_insert) {
      continue;  // 跳过本轮while
    }
    // 2.1 直接插入
    TupleMeta tuple_meta;
    tuple_meta.ts_ = txn->GetTransactionTempTs();  // 时间戳更新为 事务临时时间戳
    tuple_meta.is_deleted_ = false;
    auto key_rid = table_heap->InsertTuple(tuple_meta, child_tuple, exec_ctx_->GetLockManager(),
                                           exec_ctx_->GetTransaction(), plan_->GetTableOid());
    txn->AppendWriteSet(table_info->oid_, *key_rid);  // key_rid才是真正的rid
    // txn_mgr->UpdateUndoLink(*key_rid, UndoLink(), nullptr);
    cnt_++;

    // 2.2 插入所有索引
    for (const auto &index_info : catalog->GetTableIndexes(table_info->name_)) {
      auto index = index_info->index_.get();
      auto key =
          child_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(), index->GetKeyAttrs());
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
