#include "storage/index/b_plus_tree.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool { return GetRootPageId() == INVALID_PAGE_ID; }

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  Context ctx;
  ctx.root_page_id_ = GetRootPageId();
  if (!FindLeaf(key, &ctx, Operation::SEARCH)) {
    return false;
  }
  // 找到叶子节点后, 判断是否在叶子节点内
  auto &guard = ctx.read_set_.back();
  const auto *leaf_page = guard.As<LeafPage>();
  auto index = leaf_page->Lookup(key, comparator_);
  // LOG_DEBUG("target index = %d", index);
  auto size = leaf_page->GetSize();

  if (index < size && comparator_(leaf_page->KeyAt(index), key) == 0) {  // 如果目标key在叶子节点内
    result->push_back(leaf_page->ValueAt(index));
    return true;
  }
  // LOG_DEBUG("Can't find key");
  return false;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // std::lock_guard<std::mutex> lock(tree_latch_);
  // LOG_DEBUG("Insert key start: %ld", key.ToString());
  auto header_guard = bpm_->WritePage(header_page_id_);  // 阻塞, 一次过一个
  auto *header_page = header_guard.AsMut<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID) {  // 1. 如果树为空树, 新建一棵树
    auto root_page_id = StartNewTree(key, value);
    // LOG_DEBUG("SetRootPageId is called, root page id is %d", root_page_id);
    header_page->root_page_id_ = root_page_id;
    return true;
  }
  // 2. 树非空, 考虑插入
  Context ctx;
  ctx.root_page_id_ = header_page->root_page_id_;
  ctx.write_set_.push_back(std::move(header_guard));

  FindLeaf(key, &ctx, Operation::INSERT);  // 获得叶子节点

  auto &guard = ctx.write_set_.back();
  auto *leaf_page = guard.AsMut<LeafPage>();
  // 检查是否为重复key
  int index = leaf_page->Lookup(key, comparator_);  // 搜索key
  auto size = leaf_page->GetSize();

  if (index < size && comparator_(leaf_page->KeyAt(index), key) == 0) {  // key重复, 提前结束插入
    // LOG_DEBUG("%ld is duplicate key", key.ToString());
    return false;
  }
  if (leaf_page->GetSize() < leaf_page->GetMaxSize() - 1) {
    // LOG_DEBUG("target page id is %d , and don't need split", leaf_page->GetPageId());
    // 2.1 无需分裂
    InsertInLeaf(key, value, &ctx);
  } else {  // 2.2 插入后将等于max_size, 先插入, 后分裂
    InsertInLeaf(key, value, &ctx);

    auto &left_page_guard = ctx.write_set_.back();  // 要分裂的节点
    auto *left_page = left_page_guard.AsMut<LeafPage>();
    auto left_page_id = left_page->GetPageId();

    Split(&ctx);  // 分裂, 新节点存入write_set

    auto &right_page_guard = ctx.write_set_.back();  // 新节点
    auto *right_page = right_page_guard.AsMut<LeafPage>();

    right_page->SetNextPageId(left_page->GetNextPageId());  // 设置next_page_id
    left_page->SetNextPageId(right_page->GetPageId());

    auto right_page_id = right_page->GetPageId();
    auto split_key = right_page->KeyAt(0);

    InsertInParent(left_page_id, split_key, right_page_id, &ctx);  // 将原节点和新节点发给父节点
    header_page->root_page_id_ = ctx.root_page_id_;
  }
  // LOG_DEBUG("Insert key end: %ld", key.ToString());
  return true;
}
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::StartNewTree(KeyType key, ValueType value) -> page_id_t {
  auto new_page_id = bpm_->NewPage();  // 创建新页面装 root page
  // LOG_DEBUG("Make a new tree, its root page id is %d", new_page_id);
  auto guard = bpm_->WritePage(new_page_id);
  auto *root_page = guard.AsMut<LeafPage>();
  root_page->Init(new_page_id, leaf_max_size_);
  root_page->Insert(key, value, comparator_);
  return new_page_id;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertInLeaf(KeyType key, ValueType value, Context *ctx) {
  auto &guard = ctx->write_set_.back();  // 获取叶子节点
  auto *leaf_page = guard.AsMut<LeafPage>();
  // LOG_DEBUG("Insert key is %ld, Insert in page id:%d, search index is %d", key.ToString(), leaf_page->GetPageId(),
  //           leaf_page->Lookup(key, comparator_));
  leaf_page->Insert(key, value, comparator_);
  // 结束后 guard 并未失效
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Split(Context *ctx) {
  auto &old_page_guard = ctx->write_set_.back();  // 原节点
  auto *old_page = old_page_guard.AsMut<LeafPage>();
  auto new_page_id = bpm_->NewPage();  // 创建新节点
  auto new_page_guard = bpm_->WritePage(new_page_id);
  auto *new_page = new_page_guard.AsMut<LeafPage>();
  new_page->Init(new_page_id, leaf_max_size_);

  // LOG_DEBUG("old page id is %d, Move half to new page id:%d", old_page->GetPageId(), new_page_id);

  old_page->MoveHalfTo(new_page);
  ctx->write_set_.push_back(std::move(new_page_guard));
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::InsertInParent(page_id_t left_page_id, KeyType split_key, page_id_t right_page_id, Context *ctx) {
  if (ctx->IsRootPage(left_page_id)) {  // 1.如果原节点是根节点, 树升高
    // LOG_DEBUG("left page is root page, make new root, split_key is %ld", split_key.ToString());
    auto new_page_id = bpm_->NewPage();  // 新建一个根节点
    auto guard = bpm_->WritePage(new_page_id);
    auto *new_root_page = guard.AsMut<InternalPage>();
    new_root_page->Init(new_page_id, internal_max_size_);

    new_root_page->SetValueAt(0, left_page_id);
    new_root_page->SetKeyAt(1, split_key);
    new_root_page->SetValueAt(1, right_page_id);
    new_root_page->SetSize(2);

    // 设 new_page_id 为树的根节点
    ctx->write_set_.push_back(std::move(guard));
    ctx->root_page_id_ = new_page_id;
    return;
  }
  // 2.如果父节点安全, 将2个node和split_key插入父节点, 不触发分裂
  int size = ctx->write_set_.size();
  auto &parent_guard = ctx->write_set_[size - 3];
  auto *parent_page = parent_guard.AsMut<InternalPage>();

  if (parent_page->GetSize() < parent_page->GetMaxSize()) {
    // LOG_DEBUG("parent id:%d don't need split, split_key is %ld", parent_page->GetPageId(), split_key.ToString());
    parent_page->Insert(split_key, right_page_id, comparator_);
    return;
  }
  // 2.2父节点触发分裂
  // LOG_DEBUG("parent id:%d need split, split_key is %ld", parent_page->GetPageId(), split_key.ToString());
  // 手动分裂
  int old_size = parent_page->GetSize();
  KeyType temp_keys[INTERNAL_PAGE_SLOT_CNT + 1];
  page_id_t temp_vals[INTERNAL_PAGE_SLOT_CNT + 1];
  int insert_pos = parent_page->Lookup(split_key, comparator_);

  std::copy(parent_page->KeyArray(), parent_page->KeyArray() + insert_pos,
            temp_keys);  // 这串代码的作用是创建临时空间存放膨胀的父节点P
  std::copy(parent_page->ValueArray(), parent_page->ValueArray() + insert_pos, temp_vals);
  temp_keys[insert_pos] = split_key;
  temp_vals[insert_pos] = right_page_id;
  std::copy(parent_page->KeyArray() + insert_pos, parent_page->KeyArray() + old_size, temp_keys + insert_pos + 1);
  std::copy(parent_page->ValueArray() + insert_pos, parent_page->ValueArray() + old_size, temp_vals + insert_pos + 1);

  // 新建节点P', 记得在删除原节点和右边节点后加入duque
  auto new_parent_id = bpm_->NewPage();
  auto new_parent_guard = bpm_->WritePage(new_parent_id);
  auto *new_parent_page = new_parent_guard.AsMut<InternalPage>();
  new_parent_page->Init(new_parent_id, internal_max_size_);

  int min_size = parent_page->GetMinSize();
  int new_size = parent_page->GetSize() + 1;
  int start_pos = new_size - min_size;
  std::copy(temp_keys + start_pos, temp_keys + new_size, new_parent_page->KeyArray());
  std::copy(temp_vals + start_pos, temp_vals + new_size, new_parent_page->ValueArray());
  std::copy(temp_keys, temp_keys + start_pos, parent_page->KeyArray());
  std::copy(temp_vals, temp_vals + start_pos, parent_page->ValueArray());
  auto new_split_key = new_parent_page->KeyAt(0);
  new_parent_page->SetSize(min_size);
  parent_page->SetSize(start_pos);

  ctx->write_set_.pop_back();
  ctx->write_set_.pop_back();
  // 更新子节点的parent id
  for (int i = 0; i < new_parent_page->GetSize(); i++) {
    page_id_t child_id = new_parent_page->ValueArray()[i];
    auto temp_guard = bpm_->WritePage(child_id);
    auto *temp_page = temp_guard.AsMut<BPlusTreePage>();
    temp_page->SetParentId(new_parent_id);
  }
  // 递归
  ctx->write_set_.push_back(std::move(new_parent_guard));
  InsertInParent(parent_page->GetPageId(), new_split_key, new_parent_id, ctx);
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeaf(KeyType key, Context *ctx, Operation operation) -> bool {
  // 1. 空树直接返回, 不会触发
  auto root_page_id = ctx->root_page_id_;
  if (root_page_id == INVALID_PAGE_ID) {
    // LOG_DEBUG("This tree is empty");
    return false;
  }
  if (operation == Operation::SEARCH) {
    auto cur_guard = bpm_->ReadPage(root_page_id);
    const auto *cur_page = cur_guard.template As<BPlusTreePage>();
    // 2. 根节点是叶子节点
    if (cur_page->IsLeafPage()) {
      // LOG_DEBUG("Root is leaf page, directly return, page id is %d", cur_page->GetPageId());
      ctx->read_set_.push_back(std::move(cur_guard));
      return true;
    }
    // 3. 根节点是非叶子节点
    // LOG_DEBUG("Root is internal page, page id is %d, search downwards", cur_page->GetPageId());
    while (!cur_page->IsLeafPage()) {
      const auto *temp_page = static_cast<const InternalPage *>(cur_page);
      auto temp_index = temp_page->Lookup(key, comparator_);  // 在当前page 搜索key
      auto child_page_id = temp_page->ValueAt(temp_index - 1);
      // LOG_DEBUG("parent id is %d, child page is the %d one, its page id is %d", cur_page->GetPageId(), temp_index,
      // child_page_id);
      auto child_guard = bpm_->ReadPage(child_page_id);
      cur_guard.Drop();
      cur_guard = std::move(child_guard);  // 子节点
      cur_page = cur_guard.template As<BPlusTreePage>();
    }
    ctx->read_set_.push_back(std::move(cur_guard));
    return true;
  }
  if (operation == Operation::INSERT) {
    auto cur_guard = bpm_->WritePage(root_page_id);
    auto *cur_page = cur_guard.template AsMut<BPlusTreePage>();
    ctx->write_set_.push_back(std::move(cur_guard));

    // 2. 根节点是叶子节点
    if (cur_page->IsLeafPage()) {
      // LOG_DEBUG("Root is leaf page, directly return, page id is %d", cur_page->GetPageId());
      return true;
    }
    // 3. 根节点是非叶子节点
    // LOG_DEBUG("Root is internal page, page id is %d, search downwards", cur_page->GetPageId());
    while (!cur_page->IsLeafPage()) {
      auto *temp_page = static_cast<InternalPage *>(cur_page);
      auto temp_index = temp_page->Lookup(key, comparator_);  //
      auto child_page_id = temp_page->ValueAt(temp_index - 1);
      // LOG_DEBUG("parent id is %d, child page is the %d one, its page id is %d", cur_page->GetPageId(), temp_index,
      // child_page_id);
      cur_guard = bpm_->WritePage(child_page_id);
      cur_page = cur_guard.template AsMut<BPlusTreePage>();

      bool safe = false;

      if (cur_page->IsLeafPage()) {
        safe = cur_page->GetSize() < cur_page->GetMaxSize() - 1;
      } else {
        safe = cur_page->GetSize() < cur_page->GetMaxSize();
      }

      if (safe) {
        ctx->write_set_.clear();
      }

      ctx->write_set_.push_back(std::move(cur_guard));
    }
    return true;
  }
  // 删除
  auto cur_guard = bpm_->WritePage(root_page_id);
  auto *cur_page = cur_guard.template AsMut<BPlusTreePage>();
  ctx->write_set_.push_back(std::move(cur_guard));

  // 2. 根节点是叶子节点
  if (cur_page->IsLeafPage()) {
    // LOG_DEBUG("Root is leaf page, directly return, page id is %d", cur_page->GetPageId());
    return true;
  }
  // 3. 根节点是非叶子节点
  // LOG_DEBUG("Root is internal page, page id is %d, search downwards", cur_page->GetPageId());
  while (!cur_page->IsLeafPage()) {
    auto *temp_page = static_cast<InternalPage *>(cur_page);
    auto temp_index = temp_page->Lookup(key, comparator_);  //
    auto child_page_id = temp_page->ValueAt(temp_index - 1);
    // LOG_DEBUG("parent id is %d, child page is the %d one, its page id is %d", cur_page->GetPageId(), temp_index,
    // child_page_id);
    cur_guard = bpm_->WritePage(child_page_id);
    cur_page = cur_guard.template AsMut<BPlusTreePage>();
    if (cur_page->GetSize() > cur_page->GetMinSize()) {  // 大于minsize说明当前节点安全
      ctx->write_set_.clear();
    }
    ctx->write_set_.push_back(std::move(cur_guard));
  }
  return true;
}
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::FindLeftMost() -> page_id_t {
  auto root_page_id = GetRootPageId();
  if (root_page_id == INVALID_PAGE_ID) {
    // LOG_DEBUG("This tree is empty");
    return INVALID_PAGE_ID;
  }

  auto root_guard = bpm_->ReadPage(root_page_id);
  const auto *root_page = root_guard.template As<BPlusTreePage>();
  const auto *cur_page = root_page;
  while (!cur_page->IsLeafPage()) {
    const auto *temp_page = static_cast<const InternalPage *>(cur_page);
    auto child_page_id = temp_page->ValueAt(0);
    auto guard = bpm_->ReadPage(child_page_id);
    cur_page = guard.template As<BPlusTreePage>();
  }
  return cur_page->GetPageId();
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // std::lock_guard<std::mutex> lock(tree_latch_);
  // LOG_DEBUG("remove key start: %ld", key.ToString());
  auto header_guard = bpm_->WritePage(header_page_id_);  // 阻塞, 一次过一个
  auto *header_page = header_guard.AsMut<BPlusTreeHeaderPage>();
  if (header_page->root_page_id_ == INVALID_PAGE_ID) {  // 1. 空树直接返回
    return;
  }
  // 2. 树非空, 搜索到叶子节点, 调用DeleteEntry递归删除
  Context ctx;
  ctx.root_page_id_ = header_page->root_page_id_;
  ctx.write_set_.push_back(std::move(header_guard));
  FindLeaf(key, &ctx, Operation::DELETE);
  // LOG_DEBUG("we will remove %ld", key.ToString());
  DeleteEntry(key, &ctx);
  header_page->root_page_id_ = ctx.root_page_id_;
  // LOG_DEBUG("remove key end: %ld", key.ToString());
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::DeleteEntry(const KeyType &key, Context *ctx) {
  Delete(key, ctx);
  std::vector<decltype(bpm_->WritePage(0))> local_guards;

  // 1.如果当前节点是根节点且删除一组kv对后只有一个子节点, 让该子节点成为树的新根节点并删除当前节点
  auto &cur_guard = ctx->write_set_.back();
  auto *cur_page = cur_guard.AsMut<BPlusTreePage>();
  if (ctx->IsRootPage(cur_page->GetPageId()) && cur_page->GetSize() == 0) {  // 1.如果是根节点并且根节点已经空了
    ctx->root_page_id_ = INVALID_PAGE_ID;
    return;
  }

  if (ctx->IsRootPage(cur_page->GetPageId()) && cur_page->GetSize() == 1 &&
      !cur_page->IsLeafPage()) {  // 2.如果当前节点是根节点, 且不是叶子节点, 只剩下一个子节点
    // LOG_DEBUG("tree hight decreases");
    auto *old_root_page = static_cast<InternalPage *>(cur_page);
    auto child_page_id = old_root_page->ValueAt(0);
    auto child_guard = bpm_->WritePage(child_page_id);  // 崩溃点
    auto *child_page = child_guard.template AsMut<BPlusTreePage>();
    child_page->SetParentId(INVALID_PAGE_ID);

    ctx->root_page_id_ = child_page_id;
    bpm_->DeletePage(cur_page->GetPageId());
  } else if (!ctx->IsRootPage(cur_page->GetPageId()) && cur_page->GetSize() < cur_page->GetMinSize()) {
    // 3.如果当前节点不是根节点, 且当前节点小于minsize (注意根节点没有minsize)
    // LOG_DEBUG("current node is less smaller than min size");
    // 需要获得兄弟节点和分隔key
    int size = ctx->write_set_.size();
    auto &parent_guard = ctx->write_set_[size - 2];  // 会不会出现没有父节点的情况
    auto *parent_page = parent_guard.AsMut<InternalPage>();
    int pos = parent_page->Lookup(key, comparator_) - 1;  // 找到当前节点在父节点中的位置
    int left_pos = pos - 1;
    int right_pos = pos + 1;

    BPlusTreePage *left_sibling_page = nullptr;
    BPlusTreePage *right_sibling_page = nullptr;
    KeyType split_key;

    // 尝试左偷
    if (left_pos >= 0) {
      auto left_sibling_id = parent_page->ValueAt(left_pos);
      auto left_sibling_guard = bpm_->WritePage(left_sibling_id);  // 左兄弟页的锁
      left_sibling_page = left_sibling_guard.template AsMut<BPlusTreePage>();
      local_guards.push_back(std::move(left_sibling_guard));
      // ctx->write_set_.push_front(std::move(left_sibling_guard));

      if (left_sibling_page->GetSize() > left_sibling_page->GetMinSize()) {  // 左兄弟有富余
        // LOG_DEBUG("left steal");
        auto new_split_key = Redistribute(left_sibling_page, cur_page, Direction::LEFT);  // 左偷
        // 修改父节点对应key
        // LOG_DEBUG("new split key is %ld, target pos is %d", new_split_key.ToString(), pos);
        parent_page->SetKeyAt(pos, new_split_key);
        return;
      }
    }
    // 尝试右偷
    if (right_pos < parent_page->GetSize()) {
      auto right_sibling_id = parent_page->ValueAt(right_pos);  // 右兄弟页的锁
      auto right_sibling_guard = bpm_->WritePage(right_sibling_id);
      right_sibling_page = right_sibling_guard.template AsMut<BPlusTreePage>();
      local_guards.push_back(std::move(right_sibling_guard));
      // ctx->write_set_.push_front(std::move(right_sibling_guard));

      if (right_sibling_page->GetSize() > right_sibling_page->GetMinSize()) {  // 右兄弟有富余
        // LOG_DEBUG("right steal");
        auto new_split_key = Redistribute(right_sibling_page, cur_page, Direction::RIGHT);  // 右偷
        // 修改父节点对应key
        parent_page->SetKeyAt(right_pos, new_split_key);
        return;
      }
    }
    // 没偷到, 说明左右兄弟节点都等于minsize, 只能合并
    if (left_pos >= 0) {
      split_key = parent_page->KeyAt(pos);
      // LOG_DEBUG("page %d merge with the left sibling %d, split key is %ld", cur_page->GetPageId(),
      // left_sibling_page->GetPageId(), split_key.ToString());
      Coalesce(left_sibling_page, cur_page);  // 和左兄弟合并
      ctx->write_set_.pop_back();             // cur_page是sender, 应该删除
      local_guards.clear();
      // 递归
      DeleteEntry(split_key, ctx);
    } else if (right_pos < parent_page->GetSize()) {
      split_key = parent_page->KeyAt(right_pos);
      // LOG_DEBUG("page %d merge with the right sibling %d, split key is %ld", cur_page->GetPageId(),
      Coalesce(cur_page, right_sibling_page);  // 和右兄弟合并
      ctx->write_set_.pop_back();              // right_sibling_page是sender, 但是cur_page也用不到了
      local_guards.clear();
      // 递归
      DeleteEntry(split_key, ctx);
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Redistribute(BPlusTreePage *sibling, BPlusTreePage *recipient, Direction direction) -> KeyType {
  if (direction == Direction::LEFT) {  // 左偷
    if (recipient->IsLeafPage()) {
      auto *left = static_cast<LeafPage *>(sibling);
      auto *right = static_cast<LeafPage *>(recipient);
      left->MoveLastTo(right);
      return right->KeyAt(0);
    }
    auto *left = static_cast<InternalPage *>(sibling);
    auto *right = static_cast<InternalPage *>(recipient);
    left->MoveLastTo(right);
    return right->KeyAt(0);
  }
  if (direction == Direction::RIGHT) {  // 右偷
    if (recipient->IsLeafPage()) {
      auto *left = static_cast<LeafPage *>(recipient);
      auto *right = static_cast<LeafPage *>(sibling);
      right->MoveFirstTo(left);
      return right->KeyAt(0);
    }  // 非叶子
    auto *left = static_cast<InternalPage *>(recipient);
    auto *right = static_cast<InternalPage *>(sibling);
    right->MoveFirstTo(left);
    return right->KeyAt(0);
  }
  throw std::runtime_error("Invalid direction in Redistribute");
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Coalesce(BPlusTreePage *recipient, BPlusTreePage *sender) {
  assert(sender != nullptr && recipient != nullptr);
  if (sender->IsLeafPage()) {
    auto *left = static_cast<LeafPage *>(recipient);
    auto *right = static_cast<LeafPage *>(sender);
    right->MoveAllTo(left);
    left->SetNextPageId(right->GetNextPageId());
  } else {
    auto *left = static_cast<InternalPage *>(recipient);
    auto *right = static_cast<InternalPage *>(sender);
    right->MoveAllTo(left);
    for (int i = 0; i < right->GetSize(); ++i) {
      page_id_t child_id = right->ValueAt(i);
      auto child_guard = bpm_->WritePage(child_id);
      auto *child_page = child_guard.template AsMut<BPlusTreePage>();
      child_page->SetParentId(left->GetPageId());
    }
  }
}

INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Delete(const KeyType &key, Context *ctx) -> bool {
  // 此函数只删除目标节点的一个kv对
  // 先获取要删除kv对的页面
  auto &guard = ctx->write_set_.back();
  auto *page = guard.AsMut<BPlusTreePage>();
  // 判断类型, 转换类型
  if (page->IsLeafPage()) {
    auto *leaf_page = static_cast<LeafPage *>(page);
    return leaf_page->Remove(key, comparator_);
  }
  auto *internal_page = static_cast<InternalPage *>(page);
  return internal_page->Remove(key, comparator_);
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  // LOG_DEBUG("Begin() is called");
  page_id_t left_most_page_id = FindLeftMost();
  return INDEXITERATOR_TYPE(left_most_page_id, 0, bpm_);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  // LOG_DEBUG("Begin() with a key is called");
  Context ctx;
  ctx.root_page_id_ = GetRootPageId();

  FindLeaf(key, &ctx, Operation::SEARCH);

  auto &guard = ctx.read_set_.back();
  const auto *cur_page = guard.As<LeafPage>();
  int pos = cur_page->Lookup(key, comparator_);  // 不必等于key
  return INDEXITERATOR_TYPE(cur_page->GetPageId(), pos, bpm_);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE {
  // LOG_DEBUG("End() is called");
  return INDEXITERATOR_TYPE(INVALID_PAGE_ID, 0, bpm_);
}

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() const -> page_id_t {
  auto guard = bpm_->ReadPage(header_page_id_);
  const auto *header_page = guard.As<BPlusTreeHeaderPage>();
  return header_page->root_page_id_;
}

INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::SetRootPageId(page_id_t root_page_id) {
  // LOG_DEBUG("SetRootPageId is called, root page id is %d", root_page_id);
  auto guard = bpm_->WritePage(header_page_id_);
  auto *header_page = guard.AsMut<BPlusTreeHeaderPage>();
  header_page->root_page_id_ = root_page_id;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
