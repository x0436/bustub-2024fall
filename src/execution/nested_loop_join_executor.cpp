//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"

namespace bustub {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_executor)),
      right_child_(std::move(right_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
  if (plan->GetJoinType() == JoinType::LEFT) {
    is_inner_ = false;
  } else {
    is_inner_ = true;
  }
}

void NestedLoopJoinExecutor::Init() {
  left_child_->Init();
  right_child_->Init();
  Tuple tuple;
  RID rid;
  while (right_child_->Next(&tuple, &rid)) {
    right_table_.push_back(tuple);
  }
  right_idx_ = 0;
}

auto NestedLoopJoinExecutor::Match(const Tuple &left_tuple, const Tuple &right_tuple) -> bool {
  // 若谓词为空，视为全匹配（笛卡尔积）
  if (plan_->Predicate() == nullptr) {
    return true;
  }
  auto val = plan_->Predicate()->EvaluateJoin(&left_tuple, left_child_->GetOutputSchema(), &right_tuple,
                                              right_child_->GetOutputSchema());
  return !val.IsNull() && val.GetAs<bool>();
}

Tuple NestedLoopJoinExecutor::Join(const Tuple &left_tuple, const Tuple &right_tuple) {
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

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    // 当右表读完时, 右表索引回到0, 说明要读下一个left tuple
    if (right_idx_ == 0) {
      RID left_rid;
      if (!left_child_->Next(&left_tuple_, &left_rid)) {
        return false;
      }
      left_matched_ = false;
      // 确保每个新的 left tuple 都对 right 子算子调用 Init（满足 nlj_init_check）
      if (right_child_) {
        right_child_->Init();
      }
    }
    while (right_idx_ != right_table_.size()) {
      if (Match(left_tuple_, right_table_[right_idx_])) {
        left_matched_ = true;
        *tuple = Join(left_tuple_, right_table_[right_idx_]);
        *rid = RID();
        ++right_idx_;
        return true;
      }
      ++right_idx_;
    }

    // 扫描完右表，重置索引以便读取下一个 left tuple
    right_idx_ = 0;

    // 右表扫描完毕, 如果为 Left join 且 在右表无任何匹配, 输出 left + 右侧 NULL
    if (!is_inner_ && !left_matched_) {
      // 构造右侧全 NULL 的 tuple 部分
      std::vector<Value> values{};
      const Schema &left_schema = left_child_->GetOutputSchema();
      const Schema &right_schema = right_child_->GetOutputSchema();
      values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());
      // 左侧字段
      for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
        values.push_back(left_tuple_.GetValue(&left_schema, i));
      }
      // 右侧字段都填 NULL（使用相应列类型构造 NULL Value）
      for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
        values.push_back(ValueFactory::GetNullValueByType(right_schema.GetColumn(i).GetType()));
      }
      *tuple = Tuple(values, &GetOutputSchema());
      *rid = RID();
      
      left_matched_ = false;
      return true;
    }
  }
  return false;
}

}  // namespace bustub
