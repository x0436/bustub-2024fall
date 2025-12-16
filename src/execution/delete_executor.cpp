//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/delete_executor.h"

namespace bustub {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  child_executor_->Init();
  auto catalog = exec_ctx_->GetCatalog();
  auto table_info = catalog->GetTable(plan_->GetTableOid());
  auto table_heap = table_info->table_.get();
  Tuple child_tuple;
  RID child_rid;
  cnt_ = 0;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    auto tuple_meta = table_heap->GetTupleMeta(child_rid);
    tuple_meta.is_deleted_ = true;
    table_heap->UpdateTupleMeta(tuple_meta, child_rid);
    cnt_++;

    // 更新该表相关的所有索引
    for (auto index_info : catalog->GetTableIndexes(table_info->name_)) {
      auto index = index_info->index_.get();
      std::vector<Value> key_values{};
      key_values.reserve(index->GetIndexColumnCount());

      auto key =
          child_tuple.KeyFromTuple(child_executor_->GetOutputSchema(), *index->GetKeySchema(), index->GetKeyAttrs());
      index->DeleteEntry(key, child_rid, exec_ctx_->GetTransaction());
    }
  }
}

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
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
