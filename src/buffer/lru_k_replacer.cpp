//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include "common/exception.h"

namespace bustub {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) { curr_size_ = 0; }

auto LRUKReplacer::Evict() -> std::optional<frame_id_t> {
  std::scoped_lock lock(latch_);
  frame_id_t frame_id;

  if (!history_list_.empty()) {
    for (auto p : history_list_) {
      if (evictable_[p]) {
        frame_id = p;
        Remove(p);
        return frame_id;
      }
    }
  }
  if (!cache_list_.empty()) {
    for (auto p : cache_list_) {
      if (evictable_[p]) {
        frame_id = p;
        Remove(p);
        return frame_id;
      }
    }
  }
  return std::nullopt;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  std::scoped_lock lock(latch_);
  // 如果帧无效, 抛出异常或中止进程
  size_t &access_count = tracker_[frame_id];
  if (access_count == 0) {
    history_list_.push_back(frame_id);
    history_map_[frame_id] = std::prev(history_list_.end());
    evictable_[frame_id] = false;
  } else if (access_count > 0 && access_count < k_ - 1) {
  } else if (k_ - 1 == access_count) {
    history_list_.erase(history_map_[frame_id]);
    history_map_.erase(frame_id);
    cache_list_.push_back(frame_id);
    cache_map_[frame_id] = std::prev(cache_list_.end());
  } else {
    cache_list_.erase(cache_map_[frame_id]);
    cache_list_.push_back(frame_id);
    cache_map_[frame_id] = std::prev(cache_list_.end());
  }
  access_count++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock lock(latch_);
  // 如果帧无效, 抛出异常或中止进程
  if (evictable_[frame_id] && !set_evictable) {
    curr_size_--;
  }
  if (!evictable_[frame_id] && set_evictable) {
    curr_size_++;
  }
  evictable_[frame_id] = set_evictable;
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  // 如果给定的帧没有找到, 直接返回
  if (tracker_.count(frame_id) == 0U) {
    return;
  }
  if (evictable_[frame_id]) {
    if (tracker_[frame_id] < k_) {
      history_list_.erase(history_map_[frame_id]);
      history_map_.erase(frame_id);
    } else {
      cache_list_.erase(cache_map_[frame_id]);
      cache_map_.erase(frame_id);
    }
    tracker_.erase(frame_id);
    curr_size_--;
  } else {  // 不可驱逐
    throw std::runtime_error("frame not evictable");
  }
}

auto LRUKReplacer::Size() -> size_t {
  std::scoped_lock lock(latch_);
  return curr_size_;
}

}  // namespace bustub
