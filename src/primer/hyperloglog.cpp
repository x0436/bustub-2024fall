#include "primer/hyperloglog.h"

namespace bustub {

template <typename KeyType>
HyperLogLog<KeyType>::HyperLogLog(int16_t n_bits) : cardinality_(0), b_(n_bits) {
  if (n_bits < 0) {
    buckets_.resize(1);
  }
  buckets_.resize(1 << b_);
}

template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeBinary(const hash_t &hash) const -> std::bitset<BITSET_CAPACITY> {
  std::bitset<BITSET_CAPACITY> bits(hash);
  return bits;
}

template <typename KeyType>
auto HyperLogLog<KeyType>::PositionOfLeftmostOne(const std::vector<bool> &bits) const -> uint64_t {
  uint64_t res = 0;
  for (auto i = static_cast<int64_t>(bits.size() - 1); i >= 0; --i) {
    if (bits[i]) {
      return res + 1;
    }
    res++;
  }
  return 0;
}

template <typename KeyType>
auto HyperLogLog<KeyType>::AddElem(KeyType val) -> void {
  hash_t hash_value = CalculateHash(val);
  auto binary_value = ComputeBinary(hash_value);

  size_t index = 0;
  for (int i = 0; i < b_; ++i) {
    index = (index << 1) | binary_value[binary_value.size() - 1 - i];
  }

  size_t right_size = binary_value.size() - b_;
  std::vector<bool> right(right_size);
  for (size_t i = 0; i < binary_value.size() - b_; ++i) {
    right[i] = binary_value[i];
  }

  auto p = PositionOfLeftmostOne(right);
  buckets_[index] = buckets_[index] > p ? buckets_[index] : p;
}

template <typename KeyType>
auto HyperLogLog<KeyType>::ComputeCardinality() -> void {
  if (b_ < 0) {
    return;
  }
  auto m = 1 << b_;
  double sum = 0.0;
  for (int i = 0; i < m; ++i) {
    sum += std::pow(2.0, -buckets_[i]);
  }
  cardinality_ = std::floor(CONSTANT * m * m / sum);
}

template class HyperLogLog<int64_t>;
template class HyperLogLog<std::string>;

}  // namespace bustub
