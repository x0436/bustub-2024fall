//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"

namespace bustub {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)),
      hjht_(plan_->LeftJoinKeyExpressions()),
      hjht_iterator_(hjht_.Begin()) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for Fall 2024: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
  if (plan->GetJoinType() == JoinType::LEFT) {
    is_inner_ = false;
  } else {
    is_inner_ = true;
  }
}

void HashJoinExecutor::Init() {
  left_child_->Init();
  right_child_->Init();
  Tuple tuple;
  RID rid;
  while (left_child_->Next(&tuple, &rid)) {
    std::vector<Value> join_keys{};
    join_keys.reserve(plan_->LeftJoinKeyExpressions().size());
    for (const auto &expr : plan_->LeftJoinKeyExpressions()) {
      join_keys.push_back(expr->Evaluate(&tuple, left_child_->GetOutputSchema()));
    }
    hjht_.InsertCombine(HashJoinKey{join_keys}, tuple);
  }
  hjht_iterator_ = hjht_.Begin();
  left_idx_ = 0;
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  /*
    遍历 右表, 每取出一个tuple, 在 hash表中查找是否有匹配, 如果有, 将对应vector 取出来, 一个个连接, 每次输出一个
    如果没有并且是左连接 需要拼接NULL, 怎么操作我忘了
  */
  // 1. 如果左tuples没有匹配完
  if (!left_tuples_.empty() && left_idx_ < left_tuples_.size()) {
    *tuple = Join(left_tuples_[left_idx_], right_tuple_);
    hjht_.MarkMatched(current_match_key_, current_match_indices_[left_idx_]);
    left_idx_++;
    *rid = RID();
    return true;
  }
  // 2. 读取下一个右侧tuple
  RID right_rid;
  while (right_child_->Next(&right_tuple_, &right_rid)) {
    std::vector<Value> right_join_keys;
    right_join_keys.reserve(plan_->RightJoinKeyExpressions().size());
    for (const auto &expr : plan_->RightJoinKeyExpressions()) {
      right_join_keys.push_back(expr->Evaluate(&right_tuple_, right_child_->GetOutputSchema()));
    }
    current_match_indices_.clear();
    HashJoinKey key{right_join_keys};
    if (!hjht_.FindMatches(key, left_tuples_, &current_match_indices_)) {
      continue;  // 如果该右tuple没有在哈希表中找到匹配, 读下一个右tuple
    }
    current_match_key_ = key;
    // 初次命中
    left_idx_ = 0;
    *tuple = Join(left_tuples_[left_idx_], right_tuple_);
    hjht_.MarkMatched(current_match_key_, current_match_indices_[left_idx_]);
    left_idx_++;
    *rid = RID();
    return true;
  }
  // 3.右表遍历完了, 左表中存在未匹配的tuple, 只有左连接时需要处理这种情况
  if (is_inner_ == false) {
    while (hjht_iterator_ != hjht_.End()) {
      const auto &val = hjht_iterator_.Val();
      for (size_t i = 0; i < val.tuples_.size(); ++i) {
        if (val.matched_[i] == false) {  // 没有匹配过
          const Schema &ls = left_child_->GetOutputSchema();
          const Schema &rs = right_child_->GetOutputSchema();
          std::vector<Value> out;
          out.reserve(ls.GetColumnCount() + rs.GetColumnCount());
          for (uint32_t c = 0; c < ls.GetColumnCount(); ++c) {
            out.push_back(val.tuples_[i].GetValue(&ls, c));
          }
          for (uint32_t c = 0; c < rs.GetColumnCount(); ++c) {
            out.push_back(ValueFactory::GetNullValueByType(rs.GetColumn(c).GetType()));
          }
          *tuple = Tuple(out, &GetOutputSchema());
          *rid = RID();
          // 标记避免重复输出
          hjht_.MarkMatched(hjht_iterator_.Key(), i);
          return true;
        }
      }
      ++hjht_iterator_;
    }
  }
  return false;
}

Tuple HashJoinExecutor::Join(const Tuple &left_tuple, const Tuple &right_tuple) {
  std::vector<Value> values{};
  const Schema &left_schema = left_child_->GetOutputSchema();
  const Schema &right_schema = right_child_->GetOutputSchema();
  values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());

  for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
    values.push_back(left_tuple.GetValue(&left_schema, i));
  }
  for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
    values.push_back(right_tuple.GetValue(&right_schema, i));
  }

  return Tuple(values, &GetOutputSchema());
}

}  // namespace bustub
