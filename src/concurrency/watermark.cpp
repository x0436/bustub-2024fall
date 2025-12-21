#include "concurrency/watermark.h"
#include <exception>
#include "common/exception.h"

namespace bustub {

auto Watermark::AddTxn(timestamp_t read_ts) -> void {
  if (read_ts < commit_ts_) {
    throw Exception("read ts < commit ts");
  }
  auto it = current_reads_.find(read_ts);
  if (it != current_reads_.end()) {
    it->second++;
  } else { // 没找到
    current_reads_.insert({read_ts, 1});
  }
  set_.insert(read_ts);
  if (!set_.empty()) {
    watermark_ = *set_.begin();
  }
}

auto Watermark::RemoveTxn(timestamp_t read_ts) -> void {
  // TODO(fall2023): implement me!
  auto it = current_reads_.find(read_ts);
  if (it != current_reads_.end()) {
    current_reads_[read_ts]--;
    if (current_reads_[read_ts] == 0) {
      current_reads_.erase(read_ts);
    }
  } else {
    throw Exception("can not find ts");
  }
  auto set_it = set_.find(read_ts);
  set_.erase(set_it);
  watermark_ = *set_.begin();
}

}  // namespace bustub
