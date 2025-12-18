//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.h
//
// Identification: src/include/execution/executors/hash_join_executor.h
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <utility>

#include "common/util/hash_util.h"
#include "execution/executor_context.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/hash_join_plan.h"
#include "storage/table/tuple.h"
#include "type/value_factory.h"

namespace bustub {

struct HashJoinKey {
  std::vector<Value> join_keys_;
  auto operator==(const HashJoinKey &other) const -> bool {
    for (uint32_t i = 0; i < other.join_keys_.size(); i++) {
      if (join_keys_[i].CompareEquals(other.join_keys_[i]) != CmpBool::CmpTrue) {
        return false;
      }
    }
    return true;
  }
};
struct HashJoinValue {
  std::vector<Tuple> tuples_;
  std::vector<bool> matched_;
};

}  // namespace bustub
namespace std {
template <>
struct hash<bustub::HashJoinKey> {
  size_t operator()(const bustub::HashJoinKey &k) const {
    size_t h = 0;
    for (const auto &v : k.join_keys_) {
      if (!v.IsNull()) {
        h = bustub::HashUtil::CombineHashes(h, bustub::HashUtil::HashValue(&v));
      }
    }
    return h;
  }
};
}  // namespace std
namespace bustub {
class HashJoinHashTable {
 public:
  // HashJoinHashTable(const std::vector<AbstractExpressionRef> &key_exprs) : key_exprs_{key_exprs} {}

  void InsertCombine(const HashJoinKey &join_key, const Tuple &tuple) {
    auto &val = ht_[join_key];
    val.tuples_.push_back(tuple);
    val.matched_.push_back(false);
  }
  /**
   * Clear the hash table
   */
  void Clear() { ht_.clear(); }

  bool FindMatches(const HashJoinKey &key, std::vector<Tuple> &out, std::vector<size_t> *indices = nullptr) const {
    auto it = ht_.find(key);
    if (it == ht_.end()) {  // 没找到
      out.clear();
      if (indices) indices->clear();
      return false;
    }
    out = it->second.tuples_;
    if (indices) {  // indices是该组tuple的下标
      indices->clear();
      indices->reserve(it->second.tuples_.size());
      for (size_t i = 0; i < it->second.tuples_.size(); ++i) {
        indices->push_back(i);
      }
    }
    return true;
  }

  // 标记：某键下的某个候选被匹配
  void MarkMatched(const HashJoinKey &key, size_t idx) {
    auto it = ht_.find(key);
    if (it != ht_.end() && idx < it->second.matched_.size()) {
      it->second.matched_[idx] = true;
    }
  }

  // 遍历所有未匹配的外表行，返回它们对应的左侧 tuple（外表）及其 schema 由执行器使用
  template <typename Fn>
  void ForEachUnmatched(Fn &&fn) const {
    for (const auto &kv : ht_) {
      const auto &val = kv.second;
      for (size_t i = 0; i < val.tuples_.size(); ++i) {
        if (!val.matched_[i]) {
          fn(val.tuples_[i]);
        }
      }
    }
  }

  class Iterator {
   public:
    /** Creates an iterator for the aggregate map. */
    explicit Iterator(std::unordered_map<HashJoinKey, HashJoinValue>::const_iterator iter) : iter_{iter} {}

    /** @return The key of the iterator */
    auto Key() -> const HashJoinKey & { return iter_->first; }

    /** @return The value of the iterator */
    auto Val() -> const HashJoinValue & { return iter_->second; }

    /** @return The iterator before it is incremented */
    auto operator++() -> Iterator & {
      ++iter_;
      return *this;
    }

    /** @return `true` if both iterators are identical */
    auto operator==(const Iterator &other) -> bool { return this->iter_ == other.iter_; }

    /** @return `true` if both iterators are different */
    auto operator!=(const Iterator &other) -> bool { return this->iter_ != other.iter_; }

   private:
    /** Aggregates map */
    std::unordered_map<HashJoinKey, HashJoinValue>::const_iterator iter_;
  };
  /** @return Iterator to the start of the hash table */
  auto Begin() -> Iterator { return Iterator{ht_.cbegin()}; }

  /** @return Iterator to the end of the hash table */
  auto End() -> Iterator { return Iterator{ht_.cend()}; }

 private:
  std::unordered_map<HashJoinKey, HashJoinValue> ht_{};
  // const std::vector<AbstractExpressionRef> &key_exprs_;  // 感觉可以删掉
};

/**
 * HashJoinExecutor executes a nested-loop JOIN on two tables.
 */
class HashJoinExecutor : public AbstractExecutor {
 public:
  /**
   * Construct a new HashJoinExecutor instance.
   * @param exec_ctx The executor context
   * @param plan The HashJoin join plan to be executed
   * @param left_child The child executor that produces tuples for the left side of join
   * @param right_child The child executor that produces tuples for the right side of join
   */
  HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                   std::unique_ptr<AbstractExecutor> &&left_child, std::unique_ptr<AbstractExecutor> &&right_child);

  /** Initialize the join */
  void Init() override;

  /**
   * Yield the next tuple from the join.
   * @param[out] tuple The next tuple produced by the join.
   * @param[out] rid The next tuple RID, not used by hash join.
   * @return `true` if a tuple was produced, `false` if there are no more tuples.
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the join */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); };

 private:
  /** The HashJoin plan node to be executed. */
  const HashJoinPlanNode *plan_;
  std::unique_ptr<AbstractExecutor> left_child_;
  std::unique_ptr<AbstractExecutor> right_child_;

  size_t left_idx_;
  bool is_inner_;
  bool left_matched_{false};

  HashJoinHashTable hjht_;
  HashJoinHashTable::Iterator hjht_iterator_;

  std::vector<Tuple> left_tuples_;
  Tuple right_tuple_;

  HashJoinKey current_match_key_;
  std::vector<size_t> current_match_indices_;
  
  auto Match(const Tuple &left_tuple, const Tuple &right_tuple) -> bool;
  Tuple Join(const Tuple &left_tuple, const Tuple &right_tuple);
};

}  // namespace bustub
