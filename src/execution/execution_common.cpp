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
  if (undo_logs.empty()) {
    return base_tuple;
  }
  /* 获得tuple每列对应的谓词 */

  /* 遍历undoLogs数组, 边遍历边恢复tuple */
  /* 大循环遍历 undolog */
  /* 小循环 遍历tuple的每一列, 同时检查undolog的标记数组, 判断该列是否需要进行修改 */
  auto tuple = base_tuple;
  auto meta = base_meta;
  auto size = schema->GetColumnCount();
  for (const auto &undo_log : undo_logs) {  // 逆序读取 undo_logs
    if (undo_log.is_deleted_ == true) {
      meta.is_deleted_ = true;
    }

    auto is_modified = undo_log.modified_fields_;

    std::vector<Value> values{};  // 新tuple
    values.reserve(schema->GetColumnCount());

    // 遍历tuple 和 历史记录
    for (int i = 0, j = 0; i < size; ++i) {  // i 遍历原tuple, j遍历undo_log里的tuple
      if (is_modified[i] == false) {
        values.emplace_back(tuple.GetValue(schema, i));
        continue;
      }
      /* tuple[i] =  */ /* undo_log.tuple_[idx++] */
      // 如果需要修改该列的值
      auto expected = schema->GetColumn(i).GetType();
      values.emplace_back(undo_log.tuple_.GetValue(schema, j));
      j++;
    }  // 遍历完了构造成tuple
    tuple = Tuple(values, schema);
    tuple.SetRid(tuple.GetRid());
  }
  // 如果tuple被删了
  if (base_meta.is_deleted_ == true) {
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
  /* 获得事务的时间戳 read_ts, 创建undologs数组 */
  auto read_ts = txn->GetReadTs();
  std::vector<UndoLog> undo_logs;
  /*
  获得基tuple 的时间戳, 做对比, 判断要不要遍历恢复链
  1. 不是临时时间戳
  1.1.undolog的时间戳 <= read_ts, 不用遍历, 将undoLog直接塞进数组里返回
  1.2.时间戳 > read_ts, 需要遍历版本链

  2. 是临时时间戳
  2.1 临时时间戳=事务id 读自己修改过的tuple // 如何确定
  2.2 遍历版本链
  */
  auto base_ts = base_meta.ts_;
  if (base_ts < TXN_START_ID) {
    if (base_ts > read_ts) {
      auto is_delete = WalkUndoLogs(read_ts, undo_link.value(), undo_logs, txn_mgr);
      if (is_delete) {
        return std::nullopt;
      }
    }
  } else {  // 临时时间戳
    if (read_ts != txn->GetTransactionId()) {
      auto is_delete = WalkUndoLogs(read_ts, undo_link.value(), undo_logs, txn_mgr);
      if (is_delete) {
        return std::nullopt;
      }
    }
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
  UNIMPLEMENTED("not implemented");
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
  UNIMPLEMENTED("not implemented");
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  // always use stderr for printing logs...
  fmt::println(stderr, "debug_hook: {}", info);

  fmt::println(
      stderr,
      "You see this line of text because you have not implemented `TxnMgrDbg`. You should do this once you have "
      "finished task 2. Implementing this helper function will save you a lot of time for debugging in later tasks.");

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
}

/* 需要遍历undo_link 把时间戳大于等于 read_ts 的都收集进 undologs, 如果遍历到目标log的is_dirty = true, 就返回 true */
auto WalkUndoLogs(const timestamp_t &read_ts, const UndoLink &undo_link, std::vector<UndoLog> &undo_logs,
                  TransactionManager *txn_mgr) -> bool {
  /* 先用 undo_link获得第一个undolog, 加入undo_logs, 循环, 直到 undologs 的时间戳 <= read_ts,
  如果此时 undo_log 的is_delete 为true, 返回true */
  auto cur_link = undo_link;
  auto cur_log = txn_mgr->GetUndoLog(cur_link);
  auto cur_ts = cur_log.ts_;

  while (cur_link.IsValid() && read_ts < cur_ts) {
    cur_link = cur_log.prev_version_;
    auto cur_log = txn_mgr->GetUndoLog(cur_link);
    auto cur_ts = cur_log.ts_;
  }
  return cur_log.is_deleted_ ? true : false;
}

}  // namespace bustub
