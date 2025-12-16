#include <algorithm>
#include <memory>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/logic_expression.h"
namespace bustub {

// 出参left_cols 表示在左表的 keys
static bool CollectEqualities(const AbstractExpressionRef &expr, std::vector<std::pair<uint32_t, uint32_t>> &left_cols,
                              std::vector<std::pair<uint32_t, uint32_t>> &right_cols) {
  // 1. 如果是比较表达式
  if (auto cmp = dynamic_cast<ComparisonExpression *>(expr.get())) {
    // 只处理等号比较
    if (cmp->comp_type_ != ComparisonType::Equal) {
      return false;
    }
    auto left = cmp->GetChildAt(0);  // 此left指在表达式左侧, 不是左表的意思
    auto right = cmp->GetChildAt(1);
    // 转换成列值表达式
    auto left_col = dynamic_cast<ColumnValueExpression *>(left.get());
    auto right_col = dynamic_cast<ColumnValueExpression *>(right.get());
    if (left_col && right_col) {
      // 根据列值表达式的tuple_idx_, 判断应该放左cols还是放右cols
      if (!left_col->GetTupleIdx()) {
        left_cols.emplace_back(left_col->GetTupleIdx(), left_col->GetColIdx());
      } else {
        right_cols.emplace_back(left_col->GetTupleIdx(), left_col->GetColIdx());
      }
      if (!right_col->GetTupleIdx()) {
        left_cols.emplace_back(right_col->GetTupleIdx(), right_col->GetColIdx());
      } else {
        right_cols.emplace_back(right_col->GetTupleIdx(), right_col->GetColIdx());
      }
      return true;
    }
    return false;
  }
  // 2. 如果是逻辑表达式
  if (auto logic = dynamic_cast<LogicExpression *>(expr.get())) {
    // 只处理AND
    if (logic->logic_type_ != LogicType::And) {
      return false;
    }
    if (!CollectEqualities(logic->GetChildAt(0), left_cols, right_cols)) {
      return false;
    }
    if (!CollectEqualities(logic->GetChildAt(1), left_cols, right_cols)) {
      return false;
    }
    return true;
  }
  return false;
}

auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement NestedLoopJoin -> HashJoin optimizer rule
  // Note for 2023 Fall: You should support join keys of any number of conjunction of equi-conditions:
  // E.g. <column expr> = <column expr> AND <column expr> = <column expr> AND ...
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));
  if (optimized_plan->GetType() == PlanType::NestedLoopJoin) {
    const auto &nested_loop_join_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);
    if (nested_loop_join_plan.Predicate()) {
      std::vector<std::pair<uint32_t, uint32_t>> left_cols;
      std::vector<std::pair<uint32_t, uint32_t>> right_cols;

      if (CollectEqualities(nested_loop_join_plan.Predicate(), left_cols, right_cols)) {
        std::vector<AbstractExpressionRef> left_key_expressions;
        std::vector<AbstractExpressionRef> right_key_expressions;
        // 将 (tuple_idx, col_idx) 转为 ColumnValueExpression
        const Schema &left_schema = nested_loop_join_plan.GetLeftPlan()->OutputSchema();
        const Schema &right_schema = nested_loop_join_plan.GetRightPlan()->OutputSchema();
        // 需要保证每个等值条件是一左一右，且数量一致
        if (left_cols.size() != right_cols.size() || left_cols.empty()) {
          return optimized_plan;
        }
        // 保证所有 left 在 tuple_idx==0，所有 right 在 tuple_idx==1
        for (size_t i = 0; i < left_cols.size(); ++i) {
          if (left_cols[i].first != 0 || right_cols[i].first != 1) {
            return optimized_plan;
          }
        }
        for (size_t i = 0; i < left_cols.size(); ++i) {
          auto [l_tuple_idx, l_col_idx] = left_cols[i];   // 约定 l_tuple_idx == 0
          auto [r_tuple_idx, r_col_idx] = right_cols[i];  // 约定 r_tuple_idx == 1
          left_key_expressions.emplace_back(
              std::make_shared<ColumnValueExpression>(l_tuple_idx, l_col_idx, left_schema.GetColumn(l_col_idx)));
          right_key_expressions.emplace_back(
              std::make_shared<ColumnValueExpression>(r_tuple_idx, r_col_idx, right_schema.GetColumn(r_col_idx)));
        }
        return std::make_shared<HashJoinPlanNode>(nested_loop_join_plan.output_schema_,
                                                  nested_loop_join_plan.GetLeftPlan(),
                                                  nested_loop_join_plan.GetRightPlan(), left_key_expressions,
                                                  right_key_expressions, nested_loop_join_plan.GetJoinType());
      }
    }
  }
  return optimized_plan;
}

}  // namespace bustub
