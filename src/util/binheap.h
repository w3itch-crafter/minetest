
#include <cassert>
#include <utility>
#include "config.h"
#include "debug.h"
#include "util/basic_macros.h"

/*
 * BinHeap
 *
 * A binary heap (min-heap) backed by externally-allocated nodes.
 *
 * Same as std::priority_queue, but allows for fast removal of
 * arbitrary nodes. Performance characteristics:
 *
 *  insert()     O(log(size))
 *  remove()     O(log(size))
 *  top()        O(1)
 *
 * In order to not corrupt the heap, be careful that:
 *
 *   - Nodes stay valid while in the heap (allocated and not destructed)
 *
 *   - While a node is in the heap, it's ordering relative to other nodes
 *     does not change. (The node's `value` can be modified otherwise)
 *
 * Example:
 *
 *     BinHeap<int> h;
 *     BinHeap<int>::node_type node1(300);
 *     BinHeap<int>::node_type node2(200);
 *     BinHeap<int>::node_type node3;
 *
 *     node3.value = 100; // OK because not node3 not inserted yet
 *     BinHeap.insert(&node1);
 *     BinHeap.insert(&node2);
 *     BinHeap.insert(&node3);
 *
 *     std::cout << BinHeap.top()->value; // Prints 100, top is node3
 *     BinHeap.remove(&node3);
 *
 *     std::cout << BinHeap.top()->value; // Prints 200, top is node2
 *     BinHeap.remove(&node2);
 *
 *     node2.value = 400; // OK because node2 was removed
 *     node2.insert(&node2);
 *
 *     node2.value = 1000; // NOT OK! node2 is in the heap, and this
 *                            would change the order
 */
template<class T, class Compare = std::less<T> >
class BinHeap;

template<typename T, typename Compare>
struct BinHeapNode {
public:
	DISABLE_CLASS_COPY(BinHeapNode);

	// Forward arguments to T's constructor.
	template<typename ...Args>
	BinHeapNode(Args&& ...args) :
		value(std::forward<Args>(args)...),
		in_heap(nullptr),
		parent(nullptr),
		left(nullptr),
		right(nullptr)
	{ }

	~BinHeapNode()
	{
		assert(in_heap == nullptr);
	}

	bool inHeap() const
	{
		return in_heap != nullptr;
	}

	T value;
protected:
	friend class BinHeap<T, Compare>;

	BinHeap<T, Compare> *in_heap;
	// These are only valid if in_heap is not NULL
	BinHeapNode *parent;
	BinHeapNode *left;
	BinHeapNode *right;
};

template<class T, class Compare>
class BinHeap {
public:
	using value_type = T;
	using node_type = BinHeapNode<T, Compare>;

	// Create an empty BinHeap.
	BinHeap() :
		is_less_than(),
		m_size(0),
		m_root(nullptr)
	{ }

	~BinHeap()
	{
		clear();
	}

	// Remove all nodes from the heap.
	void clear()
	{
		remove_recursively(m_root);
		m_size = 0;
	}



	DISABLE_CLASS_COPY(BinHeap);

	size_t size() const
	{
		return m_size;
	}

	bool empty() const
	{
		return m_size == 0;
	}

	void insert(node_type *node)
	{
		assert(node->in_heap == nullptr);

		// Insert at the end of the bottom row
		// Use the size to determine the path to get there.
		node_type **parent;
		node_type **cursor;
		find_index(m_size + 1, parent, cursor);
		assert(*cursor == nullptr);

		node->in_heap = this;
		node->parent = *parent;
		node->left = nullptr;
		node->right = nullptr;
		*cursor = node;
		m_size++;

		// Restore the heap property
		sift_up(node);
	}

	void remove(node_type *node)
	{
		assert(node->in_heap == this);
		assert(m_size > 0);

		// Find the terminal node (last position in bottom row)
		node_type **parent;
		node_type **cursor;
		find_index(m_size, parent, cursor);
		assert(*parent && *cursor);

		// Swap it with the node we want to remove, so that
		// 'node' is now in the terminal position.
		node_type *terminal = *cursor;
		if (terminal != node) {
			swap_nodes(node, terminal);
		}

		// Remove node (now in the last position)
		assert(node->left == nullptr);
		assert(node->right == nullptr);
		change_child(node->parent, node, nullptr);
		node->in_heap = nullptr;
		m_size--;

		// Restore the heap property
		if (terminal != node) {
			sift_down(terminal);
			sift_up(terminal);
		}

	}

	// Peek at the node on the top of the heap.
	// Returns null if the heap is empty.
	node_type *top() const
	{
		return m_root;
	}

#if BUILD_UNITTESTS
	// Validate the internal structure completely. Use for testing only.
	void validate() const
	{
		size_t count, depth;
		_validate(m_root, count, depth);
		SANITY_CHECK(count == m_size);
		SANITY_CHECK(depth < 64);
		size_t fullTreeSize = (((size_t)1) << depth) - 1;
		SANITY_CHECK(fullTreeSize/2 <= m_size && m_size <= fullTreeSize);
	}
#endif

private:
	Compare is_less_than;
	size_t m_size;
	node_type *m_root;

	// Nodes are indexed like so:
	//                               1
	//                  10                              11
	//        100             101             110               111
	//   1000     1001   1010     1011   1100     1101     1110     1111
	void find_index(size_t index, node_type** &parent, node_type** &cursor)
	{
		assert(index > 0);

		// Reverse the bits after the leading '1'. This gives the path.
		// e.g. 1010011 becomes 1110010
		// The leading '1' is used as a marker bit, denoting the end of the path.
		size_t path = 1;
		while (index > 1) {
			path <<= 1;
			path |= index & 1;
			index >>= 1;
		}
		parent = &m_root;
		cursor = &m_root;
		while (path > 1) {
			assert(*cursor != nullptr);
			parent = cursor;
			cursor = (path & 1) ? &(*cursor)->right : &(*cursor)->left;
			path >>= 1;
		}
	}

	void sift_up(node_type *node)
	{
		// While 'node' is smallest than its parent, move it upward.
		while (node->parent && is_less_than(node->value, node->parent->value)) {
			swap_nodes(node->parent, node);
		}
	}

	void sift_down(node_type *node)
	{
		// While 'node' is larger than either of its children,
		// move it downward.
		for (;;) {
			node_type *new_min = node;
			if (node->left && is_less_than(node->left->value, new_min->value))
				new_min = node->left;
			if (node->right && is_less_than(node->right->value, new_min->value))
				new_min = node->right;
			if (new_min == node)
				break;
			swap_nodes(new_min, node);
		}
	}

	void change_child(node_type *parent, node_type *child, node_type *newchild)
	{
		if (parent == nullptr) {
			assert(m_root == child);
			m_root = newchild;
		} else if (parent->left == child) {
			parent->left = newchild;
		} else if (parent->right == child) {
			parent->right = newchild;
		} else {
			sanity_check(0); // invalid parent/child
		}
	}

	void change_parent(node_type *child, node_type *parent, node_type *newparent)
	{
		if (!child)
			return;
		assert(child->parent == parent);
		child->parent = newparent;
	}

	void swap_left_child(node_type *parent, node_type *child)
	{
		//                parent
		//      child                 r
		//   ll       lr
		node_type *r = parent->right;
		node_type *ll = child->left;
		node_type *lr = child->right;
		change_child(parent->parent, parent, child);
		child->parent = parent->parent;
		child->left = parent;
		child->right = r;
		parent->parent = child;
		parent->left = ll;
		parent->right = lr;
		if (ll) ll->parent = parent;
		if (lr) lr->parent = parent;
		if (r) r->parent = child;
	}

	void swap_right_child(node_type *parent, node_type *child)
	{
		//                parent
		//        l                  child
		//                         rl       rr
		node_type *l = parent->left;
		node_type *rl = child->left;
		node_type *rr = child->right;
		change_child(parent->parent, parent, child);
		child->parent = parent->parent;
		child->left = l;
		child->right = parent;
		parent->parent = child;
		parent->left = rl;
		parent->right = rr;
		if (rl) rl->parent = parent;
		if (rr) rr->parent = parent;
		if (l) l->parent = child;
	}

	void swap_same_parent(node_type *node1, node_type *node2)
	{
		assert(node1->parent == node2->parent);
		node_type *parent = node1->parent;
		std::swap(parent->left, parent->right);
		std::swap(node1->left, node2->left);
		std::swap(node1->right, node2->right);
	}

	void swap_nodes(node_type *node1, node_type *node2)
	{
		// This is the trickiest part of this data structure, because
		// node1 and node2 could be parent/child/siblings.
		if (node1->parent == node2->parent)
			swap_same_parent(node1, node2);
		else if (node1->left == node2)
			swap_left_child(node1, node2);
		else if (node1->right == node2)
			swap_right_child(node1, node2);
		else if (node2->left == node1)
			swap_left_child(node2, node1);
		else if (node2->right == node1)
			swap_right_child(node2, node1);
		else
			swap_distant(node1, node2);
	}

	void swap_distant(node_type *node1, node_type *node2)
	{
		// Swap references to node1 and node2 in parents
		change_child(node1->parent, node1, node2);
		change_child(node2->parent, node2, node1);

		// Reparent children
		change_parent(node1->left, node1, node2);
		change_parent(node1->right, node1, node2);
		change_parent(node2->left, node2, node1);
		change_parent(node2->right, node2, node1);

		// Finally swap their own pointers
		std::swap(node1->parent, node2->parent);
		std::swap(node1->left, node2->left);
		std::swap(node1->right, node2->right);
	}

	void remove_recursively(node_type *node)
	{
		if (node) {
			node->in_heap = nullptr;
			remove_recursively(node->left);
			remove_recursively(node->right);
		}
	}

#if BUILD_UNITTESTS
	void _validate(node_type *base, size_t &count, size_t &depth) const
	{
		if (base == nullptr) {
			count = 0;
			depth = 0;
			return;
		}
		SANITY_CHECK(base->in_heap == this);
		if (base->left) {
			SANITY_CHECK(!is_less_than(base->left->value, base->value));
			SANITY_CHECK(base->left->parent == base);
		}
		if (base->right) {
			SANITY_CHECK(!is_less_than(base->right->value, base->value));
			SANITY_CHECK(base->right->parent == base);
		}
		size_t leftcount, leftdepth;
		size_t rightcount, rightdepth;
		_validate(base->left, leftcount, leftdepth);
		_validate(base->right, rightcount, rightdepth);
		SANITY_CHECK(leftcount >= rightcount);
		count = 1 + leftcount + rightcount;
		depth = 1 + std::max(leftdepth, rightdepth);
	}
#endif
};
