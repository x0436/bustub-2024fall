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
  
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    // 删除
    auto tuple_meta = table_heap->GetTupleMeta(child_rid);
    tuple_meta.is_deleted_ = true;
    table_heap->UpdateTupleMeta(tuple_meta, child_rid);

    // 插入
    const Schema *schema = &table_info->schema_;
    auto child_schema = child_executor_->GetOutputSchema();
    std::vector<Value> vals;
    vals.reserve(schema->GetColumnCount());
    for (uint32_t i = 0; i < schema->GetColumnCount(); ++i)  // 借助ai
    {
      auto &expr = plan_->target_expressions_[i];
      vals.push_back(expr->Evaluate(&child_tuple, child_schema));
    }
    Tuple new_tuple(vals, schema);
    TupleMeta new_tuple_meta;
    new_tuple_meta.ts_ = 0;
    new_tuple_meta.is_deleted_ = false;
    auto key_rid = table_heap->InsertTuple(new_tuple_meta, new_tuple, exec_ctx_->GetLockManager(),
                                           exec_ctx_->GetTransaction(), plan_->GetTableOid());

    cnt_++;

    // 更新该表相关的所有索引
    for (auto index_info : catalog->GetTableIndexes(table_info->name_)) {
      auto index = index_info->index_.get();
      std::vector<Value> key_values{};
      key_values.reserve(index->GetIndexColumnCount());

      auto key1 =
          child_tuple.KeyFromTuple(child_schema, *index->GetKeySchema(), index->GetKeyAttrs());
      index->DeleteEntry(key1, child_rid, exec_ctx_->GetTransaction());  // 删除

      auto key2 =
          new_tuple.KeyFromTuple(*schema, *index->GetKeySchema(), index->GetKeyAttrs()); // 感觉子算子的schema和表的schema一样
      index->InsertEntry(key2, *key_rid, exec_ctx_->GetTransaction());   // 插入
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
