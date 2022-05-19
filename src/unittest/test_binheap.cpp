/*
Minetest
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "test.h"
#include "util/binheap.h"
#include <algorithm>
#include <random>

class TestBinHeap : public TestBase {
public:
	TestBinHeap() { TestManager::registerTestModule(this); }
	const char *getName() { return "TestBinHeap"; }

	void runTests(IGameDef *gamedef);

	void testBasics();
	void testCompare();
	void testFuzz();
};

static TestBinHeap g_test_instance;

void TestBinHeap::runTests(IGameDef *gamedef)
{
	TEST(testBasics);
	TEST(testCompare);
	TEST(testFuzz);
}

void TestBinHeap::testBasics()
{
	using Heap = BinHeap<int>;
	using Node = Heap::node_type;

	Heap h;
	UASSERTEQ(size_t, h.size(), 0);
	UASSERT(h.empty());

	Node n1(10);
	Node n2{20};
	Node n3;
	Node n4;
	UASSERTEQ(int, n1.value, 10);
	UASSERTEQ(int, n2.value, 20);
	n3.value = 30;
	n4.value = 40;

	h.insert(&n3);
	h.insert(&n4);
	h.insert(&n2);
	h.insert(&n1);
	UASSERTEQ(size_t, h.size(), 4);
	UASSERT(!h.empty());
	h.validate();

	UASSERTEQ(int, h.top()->value, 10);
	h.remove(h.top());
	UASSERT(!n1.inHeap());

	UASSERTEQ(int, h.top()->value, 20);
	h.remove(h.top());
	UASSERT(!n2.inHeap());

	// Remove a node not on top
	h.remove(&n4);
	UASSERT(!n4.inHeap());

	UASSERTEQ(int, h.top()->value, 30);
	h.remove(h.top());
	UASSERT(!n3.inHeap());
	UASSERT(h.empty());
}

void TestBinHeap::testCompare() {
	// Same as testBasic, but as a max heap
	using Heap = BinHeap<int, std::greater<int> >;
	using Node = Heap::node_type;

	Heap h;
	UASSERTEQ(size_t, h.size(), 0);
	UASSERT(h.empty());

	Node n1(10);
	Node n2{20};
	Node n3{30};
	Node n4{40};

	h.insert(&n1);
	h.insert(&n3);
	h.insert(&n4);
	h.insert(&n2);
	h.validate();

	UASSERTEQ(int, h.top()->value, 40);
	h.remove(h.top());

	UASSERTEQ(int, h.top()->value, 30);
	h.remove(h.top());

	UASSERTEQ(int, h.top()->value, 20);
	h.remove(h.top());

	UASSERTEQ(int, h.top()->value, 10);
	h.remove(h.top());
}

//
// DummyBinHeap
//
// Replicates the same behavior as BinHeap, but using a simple
// implementation with poor performance.
//
template<typename T>
struct DummyBinHeapNode {
	T value;
	typename BinHeap<T>::node_type* partner;
	DummyBinHeapNode(T v) : value(v) { }
};

template<typename T>
struct ValueLessThan {
	bool operator()(DummyBinHeapNode<T> *a, DummyBinHeapNode<T> *b) {
		return a->value < b->value;
	}
};

template<typename T>
class DummyBinHeap {
public:
	using value_type = T;
	using node_type = DummyBinHeapNode<T>;

	void insert(node_type *node) {
		auto it = std::lower_bound(values.begin(), values.end(), node, ValueLessThan<T>{});
		values.insert(it, node);
	}

	void remove(node_type *node) {
		// It's sorted by values, not node. So do a linear search.
		auto it = std::find(values.begin(), values.end(), node);
		UASSERT(it != values.end());
		values.erase(it);
	}

	node_type* top() {
		if (values.empty())
			return nullptr;
		return values[0];
	}

	size_t size() const {
		return values.size();
	}

	bool empty() const {
		return values.empty();
	}

	node_type* get_at_index(size_t index) {
		return values[index];

	}
private:
	std::vector<node_type*> values; // sorted by value
};


void TestBinHeap::testFuzz()
{
	using Dummy = DummyBinHeap<int>;
	using Real = BinHeap<int>;
	std::minstd_rand rand32(time(NULL));
	Dummy dummy;
	Real real;

	size_t M = 10000;
	// Perform M random operations. Because the chance of insert/remove are the same,
	// the heap grows/shrinks like a random walk. It should stay approximately within
	// O(sqrt(M)) ~ 100 elements
	// This is important because the dummy bin heap remove is linear in the size.
	for (size_t iter = 0; iter < M; iter++) {
		real.validate();
		UASSERTEQ(size_t, dummy.size(), real.size());
		UASSERTEQ(bool, dummy.empty(), real.empty());
		if (real.size() > 0) {
			UASSERTEQ(int, dummy.top()->value, real.top()->value);
		}
		if (rand32() % 2 == 0) {
			// insert a new node with a random value
			int v = rand32() % 100;
			auto dnode = new Dummy::node_type(v);
			dnode->partner = new Real::node_type(v);
			dummy.insert(dnode);
			real.insert(dnode->partner);
		} else if (!dummy.empty()) {
			// remove a random node
			int index = rand32() % dummy.size();
			auto dnode = dummy.get_at_index(index);
			UASSERT(dnode->value == dnode->partner->value);
			real.remove(dnode->partner);
			dummy.remove(dnode);
			delete dnode->partner;
			delete dnode;
		}
	}
	real.validate();

	// Clean up
	real.clear();
	UASSERT(real.size() == 0);
	while (!dummy.empty()) {
		auto dnode = dummy.get_at_index(dummy.size() - 1);
		UASSERT(!dnode->partner->inHeap());
		delete dnode->partner;
		dummy.remove(dnode);
		delete dnode;
	}
}
