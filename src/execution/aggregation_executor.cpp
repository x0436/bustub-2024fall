//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <vector>

#include "execution/executors/aggregation_executor.h"

namespace bustub {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      aht_(plan_->aggregates_, plan_->agg_types_),
      aht_iterator_(aht_.Begin()) {}

void AggregationExecutor::Init() {
  /*
                循环接收下层算子传来的tuple, 提取出 group by字段 和 aggregate字段
        比如 `SELECT min(t.z), max(t.z), sum(t.z) FROM t GROUP BY t.x, t.y;`
        的group by字段 (AggregateKey) 为 {t.x, t.y}, aggregate字段 (AggregateValue) 为 {t.z, t.z, t.z}
        调用 `InsertCombine()` 将 group by 和 aggregate 的映射关系存入 `SimpleAggregationHashTable`
        若已有记录, 则根据聚合规则 (取 max/min，求 sum 等) 更新 aggregate字段对应的数据
   */
  child_executor_->Init();

  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    std::vector<Value> group_bys;
    std::vector<Value> aggregates;
    for (const auto &expr : plan_->GetGroupBys()) {
      group_bys.push_back(expr->Evaluate(&child_tuple, child_executor_->GetOutputSchema()));
    }
    for (const auto &expr : plan_->GetAggregates()) {
      aggregates.push_back(expr->Evaluate(&child_tuple, child_executor_->GetOutputSchema()));
    }
    AggregateKey aggregate_key{group_bys};
    AggregateValue aggregate_value{aggregates};
    aht_.InsertCombine(aggregate_key, aggregate_value);
  }
  // 如果没有 group by 且哈希表为空
  if (plan_->GetGroupBys().empty() && aht_.Begin() == aht_.End()) {
    AggregateKey empty_key{std::vector<Value>{}};
    aht_.InsertCombine(empty_key, aht_.GenerateInitialAggregateValue());
  }
  aht_iterator_ = aht_.Begin();
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (aht_iterator_ != aht_.End()) {
    const auto &agg_key = aht_iterator_.Key();
    const auto &agg_val = aht_iterator_.Val();

    std::vector<Value> values;
    values.reserve(agg_key.group_bys_.size() + agg_val.aggregates_.size());
    values.insert(values.end(), agg_key.group_bys_.begin(), agg_key.group_bys_.end());
    values.insert(values.end(), agg_val.aggregates_.begin(), agg_val.aggregates_.end());

    *tuple = Tuple(values, &GetOutputSchema());

    if (rid != nullptr) {
      *rid = RID();
    }

    ++aht_iterator_;
    return true;
  }
  return false;
}

auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
