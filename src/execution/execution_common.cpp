//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2024-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/execution_common.h"

#include "catalog/catalog.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"

namespace bustub {

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool {
  const auto &key_a = entry_a.first;
  const auto &key_b = entry_b.first;
  for (size_t i = 0; i < order_bys_.size(); ++i) {
    // 没考虑无效的情况
    const auto type = (order_bys_[i].first == OrderByType::DESC) ? OrderByType::DESC : OrderByType::ASC;
    const auto &value_a = key_a[i];
    const auto &value_b = key_b[i];

    // 必须要考虑一方不存在的情况, 等于null表示不存在
    const bool a_null = value_a.IsNull();
    const bool b_null = value_b.IsNull();

    // 特殊处理, 一方或两方为NULL的情况
    if (a_null || b_null) {
      if (a_null && b_null) {
        continue;  // 都为null, 跳过
      }
      if (type == OrderByType::ASC) {
        if (!a_null && b_null) {
          return true;
        }
        if (a_null && !b_null) {
          return false;
        }
      } else {  // 默认和ASC
        if (a_null && !b_null) {
          return true;
        }
        if (!a_null && b_null) {
          return false;
        }
      }
      continue;
    }

    // 都非 NULL, 按值比较
    if (value_a.CompareLessThan(value_b) == CmpBool::CmpTrue) {
      return type == OrderByType::ASC;
    }
    if (value_a.CompareGreaterThan(value_b) == CmpBool::CmpTrue) {
      return type == OrderByType::DESC;
    }
  }
  return false;
}

auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
  std::vector<Value> values;
  values.reserve(order_bys.size());
  for (const auto &ob : order_bys) {
    values.push_back(ob.second->Evaluate(&tuple, schema));
  }
  return values;
}

/**
 * Above are all you need for P3.
 * You can ignore the remaining part of this file until P4.
 */

/**
 * @brief Reconstruct a tuple by applying the provided undo logs from the base tuple. All logs in the undo_logs are
 * applied regardless of the timestamp
 *
 * @param schema The schema of the base tuple and the returned tuple.
 * @param base_tuple The base tuple to start the reconstruction from.
 * @param base_meta The metadata of the base tuple.
 * @param undo_logs The list of undo logs to apply during the reconstruction, the front is applied first.
 * @return An optional tuple that represents the reconstructed tuple. If the tuple is deleted as the result, returns
 * std::nullopt.
 */
auto ReconstructTuple(const Schema *schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
  /* 获得tuple每列对应的谓词 */

  /* 遍历undoLogs数组, 边遍历边恢复tuple */
  /* 大循环遍历 undolog */
  /* 小循环 遍历tuple的每一列, 同时检查undolog的标记数组, 判断该列是否需要进行修改 */
  auto tuple = base_tuple;
  auto meta = base_meta;

  auto size = schema->GetColumnCount();
  for (const auto &undo_log : undo_logs) {  // 逆序读取 undo_logs
    meta.is_deleted_ = undo_log.is_deleted_;
    auto is_modified = undo_log.modified_fields_;
    auto new_schema = GetUndoLogSchema(*schema, undo_log);
    std::vector<Value> values{};  // 新tuple
    values.reserve(schema->GetColumnCount());

    // 遍历tuple 和 历史记录
    for (size_t i = 0, j = 0; i < size; ++i) {  // i 遍历原tuple, j遍历undo_log里的tuple
      if (is_modified[i] == false) {
        values.emplace_back(tuple.GetValue(schema, i));
        continue;
      }
      // 如果需要修改该列的值
      values.emplace_back(undo_log.tuple_.GetValue(&new_schema, j));
      j++;
    }  // 遍历完了构造成tuple
    tuple = Tuple(values, schema);
    tuple.SetRid(tuple.GetRid());
  }
  if (meta.is_deleted_ == true) {  // 文档要求基tuple不存在就直接返回 nullopt
    return std::nullopt;
  }
  return tuple;
}

/**
 * @brief Collects the undo logs sufficient to reconstruct the tuple w.r.t. the txn.
 *
 * @param rid The RID of the tuple.
 * @param base_meta The metadata of the base tuple.
 * @param base_tuple The base tuple.
 * @param undo_link The undo link to the latest undo log.
 * @param txn The transaction.
 * @param txn_mgr The transaction manager.
 * @return An optional vector of undo logs to pass to ReconstructTuple(). std::nullopt if the tuple did not exist at the
 * time.
 */
auto CollectUndoLogs(RID rid, const TupleMeta &base_meta, const Tuple &base_tuple, std::optional<UndoLink> undo_link,
                     Transaction *txn, TransactionManager *txn_mgr) -> std::optional<std::vector<UndoLog>> {
  auto read_ts = txn->GetReadTs();
  std::vector<UndoLog> undo_logs;
  auto base_ts = base_meta.ts_;
  /* 最特殊情况, 如果基tuple时间戳小于等于read_ts, 或者临时时间戳等于 事务id, 说明tuple不需要重建, 直接返回空数组 */
  if (base_ts <= read_ts || base_ts == txn->GetTransactionId()) {
    return undo_logs;
  }
  if (undo_link.has_value() && undo_link->IsValid()) {
    WalkUndoLogs(read_ts, undo_link.value(), undo_logs, txn_mgr);
  }
  /* 然后再判断1.时间戳 大于 read_ts 2.临时时间戳, 但是不等于 观测事务id的情况
  如果link 不存在或者无效, 说明没必要walk了
  如果link 有效, walk, walk完如果 logs 为空, 就返回 nullopt, 否则返回 undologs
  */
  if (undo_logs.empty()) {
    return std::nullopt;
  }
  return undo_logs;
}

/**
 * @brief Generates a new undo log as the transaction tries to modify this tuple at the first time.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param ts The timestamp of the base tuple.
 * @param prev_version The undo link to the latest undo log of this tuple.
 * @return The generated undo log.
 */
auto GenerateNewUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple, timestamp_t ts,
                        UndoLink prev_version) -> UndoLog {
  /*  */
  std::vector<Value> values;
  UndoLog undolog;
  undolog.ts_ = ts;
  undolog.prev_version_ = prev_version;
  undolog.is_deleted_ = false;

  auto size = schema->GetColumnCount();
  undolog.modified_fields_.resize(size, false);

  if (target_tuple == nullptr) {  // 1.删除
    std::fill(undolog.modified_fields_.begin(), undolog.modified_fields_.end(), true);
    undolog.tuple_ = *base_tuple;
    return undolog;
  }
  if (base_tuple == nullptr) {  // 2.插入
    undolog.is_deleted_ = true;
    return undolog;
  }

  std::vector<Column> columns;
  size_t cnt = 0;
  for (size_t i = 0; i < size; ++i) {
    if (!base_tuple->GetValue(schema, i).CompareExactlyEquals(target_tuple->GetValue(schema, i)))  // 如果不等, cnt++
    {
      cnt++;
    }
  }
  values.reserve(cnt);
  columns.reserve(cnt);
  for (size_t i = 0; i < size; ++i) {  // 遍历基tuple和新tuple找不同
    if (!base_tuple->GetValue(schema, i).CompareExactlyEquals(target_tuple->GetValue(schema, i))) {
      // 不同则把新tuple的Value塞入 values 并更新 modified 数组
      values.emplace_back(base_tuple->GetValue(schema, i));
      undolog.modified_fields_[i] = true;
      columns.emplace_back(schema->GetColumn(i));
    } else {
      undolog.modified_fields_[i] = false;
    }
  }
  Schema tuple_schema(columns);
  undolog.tuple_ = Tuple(values, &tuple_schema);
  return undolog;
}

/**
 * @brief Generate the updated undo log to replace the old one, whereas the tuple is already modified by this txn once.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param log The original undo log.
 * @return The updated undo log.
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
  UndoLog new_log;
  auto size = schema->GetColumnCount();
  new_log.modified_fields_.resize(size, false);

  std::vector<uint32_t> idxs;
  for (size_t i = 0; i < size; ++i) {
    if (log.modified_fields_[i] == true) {
      idxs.push_back(i);
    }
  }
  auto schema_1 = Schema::CopySchema(schema, idxs);  // 旧log里tuple的schema
  std::vector<Value> values;
  values.reserve(size);  // 因为不确定会有多大, 所以设这么大

  if (target_tuple == nullptr) {  // 1.删除
    std::fill(new_log.modified_fields_.begin(), new_log.modified_fields_.end(), true);
    for (size_t i = 0, j = 0; i < size; ++i) {
      if (log.modified_fields_[i] == false) {
        values.emplace_back(base_tuple->GetValue(schema, i));
        continue;
      }
      values.emplace_back(log.tuple_.GetValue(&schema_1, j));
      j++;
    }
    new_log.tuple_ = Tuple(values, schema);
    new_log.is_deleted_ = log.is_deleted_;
    new_log.ts_ = log.ts_;                      // 保留原 ts
    new_log.prev_version_ = log.prev_version_;  // 保留原版本链
    return new_log;
  }

  // 累积修改列：new = old || now_modified
  for (size_t i = 0, idx = 0; i < size; ++i) {
    const bool old_mod = log.modified_fields_[i];
    const bool now_mod = !target_tuple->GetValue(schema, i).CompareExactlyEquals(base_tuple->GetValue(schema, i));
    const bool new_mod = old_mod || now_mod;
    new_log.modified_fields_[i] = new_mod;
    if (!new_mod) {
      continue;
    }
    // 优先使用旧 undo log 中的值；若旧未修改则保存 base 的值
    if (old_mod) {
      values.emplace_back(log.tuple_.GetValue(&schema_1, idx));
      idx++;
    } else {
      values.emplace_back(base_tuple->GetValue(schema, i));
    }
  }

  // 遍历log的modified制作schema
  idxs.clear();
  for (size_t i = 0; i < size; ++i) {
    if (new_log.modified_fields_[i] == true) {
      idxs.push_back(i);
    }
  }
  auto schema_2 = Schema::CopySchema(schema, idxs);

  new_log.is_deleted_ = log.is_deleted_;
  new_log.ts_ = log.ts_;
  new_log.prev_version_ = log.prev_version_;
  new_log.tuple_ = Tuple(values, &schema_2);
  return new_log;
}

static auto FormatUndoLogTuple(const Schema &base_schema, const UndoLog &log) -> std::string {
  if (log.is_deleted_) {
    return "<del>";
  }
  auto undo_schema = GetUndoLogSchema(base_schema, log);
  std::vector<std::string> parts;
  parts.reserve(base_schema.GetColumnCount());
  size_t j = 0;
  for (uint32_t i = 0; i < base_schema.GetColumnCount(); i++) {
    if (!log.modified_fields_[i]) {
      parts.emplace_back("_");
      continue;
    }
    Value v = log.tuple_.GetValue(&undo_schema, j++);
    if (v.IsNull()) {
      parts.emplace_back("<NULL>");
      continue;
    }
    switch (base_schema.GetColumn(i).GetType()) {
      case TypeId::INTEGER:
        parts.emplace_back(fmt::format("{}", v.GetAs<int32_t>()));
        break;
      case TypeId::DECIMAL:
        parts.emplace_back(fmt::format("{:.6f}", v.GetAs<double>()));
        break;
      case TypeId::BOOLEAN:
        parts.emplace_back(v.GetAs<bool>() ? "true" : "false");
        break;
      default:
        parts.emplace_back(v.ToString());
        break;
    }
  }
  return fmt::format("({})", fmt::join(parts, ", "));
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  fmt::println(stderr, "debug_hook: {}", info);

  const Schema *base_schema = &table_info->schema_;

  for (auto it = table_heap->MakeIterator(); !it.IsEnd(); ++it) {
    auto [meta, tuple] = it.GetTuple();
    RID rid = it.GetRID();

    const bool deleted = meta.is_deleted_;
    const bool is_txn_ts = meta.ts_ >= TXN_START_ID;
    const std::string ts_str =
        is_txn_ts ? fmt::format("txn{}", (meta.ts_ ^ TXN_START_ID)) : fmt::format("{}", meta.ts_);

    if (deleted) {
      fmt::println(stderr, "RID={}/{} ts={} <del marker> tuple={}", rid.GetPageId(), rid.GetSlotNum(), ts_str,
                   tuple.ToString(base_schema));
    } else {
      fmt::println(stderr, "RID={}/{} ts={} tuple={}", rid.GetPageId(), rid.GetSlotNum(), ts_str,
                   tuple.ToString(base_schema));
    }

    auto opt_link = txn_mgr->GetUndoLink(rid);
    if (!opt_link.has_value() || !opt_link->IsValid()) {
      continue;
    }

    // 手工沿链遍历，保留每条记录的 (txn_id, log_idx)
    auto link = opt_link.value();
    while (link.IsValid()) {
      {
        std::shared_lock<std::shared_mutex> l(txn_mgr->txn_map_mutex_);
        if (txn_mgr->txn_map_.count(link.prev_txn_) == 0) {
          break;
        }
      }
      auto log = txn_mgr->GetUndoLog(link);
      auto owner_hr = (link.prev_txn_ ^ TXN_START_ID);
      if (log.is_deleted_) {
        fmt::println(stderr, "  txn{}@{} <del> ts={}", owner_hr, link.prev_log_idx_, log.ts_);
      } else {
        fmt::println(stderr, "  txn{}@{} {} ts={}", owner_hr, link.prev_log_idx_, FormatUndoLogTuple(*base_schema, log),
                     log.ts_);
      }
      link = log.prev_version_;
    }
  }
}

// We recommend implementing this function as traversing the table heap and print the version chain. An example output
// of our reference solution:
//
// debug_hook: before verify scan
// RID=0/0 ts=txn8 tuple=(1, <NULL>, <NULL>)
//   txn8@0 (2, _, _) ts=1
// RID=0/1 ts=3 tuple=(3, <NULL>, <NULL>)
//   txn5@0 <del> ts=2
//   txn3@0 (4, <NULL>, <NULL>) ts=1
// RID=0/2 ts=4 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
//   txn7@0 (5, <NULL>, <NULL>) ts=3
// RID=0/3 ts=txn6 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
//   txn6@0 (6, <NULL>, <NULL>) ts=2
//   txn3@1 (7, _, _) ts=1

/* 调用该函数的前提是 基tuple的ts > read_ts, 或者是临时时间戳 且不等于 事务id, 必须去undo链找可用于重建的undologs */
/* 有可能找不到, 那么返回空数组, 意思是无法重建tuple */
/* 需要遍历undo_link 把时间戳大于等于 read_ts 的都收集进 undologs, 如果遍历到目标log的is_dirty = true, 就返回 true */
void WalkUndoLogs(const timestamp_t &read_ts, const UndoLink &undo_link, std::vector<UndoLog> &undo_logs,
                  TransactionManager *txn_mgr) {
  if (!undo_link.IsValid()) {  // 链无效
    return;
  }
  auto link = undo_link;
  while (link.IsValid()) {
    const auto log = txn_mgr->GetUndoLog(link);
    if (log.ts_ <= read_ts) {
      undo_logs.push_back(log);
      return;  // 成功找到
    }
    undo_logs.push_back(log);
    link = log.prev_version_;
  }
  // 遍历结束, 发现无法重建tuple
  undo_logs.clear();
}

/* 遍历base_schema和 is_modified数组 构建新的 schema */
auto GetUndoLogSchema(const Schema &base_schema, const UndoLog &undo_log) -> Schema {
  auto size = base_schema.GetColumnCount();
  auto is_modified = undo_log.modified_fields_;
  int cnt = 0;
  for (size_t i = 0; i < size; ++i) {
    if (is_modified[i] == true) {
      cnt++;
    }
  }

  std::vector<Column> columns;
  columns.reserve(cnt);
  for (size_t i = 0; i < size; ++i) {
    if (is_modified[i] == true) {
      columns.push_back(base_schema.GetColumn(i));
    }
  }
  return Schema(columns);
}

void IsWriteWriteConflict(Transaction *txn, TupleMeta meta) {
  if (meta.ts_ > TXN_START_ID) {
    if (meta.ts_ != txn->GetTransactionId()) {
      txn->SetTainted();
      throw ExecutionException("");
    }
  } else {
    if (txn->GetReadTs() < meta.ts_) {
      txn->SetTainted();
      throw ExecutionException("");
    }
  }
}

}  // namespace bustub
