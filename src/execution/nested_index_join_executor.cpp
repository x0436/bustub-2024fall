//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"

namespace bustub {

NestIndexJoinExecutor::NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), left_child_(std::move(child_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Spring: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }

  plan->GetJoinType() == JoinType::LEFT ? is_inner_ = false : is_inner_ = true;
}

auto NestIndexJoinExecutor::Match(const Tuple &left_tuple, const Tuple &right_tuple) -> bool {
  auto val = plan_->KeyPredicate()->Evaluate(&left_tuple, left_child_->GetOutputSchema());
  const auto &key_attrs = index_info_->index_->GetKeyAttrs();
  Value right_key_val = right_tuple.GetValue(&plan_->InnerTableSchema(), key_attrs[0]);
  return (right_key_val.CompareEquals(val) == CmpBool::CmpTrue);
}

auto NestIndexJoinExecutor::Join(const Tuple &left_tuple, const Tuple &right_tuple) -> Tuple {
  std::vector<Value> values{};
  const Schema &left_schema = left_child_->GetOutputSchema();
  const Schema &right_schema = table_info_->schema_;
  values.reserve(left_schema.GetColumnCount() + right_schema.GetColumnCount());

  for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
    values.push_back(left_tuple.GetValue(&left_schema, i));
  }
  for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
    values.push_back(right_tuple.GetValue(&right_schema, i));
  }

  return {Tuple(values, &GetOutputSchema())};
}

void NestIndexJoinExecutor::Init() {
  auto catalog = exec_ctx_->GetCatalog();
  index_info_ = catalog->GetIndex(plan_->GetIndexOid()).get();
  table_info_ = catalog->GetTable(plan_->inner_table_oid_).get();
  tree_ = dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get());
  iterator_ = std::make_unique<BPlusTreeIndexIteratorForTwoIntegerColumn>(tree_->GetBeginIterator());
  left_child_->Init();
}

auto NestIndexJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    // 当右表读完时, 右表索引回到0, 说明要读下一个left tuple
    if (*iterator_ == tree_->GetBeginIterator()) {
      RID left_rid;
      if (!left_child_->Next(&left_tuple_, &left_rid)) {
        return false;
      }
      left_matched_ = false;
    }
    while (!iterator_->IsEnd()) {
      auto right_rid = (**iterator_).second;
      auto pair = table_info_->table_->GetTuple(right_rid);
      auto right_tuple = pair.second;
      if (Match(left_tuple_, right_tuple)) {
        left_matched_ = true;
        *tuple = Join(left_tuple_, right_tuple);
        *rid = RID();
        ++(*iterator_);
        return true;
      }
      ++(*iterator_);
    }

    // 扫描完右表，重置迭代器以便读取下一个 left tuple
    *iterator_ = tree_->GetBeginIterator();

    // Left join 且 在右表无任何匹配, 输出 left + 右侧 NULL
    if (!is_inner_ && !left_matched_) {
      // 构造右侧全 NULL 的 tuple 部分
      std::vector<Value> values{};
      const Schema &left_schema = left_child_->GetOutputSchema();
      const Schema &right_schema = table_info_->schema_;
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
