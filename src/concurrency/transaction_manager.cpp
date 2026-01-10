//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// transaction_manager.cpp
//
// Identification: src/concurrency/transaction_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/transaction_manager.h"

#include <memory>
#include <mutex>  // NOLINT
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "catalog/catalog.h"
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/config.h"
#include "common/exception.h"
#include "common/macros.h"
#include "concurrency/transaction.h"
#include "execution/execution_common.h"
#include "storage/table/table_heap.h"
#include "storage/table/tuple.h"
#include "type/type_id.h"
#include "type/value.h"
#include "type/value_factory.h"

namespace bustub {

auto TransactionManager::Begin(IsolationLevel isolation_level) -> Transaction * {
  std::unique_lock<std::shared_mutex> l(txn_map_mutex_);
  auto txn_id = next_txn_id_++;
  auto txn = std::make_unique<Transaction>(txn_id, isolation_level);
  auto *txn_ref = txn.get();

  // 获取当前最新已提交事务的 提交时间戳, 作为本事务的 read_ts
  auto read_ts = last_commit_ts_.load();
  txn->read_ts_.store(read_ts);
  txn_map_.insert(std::make_pair(txn_id, std::move(txn)));

  // 将该事务的 read_ts 加入 watermark
  running_txns_.AddTxn(txn_ref->read_ts_);
  return txn_ref;
}

auto TransactionManager::VerifyTxn(Transaction *txn) -> bool { return true; }

auto TransactionManager::Commit(Transaction *txn) -> bool {
  std::unique_lock<std::mutex> commit_lck(commit_mutex_);
  // 只有 RUNNING 状态的事务才能提交
  if (txn->state_ != TransactionState::RUNNING) {
    throw Exception("txn not in running state");
  }
  // 如果是串行化隔离级别, 需要检验事务是否可提交
  if (txn->GetIsolationLevel() == IsolationLevel::SERIALIZABLE) {
    if (!VerifyTxn(txn)) {
      commit_lck.unlock();
      Abort(txn);
      return false;
    }
  }

  std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);

  // 提交时间戳唯一标识一个事务, 最新提交时间戳++, 并更新当前事务的提交时间戳
  last_commit_ts_++;
  txn->commit_ts_.store(last_commit_ts_.load());
  auto write_sets = txn->GetWriteSets();
  for (const auto &[table_oid, rid_set] : write_sets) {
    auto table_info = catalog_->GetTable(table_oid);
    auto table_heap = table_info->table_.get();
    for (const auto &rid : rid_set) {
      auto meta = table_heap->GetTupleMeta(rid);
      meta.ts_ = last_commit_ts_;
      table_heap->UpdateTupleMeta(meta, rid);
    }
  }

  // 设置事务状态为已提交
  txn->state_ = TransactionState::COMMITTED;
  // 更新 running_txns_ (watermark)
  running_txns_.UpdateCommitTs(txn->commit_ts_);
  running_txns_.RemoveTxn(txn->read_ts_);

  return true;
}

void TransactionManager::Abort(Transaction *txn) {
  if (txn->state_ != TransactionState::RUNNING && txn->state_ != TransactionState::TAINTED) {
    throw Exception("txn not in running / tainted state");
  }

  // TODO(fall2023): Implement the abort logic!

  std::unique_lock<std::shared_mutex> lck(txn_map_mutex_);
  txn->state_ = TransactionState::ABORTED;
  running_txns_.RemoveTxn(txn->read_ts_);
}

void TransactionManager::GarbageCollection() {
  // 1. 遍历所有tuple的版本链, 收集符合条件的UndoLink对应的事务id
  std::unordered_set<txn_id_t> needed_txns;  // 记录不用删的txns
  auto read_ts = running_txns_.GetWatermark();

  for (auto [page_id, version_info] : version_info_) {
    for (auto [slot, undo_link] : version_info->prev_link_) {
      // 获取tuple meta, 看看基tuple的(提交)时间戳是否小于等于read_ts, 如果满足, 则不用遍历版本链了
      auto table_info = catalog_->GetTable(page_id);
      auto table_heap = table_info->table_.get();
      auto meta = table_heap->GetTupleMeta(RID(page_id, slot));
      if (meta.ts_ <= read_ts) {
        continue;
      }

      if (!undo_link.IsValid()) {  // 链无效
        continue;
      }
      auto link = undo_link;
      while (link.IsValid()) {
        const auto log = GetUndoLog(link);
        if (log.ts_ <= read_ts) {
          needed_txns.insert(link.prev_txn_);
          break;  // 该条版本链遍历完毕
        }
        needed_txns.insert(link.prev_txn_);
        link = log.prev_version_;
      }
    }
  }
  // 2. 遍历所有事务, 不在txns的事务均删除
  std::vector<txn_id_t> to_delete;
  for (const auto &[txn_id, txn] : txn_map_) {
    if (needed_txns.count(txn_id) == 0U &&
        (txn->state_ == TransactionState::COMMITTED || txn->state_ == TransactionState::ABORTED)) {
      to_delete.push_back(txn_id);
    }
  }
  for (auto txn_id : to_delete) {
    txn_map_.erase(txn_id);
  }
}
}  // namespace bustub
