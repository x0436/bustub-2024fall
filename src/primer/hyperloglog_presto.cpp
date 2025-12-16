#include "primer/hyperloglog_presto.h"

namespace bustub {

template <typename KeyType>
HyperLogLogPresto<KeyType>::HyperLogLogPresto(int16_t n_leading_bits) : cardinality_(0) {
  if (n_leading_bits < 0) {
    b_ = 0;
  } else {
    b_ = n_leading_bits;
  }
  dense_bucket_.resize(1 << b_);
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::GetRegister(size_t i) -> size_t {
  if (overflow_bucket_.count(i) > 0) {
    return (overflow_bucket_[i].to_ulong() << 4) | dense_bucket_[i].to_ulong();
  }
  return dense_bucket_[i].to_ulong();
}

template <typename KeyType>
void HyperLogLogPresto<KeyType>::SetRegister(size_t i, size_t num) {
  if (num < 16) {
    std::bitset<4> reg(num);
    dense_bucket_[i] = reg;
  } else {
    std::bitset<3> high((num >> 4) & ((1ULL << 3) - 1));
    std::bitset<4> low(num & ((1ULL << 4) - 1));
    overflow_bucket_[i] = high;
    dense_bucket_[i] = low;
  }
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::ComputeBinary(const hash_t &hash) const -> std::bitset<BITSET_CAPACITY> {
  std::bitset<BITSET_CAPACITY> bits(hash);
  return bits;
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::CountTrailingZeros(const std::bitset<BITSET_CAPACITY> &bits) const -> uint64_t {
  int cnt = 0;
  for (size_t i = 0; i < bits.size() - b_; ++i) {
    if (!bits[i]) {
      cnt++;
    } else {
      break;
    }
  }
  return cnt;
}

template <typename KeyType>
auto HyperLogLogPresto<KeyType>::AddElem(KeyType val) -> void {
  hash_t hash_value = CalculateHash(val);
  auto binary_value = ComputeBinary(hash_value);
  size_t index = 0;
  for (int i = 0; i < b_; ++i) {
    index = (index << 1) | binary_value[binary_value.size() - 1 - i];
  }
  size_t zero_cnt = CountTrailingZeros(binary_value);
  if (zero_cnt > GetRegister(index)) {
    SetRegister(index, zero_cnt);
  }
}

template <typename T>
auto HyperLogLogPresto<T>::ComputeCardinality() -> void {
  auto m = 1 << b_;
  double sum = 0.0;
  for (int i = 0; i < m; ++i) {
    auto power = static_cast<int64_t>(GetRegister(i));
    sum += std::pow(2.0, -power);
  }
  cardinality_ = std::floor(CONSTANT * m * m / sum);
}

template class HyperLogLogPresto<int64_t>;
template class HyperLogLogPresto<std::string>;
}  // namespace bustub
