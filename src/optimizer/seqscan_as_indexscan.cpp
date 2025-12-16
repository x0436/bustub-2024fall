#include <memory>
#include <vector>
#include "execution/plans/filter_plan.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/limit_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "execution/plans/sort_plan.h"
#include "execution/plans/topn_plan.h"

#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"

#include "optimizer/optimizer.h"

namespace bustub {

static bool CollectEqualities(const AbstractExpressionRef &expr, std::vector<std::pair<uint32_t, uint32_t>> &cols,
                              std::vector<AbstractExpressionRef> &tmp_pred_keys) {
  // 1.如果是比较表达式
  if (auto cmp = dynamic_cast<ComparisonExpression *>(expr.get())) {
    // 只处理等号比较
    if (cmp->comp_type_ != ComparisonType::Equal) {
      return false;
    }
    auto left = cmp->GetChildAt(0);
    auto right = cmp->GetChildAt(1);
    // 判定左右是列或常量的具体表达式类型
    auto left_col = dynamic_cast<ColumnValueExpression *>(left.get());
    auto right_col = dynamic_cast<ColumnValueExpression *>(right.get());
    auto left_const = dynamic_cast<ConstantValueExpression *>(left.get());
    auto right_const = dynamic_cast<ConstantValueExpression *>(right.get());

    // 只接受 col = const 或 const = col
    if (left_col && right_const) {
      cols.emplace_back(left_col->GetTupleIdx(), left_col->GetColIdx());
      tmp_pred_keys.emplace_back(right);
      return true;
    }
    if (right_col && left_const) {
      cols.emplace_back(right_col->GetTupleIdx(), right_col->GetColIdx());
      tmp_pred_keys.emplace_back(left);
      return true;
    }
    return false;  // 其他形式（例如 col = col、const = const、col op col 等）不匹配
  }
  // 2.如果是逻辑表达式
  if (auto logic = dynamic_cast<LogicExpression *>(expr.get())) {
    // 只允许 OR 在顶层（支持 v = c OR v = d 的多点查找）
    if (logic->logic_type_ != LogicType::Or) {
      return false;
    }
    // 递归分别收集左右子表达式的等值项，任一侧失败则整体失败
    if (!CollectEqualities(logic->GetChildAt(0), cols, tmp_pred_keys)) {
      return false;
    }
    if (!CollectEqualities(logic->GetChildAt(1), cols, tmp_pred_keys)) {
      return false;
    }
    return true;
  }

  // 其他表达式类型（例如函数调用、算术、NOT 等）不匹配该模式
  return false;
}
auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement seq scan with predicate -> index scan optimizer rule
  // The Filter Predicate Pushdown has been enabled for you in optimizer.cpp when forcing starter rule
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }

  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::SeqScan) {
    const auto &seq_scan_plan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);
    if (seq_scan_plan.filter_predicate_) {  // 如果seq_scan_plan有谓词, 需要判断是否在表上
      std::vector<std::pair<uint32_t, uint32_t>> cols;
      std::vector<AbstractExpressionRef> tmp_pred_keys;

      // 如果谓词符合格式
      if (CollectEqualities(seq_scan_plan.filter_predicate_, cols, tmp_pred_keys) && !cols.empty()) {
        // 要求所有等值项引用完全相同的列（同一索引）
        const auto &first = cols.front();
        for (const auto &p : cols) {
          if (p != first) {
            // 若不同列，则放弃转换（例如 v1=1 OR v2=2 不转换）
            return optimized_plan;
          }
        }
        // 确认这列上确实有索引（否则不能用 IndexScan）
        uint32_t col_idx = first.second;
        bool has_index = false;
        // 从 Catalog 获取该表的所有索引，检查是否有索引的 key 列等于 col_idx
        auto indexes = catalog_.GetTableIndexes(seq_scan_plan.table_name_);
        IndexInfo *index_info = nullptr;
        for (const auto &idx_info : indexes) {
          auto key_attrs = idx_info->index_->GetKeyAttrs();  // 返回索引对应的列号
          if (key_attrs.size() == 1 && key_attrs[0] == col_idx) {
            has_index = true;
            index_info = idx_info.get();
            break;
          }
        }
        if (!has_index) {
          // 该列没有索引，不能将 SeqScan 转为 IndexScan
          return optimized_plan;
        }
        // 索引存在，优化为 IndexScan计划节点
        return std::make_shared<IndexScanPlanNode>(seq_scan_plan.output_schema_, seq_scan_plan.table_oid_,
                                                   index_info->index_oid_, seq_scan_plan.filter_predicate_,
                                                   tmp_pred_keys);
      }
    }
  }
  return optimized_plan;
}

}  // namespace bustub
