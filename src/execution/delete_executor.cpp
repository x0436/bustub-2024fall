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
  // auto table_heap = table_info->table_.get();
  auto schema = child_executor_->GetOutputSchema();

  Tuple child_tuple;
  RID child_rid;
  cnt_ = 0;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    // auto tuple_meta = table_heap->GetTupleMeta(child_rid);
    // IsWriteWriteConflict(txn, tuple_meta);  // 在修改前检查是否有写写冲突
    // // 没有写写冲突
    // auto undolink_opt = txn_mgr->GetUndoLink(child_rid);
    // UndoLink undolink = undolink_opt.has_value() ? undolink_opt.value() : UndoLink();
    // auto undolog_opt = txn_mgr->GetUndoLogOptional(undolink);
    // bool has_log = undolog_opt.has_value();
    // auto ts = tuple_meta.ts_;
    // UndoLog undolog;
    // bool is_current_txn_insert =
    //     (undolink.prev_txn_ == INVALID_TXN_ID) && (tuple_meta.ts_ == txn->GetTransactionTempTs());

    // if (!is_current_txn_insert) {
    //   if (!has_log || undolink.prev_txn_ != txn->GetTransactionId()) {
    //     // 第一次 delete，追加 undo log
    //     undolog = GenerateNewUndoLog(&schema, &child_tuple, nullptr, ts, undolink);
    //     auto new_link = txn->AppendUndoLog(undolog);
    //     txn_mgr->UpdateUndoLink(child_rid, new_link);
    //   } else {
    //     // 已有 undo log，合并并覆盖
    //     undolog = GenerateUpdatedUndoLog(&schema, &child_tuple, nullptr, undolog_opt.value());
    //     txn->ModifyUndoLog(undolink.prev_log_idx_, undolog);
    //   }
    // }
    // txn->AppendWriteSet(table_info->oid_, child_rid);

    // tuple_meta.ts_ = txn->GetTransactionTempTs();
    // tuple_meta.is_deleted_ = true;
    // table_heap->UpdateTupleMeta(tuple_meta, child_rid);
    Modify(child_rid, nullptr, exec_ctx_, table_info.get());
    cnt_++;
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
