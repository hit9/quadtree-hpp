// Optimized quadtrees on grid rectangles in C++.
// https://github.com/hit9/quadtree-hpp
//
// BSD license. Chao Wang, Version: 0.4.0
//
// Coordinate conventions:
//
//           w
//    +---------------> x
//    |
// h  |
//    |
//    v
//    y
//

// changes
// ~~~~~~~
// 0.4.1: Limit query range AABB box to winth th grid for QueryRange and QueryLeafNodesInRange.
// 0.4.0: **Breaking change**: switch to ue coding style.
// 0.3.0: **Breaking change**: inverts the coordinates conventions.
// 0.2.2: Add `RemoveObjects` and `BatchAddToLeafNode`.

#ifndef HIT9_QUADTREE_HPP
#define HIT9_QUADTREE_HPP

#include <algorithm>	 // for std::max
#include <cstdint>		 // for std::uint64_t
#include <cstring>		 // for memset
#include <functional>	 // for std::function, std::hash
#include <unordered_map> // for std::unordered_map
#include <unordered_set> // for std::unordered_set
#include <vector>

namespace Quadtree
{

	// The maximum width and height of the entire rectangular region.
	const int MAX_SIDE = (1 << 29) - 1;
	// The maximum depth of a quadtree.
	const int MAX_DEPTH = 29;

	using std::uint64_t;
	using std::uint8_t;

	// NodeId is the unique identifier of a tree node, composed of:
	//
	// +----- 6bit -----+------- 29bit ----+----- 29bit -----+
	// | depth d (6bit) | floor(x*(2^d)/w) | floor(y*(2^d)/h |
	// +----------------+------------------+-----------------+
	//
	// Properties:
	// 1. Substituting this formula into any position (x,y) inside the node always give the same ID.
	// 2. The id of tree root is always 0.
	// 3. The deeper the node, the larger the id.
	// 4. For nodes at the same depth, the id changes with the size of x*h+y.
	using NodeId = uint64_t;

	// pack caculates the id of a node.
	// the d is the depth of the node, the (x,y) is any position inside the node's rectangle.
	// the w and h is the whole rectangular region managed by the quadtree.
	inline NodeId Pack(uint64_t d, uint64_t x, uint64_t y, uint64_t w, uint64_t h)
	{
		// The following magic numbers:
		// 0xfc00000000000000 : the highest 6 bits are all 1, the other bits are all 0.
		// 0x3ffffffe0000000  : the highest 6 bits are all 0, the following 29 bits are all 1,
		// and the
		//                      other bits are all 0.
		// 0x1fffffff         : the lowest 29 bits are all 1, the other bits are all 0.
		return ((d << 58) & 0xfc00000000000000ULL) | ((((1 << d) * x / w) << 29) & 0x3ffffffe0000000ULL) | (((1 << d) * y / h) & 0x1fffffffULL);
	}

	template <typename Object>
	struct ObjectKey
	{
		int	   x, y;
		Object o;
		// ObjectKey is comparable.
		bool operator==(const ObjectKey& other) const;
	};

	// Hasher for ObjectKey.
	template <typename Object, typename ObjectHasher = std::hash<Object>>
	struct ObjectKeyHasher
	{
		std::size_t operator()(const ObjectKey<Object>& k) const;
	};

	// SplitingStopper is the type of the function to check if a node should stop to split.
	// The parameters here:
	//  w (int): the node's rectangle's width.
	//  h (int): the node's rectangle's height.
	//  n (int): the number of objects managed by the node.
	// What's more, if the w and h are both 1, it stops to split anyway.
	// Examples:
	//  1. to split into small rectangles not too small (e.g. at least 10x10)
	//        [](int w, int h, int n) { return w <= 10 && h <= 10; };
	//  2. to split into small rectangles contains less than q objects (e.g. q=10)
	//        [](int w, int h, int n) { return n < 10; };
	using SplitingStopper = std::function<bool(int w, int h, int n)>;

	// SplitingStopper v2 version.
	// (x1,y1) and (x2,y2) is the node's left-top and right-bottom corners.
	// n is the number of objects managed by the node.
	// If a SplitingStopperV2 ssf2 is provided, it's considered over (instead of) ssf v1.
	using SplitingStopperV2 = std::function<bool(int x1, int y1, int x2, int y2, int n)>;

	// Objects is the container to store objects and their positions.
	// It's an unordered_set of {x, y, object} structs.
	template <typename Object, typename ObjectHasher = std::hash<Object>>
	using Objects = std::unordered_set<ObjectKey<Object>, ObjectKeyHasher<Object, ObjectHasher>>;

	// The structure of a tree node.
	template <typename Object, typename ObjectHasher = std::hash<Object>>
	struct Node
	{
		bool isLeaf;
		// d is the depth of this node in the tree, starting from 0.
		uint8_t d;
		// (x1,y1) and (x2,y2) are the upper-left and lower-right corners of the node's rectangle:
		//
		//     (x1,y1) +---------------+
		//             |               |
		//             +---------------+ (x2,y2)
		int x1, y1, x2, y2;
		// Children: 0: left-top, 1: right-top, 2: left-bottom, 3: right-bottom
		// For a leaf node, the children of which are all nullptr.
		// For a non-leaf node, there's atleast one non-nullptr child.
		//
		//       +-----+-----+
		//       |  0  |  1  |
		//       +-----+-----+
		//       |  2  |  3  |
		//       +-----+-----+
		Node* children[4];
		// For a leaf node, this container stores the objects managed by this node.
		// For a non-leaf node, this container is empty.
		// The objects container itself is an unordered_set.
		// To iterate this container, just:
		//    for (auto [x, y, o] : objects)
		//       // for each object o locates at position (x,y)
		Objects<Object, ObjectHasher> objects;

		Node(bool isLeaf, uint8_t d, int x1, int y1, int x2, int y2);
		~Node();
	};

	// Collector is the function that can collect the managed objects.
	// The arguments is (x,y,object), where the (x,y) is the position of the object.
	template <typename Object>
	using Collector = std::function<void(int, int, Object)>;

	// Visitor is the function that can access a quadtree node.
	template <typename Object, typename ObjectHasher = std::hash<Object>>
	using Visitor = std::function<void(Node<Object, ObjectHasher>*)>;

	template <typename Object>
	struct BatchOperationItem
	{
		// (x,y) is the position to add or remove object o.
		int x, y;
		// o is the object to add or remove.
		Object o;
	};

	// Quadtree on a rectangle with width w and height h, storing the objects.
	// The type parameter Object is the type of the objects to store on this tree.
	// Object is required to be comparable (the operator== must be available).
	// e.g. Quadtree<int>, Quadtree<Entity*>, Quadtree<void*>
	// We store the objects into an unordered_set along with its position,
	// the hashing of a object is handled by the second type parameter ObjectHasher.
	template <typename Object, typename ObjectHasher = std::hash<Object>>
	class Quadtree
	{
	public:
		using NodeT = Node<Object, ObjectHasher>;
		using CollectorT = Collector<Object>;
		using VisitorT = Visitor<Object, ObjectHasher>;
		using ObjectsT = Objects<Object, ObjectHasher>;
		using BatchOperationItemT = BatchOperationItem<Object>;

		Quadtree(int w, int h,							// width and height of the whole region.
			SplitingStopper ssf = nullptr,				// function to stop node spliting
			VisitorT		afterLeafCreated = nullptr, // callback to be called after leaf nodes created.
			VisitorT		afterLeafRemoved = nullptr	// callback to be called after leaf nodes removed.
		);
		~Quadtree();

		// Returns the depth of the tree, starting from 0.
		uint8_t Depth() const { return maxd; }

		// Returns the total number of objects managed by this tree.
		int NumObjects() const { return numObjects; }

		// Returns the number of nodes in this tree.
		int NumNodes() const { return m.size(); }

		// Returns the number of leaf nodes in this tree.
		int NumLeafNodes() const { return numLeafNodes; }

		// Sets the ssf function later after construction.
		void SetSsf(SplitingStopper f) { ssf = f; }

		// Sets the ssf v2 function later after construction.
		void SetSsfV2(SplitingStopperV2 f) { ssfv2 = f; }

		// Sets the callback functions later after construction.
		void SetAfterLeafCreatedCallback(VisitorT cb) { afterLeafCreated = cb; }

		// Notes that the node is already freed, don't access the memory this node pointing to.
		void SetAfterLeafRemovedCallback(VisitorT cb) { afterLeafRemoved = cb; }

		// Returns the root node.
		NodeT* GetRootNode() { return root; }

		// Build all nodes recursively on an empty quadtree.
		// This build function must be called on an **empty** quadtree,
		// where the word "empty" means that there's no nodes inside this tree.
		void Build();

		// Find the leaf node managing given position (x,y).
		// If the given position crosses the bound, returns nullptr.
		// We use binary-search for optimization, the time complexity is O(log Depth).
		NodeT* Find(int x, int y) const;

		// Add a object located at position (x,y) to the right leaf node.
		// And split down if the node is able to continue the spliting after the insertion.
		// Or merge up if the node's parent is able to be a leaf node instead.
		// At most one of "potential spliting and merge" will happen.
		// Does nothing if the given position is out of boundary.
		// Dose nothing if this object already exist at given position.
		void Add(int x, int y, Object o);

		// Remove the managed object located at position (x,y).
		// And then try to merge the corresponding leaf node with its brothers, if possible.
		// Or try to split down if the node's parent is able to be a leaf node itself.
		// At most one of "potential spliting and merge" will happen.
		// Does nothing if the given position crosses the boundary.
		// Dose nothing if this object dose not exist at given position.
		void Remove(int x, int y, Object o);

		// RemoveObjects remove all objects located at position (x,y).
		// And then try to merge or split to maintain the structure of the quadtree.
		// the behaviour is similar to method Remove().
		// Does nothing if the given position crosses the boundary.
		// Dose nothing if this object dose not exist at given position.
		void RemoveObjects(int x, int y);

		// Query the objects inside given rectangular range, the given collector will be called
		// for each object hits.
		//
		// The parameters (x1,y1) and (x2,y2) are the left-top and right-bottom corners of the given
		// rectangle.
		// Does nothing if x1 <= x2 && y1 <= y2 is not satisfied.
		// We will limit the query range to within the valid grid.
		//
		// We first locate the smallest node that encloses the given rectangular range, and then query
		// its descendant leaf nodes overlapping with this range recursively, and finally collects the
		// objects inside this range from these leaf nodes. Time complexity: O(log D + N), where N is the
		// number of nodes under the node found, which is worst to be the total tree's nodes.
		void QueryRange(int x1, int y1, int x2, int y2, CollectorT& collector) const;
		void QueryRange(int x1, int y1, int x2, int y2, CollectorT&& collector) const;

		// Quert the leaf nodes overlapping with  given rectangular range, the given visitor will be
		// called for each leaf nodes hits. The parameters (x1,y1) and (x2,y2) are the left-top and
		// right-bottom corners of the given rectangle.
		//
		// Does nothing if x1 <= x2 && y1 <= y2 is not satisfied. The internal implementation is the same
		// to QueryRange.
		//
		// We will limit the query range to within the valid grid.
		void QueryLeafNodesInRange(int x1, int y1, int x2, int y2, VisitorT& collector) const;
		void QueryLeafNodesInRange(int x1, int y1, int x2, int y2, VisitorT&& collector) const;

		// Find the smallest node enclosing the given rectangular query range.
		// (x1,y1) and (x2,y2) are the left-top and right-bottom corners of the query range.
		// Returns nullptr if any axis of the two corners is out-of-boundary.
		// The time complexity is O(log D), where D is the depth of the tree.
		NodeT* FindSmallestNodeCoveringRange(int x1, int y1, int x2, int y2) const;

		// Find all neighbours leaf nodes for given node at one direction.
		// The meaning of 8 direction integers:
		//
		//        4| 0(N)| 5
		//       --+-----+--
		//     3(W)|     | 1(E)
		//       --+-----+--
		//        7| 2(S)| 6
		// Properties of the directions:
		//    1. the opposite direction is direction XOR 2 .
		// For diagonal directions(4,5,6,7), it simply returns the single diagonal leaf neighbour.
		// For non-diagonal directions (0,1,2,3), there're two steps.
		// Explaination for direction=0 (North), supposing the depth of given node is d:
		// 1. Take 2 neighbour positions p1(x1,y1-1), p2(x1,y2-1) find the smallest node containing p1
		//    and p2. This node's size should be equal or greater than current node.
		//    This step could be done by a binary-search in time complexity O(log Depth).
		// 2. Find the sourth children(No. 2,3) downward recursively from the node found in step1, until
		//    we reach all the most sourth leaf nodes, here are the answers.
		void FindNeighbourLeafNodes(NodeT* node, int direction, VisitorT& visitor) const;

		// Traverse all nodes in this tree.
		// The order is unstable between two traverses since we are traversing a cache hashtable of all
		// nodes actually.
		// To traverse only the leaf nodes, you may filter by the `node->isLeaf` attribute.
		void ForEachNode(VisitorT& visitor) const;

		// ForceSyncLeafNode is a low-level interface, please use it with caution.
		// The design purpose for it: in case our ssf function depends more than objects adding and
		// removing. If some changes happen at places other than the objects locating areas, we may force
		// sync the leaf nodes to maintain the potential splitings and mergings.
		void ForceSyncLeafNode(NodeT* leafNode);

		// BatchAddToLeafNode adds multiple objects into the given leaf node.
		// The caller should guarantee:
		//
		// 1. the given leafNode is indeed a leaf node, doing nothing if this is not satisfied.
		// 2. each given item's (x,y) is inside the leaf node, skipping if this is not satisfied.
		//
		// There's a typical scenario: if the map already contains some objects, we can use this api to
		// initialize the quadtree. This is faster than adding the object one by one. We can just call
		// something like: BatchAddToLeafNode(GetRootNode(), allObjectItems).
		void BatchAddToLeafNode(NodeT* leafNode, const std::vector<BatchOperationItemT>& items);

	private:
		NodeT* root = nullptr;
		// width and height of the whole region.
		const int w, h;
		// maxd is the current maximum depth.
		uint8_t maxd = 0;
		// numDepthTable records how many nodes reaches every depth.
		int numDepthTable[MAX_DEPTH];
		// the number of objects in this tree.
		int numObjects = 0;
		// the number of leaf nodes in this tree.
		int numLeafNodes = 0;
		// the function to test if a node should stop to split.
		SplitingStopper ssf = nullptr;
		// ssfv2 takes higher priority than ssf v1.
		// if ssfv2 is not nullptr, ssf v1 won't be used anymore.
		SplitingStopperV2 ssfv2 = nullptr;
		// cache the mappings between id and the node.
		std::unordered_map<NodeId, NodeT*> m;
		// callback functions
		VisitorT afterLeafCreated = nullptr, afterLeafRemoved = nullptr;

		// ~~~~~~~~~~~ Internals ~~~~~~~~~~~~~
		using NodeSet = std::unordered_set<NodeT*>;
		NodeT* ParentOf(NodeT* node) const;
		bool   IsSplitable(int x1, int y1, int x2, int y2, int n) const;
		NodeT* CreateNode(bool isLeaf, uint8_t d, int x1, int y1, int x2, int y2);
		void   RemoveLeafNode(NodeT* node);
		bool   TrySplitDown(NodeT* node);
		bool   TryMergeUp(NodeT* node);
		NodeT* SplitHelper1(uint8_t d, int x1, int y1, int x2, int y2, ObjectsT& upstreamObjects,
			NodeSet& createdLeafNodes);
		void   SplitHelper2(NodeT* node, NodeSet& createdLeafNodes);
		bool   IsMergeable(NodeT* node, NodeT*& parent) const;
		NodeT* MergeHelper(NodeT* node, NodeSet& removedLeafNodes);
		void   QueryRange(NodeT* node, CollectorT& objectsCollector, VisitorT& nodeVisitor, int x1, int y1,
			  int x2, int y2) const;
		NodeT* FindSmallestNodeCoveringRangeHelper(int x1, int y1, int x2, int y2, int dma) const;
		// ~~~~~~~~~~~~~ Internals::FindNeighbourLeafNodes ~~~~~~~~~~~~
		void FindNeighbourLeafNodesDiagonal(NodeT* node, int direction, VisitorT& visitor) const;
		void FindNeighbourLeafNodesHV(NodeT* node, int direction, VisitorT& visitor) const;
		void GetNeighbourPositionDiagonal(NodeT* node, int direction, int& px, int& py) const;
		void GetNeighbourPositionsHV(NodeT* node, int direction, int& px1, int& py1, int& px2,
			int& py2) const;
		void GetLeafNodesAtDirection(NodeT* node, int direction, VisitorT& visitor) const;
	};

	// ~~~~~~~~~~~ Implementation ~~~~~~~~~~~~~

	template <typename Object>
	bool ObjectKey<Object>::operator==(const ObjectKey& other) const
	{
		return x == other.x && y == other.y && o == other.o;
	}

	const std::size_t __FNV_BASE = 14695981039346656037ULL;
	const std::size_t __FNV_PRIME = 1099511628211ULL;

	template <typename Object, typename ObjectHasher>
	std::size_t ObjectKeyHasher<Object, ObjectHasher>::operator()(const ObjectKey<Object>& k) const
	{
		// pack x and y into a single uint64_t integer.
		uint64_t a = ((k.x << 29) & 0x3ffffffe0000000) | (k.y & 0x1fffffff);
		// hash the object to a size_t integer.
		std::size_t b = ObjectHasher{}(k.o);
		// combine them via FNV hash.
		std::size_t h = __FNV_BASE;
		h ^= a;
		h *= __FNV_PRIME;
		h ^= b;
		h *= __FNV_PRIME;
		return h;
	}

	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>::Node(bool isLeaf, uint8_t d, int x1, int y1, int x2, int y2)
		: isLeaf(isLeaf), d(d), x1(x1), y1(y1), x2(x2), y2(y2)
	{
		memset(children, 0, sizeof children);
	}

	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>::~Node()
	{
		for (int i = 0; i < 4; i++)
		{
			if (children[i] != nullptr)
			{
				delete children[i];
				children[i] = nullptr;
			}
		}
	}

	// Constructs a quadtree.
	// Where w and h is the width and height of the whole rectangular region.
	// ssf is the function to determine whether to stop split a leaf node.
	// The afterLeafCreated is a callback function to be called after a leaf node is created, or after
	// a non-leaf node turns to a leaf node.
	// The afterLeafRemoved is a callback function to be called
	// after a leaf node is removed or a leaf node turns to a non-leaf node.
	// It's important to note that the node passed to afterLeafRemoved is **already** freed, so don't
	// access the memory it pointing to. And notes that the afterLeafRemoved won't be called on the
	// whole quadtree's destruction.
	template <typename Object, typename ObjectHasher>
	Quadtree<Object, ObjectHasher>::Quadtree(int w, int h, SplitingStopper ssf,
		VisitorT afterLeafCreated, VisitorT afterLeafRemoved)
		: w(w), h(h), ssf(ssf), afterLeafCreated(afterLeafCreated), afterLeafRemoved(afterLeafRemoved)
	{
		memset(numDepthTable, 0, sizeof numDepthTable);
	}

	template <typename Object, typename ObjectHasher>
	Quadtree<Object, ObjectHasher>::~Quadtree()
	{
		m.clear();
		delete root;
		root = nullptr;
		memset(numDepthTable, 0, sizeof numDepthTable);
		maxd = 0, numLeafNodes = 0, numObjects = 0;
	}

	// Returns the parent of given non-root node.
	// The node passed in here should guarantee not be root.
	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>* Quadtree<Object, ObjectHasher>::ParentOf(NodeT* node) const
	{
		if (node == nullptr || node == root || node->d == 0)
			return nullptr;
		return m.at(Pack(node->d - 1, node->x1, node->y1, w, h));
	}

	// Indicates whether given rectangle with n number of objects inside it is splitable.
	// Return true meaning that this rectangle should be managed by a leaf node.
	// Otherwise returns false meaning it should be managed by a non-leaf node.
	template <typename Object, typename ObjectHasher>
	bool Quadtree<Object, ObjectHasher>::IsSplitable(int x1, int y1, int x2, int y2, int n) const
	{
		// We can't split if it's a single cell.
		if (x1 == x2 && y1 == y2)
			return false;
		// We can't split if there's a ssf function stops it.
		// if ssfv2 is provided not nullptr, we use only ssfv2 instead of ssf v1.
		if (ssfv2 != nullptr)
		{
			if (ssfv2(x1, y1, x2, y2, n))
				return false;
			return true;
		}
		// ssf v1
		if (ssf != nullptr && ssf(x2 - x1 + 1, y2 - y1 + 1, n))
			return false;
		return true;
	}

	// createNode is a simple function to create a new node and add to the global node table.
	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>* Quadtree<Object, ObjectHasher>::CreateNode(bool isLeaf, uint8_t d,
		int x1, int y1, int x2,
		int y2)
	{
		auto id = Pack(d, x1, y1, w, h);
		auto node = new NodeT(isLeaf, d, x1, y1, x2, y2);
		m.insert({ id, node });
		if (isLeaf)
			++numLeafNodes;
		// maintains the max depth.
		maxd = std::max(maxd, d);
		++numDepthTable[d];
		return node;
	}

	// removeLeafNode is a simple function to delete a leaf node and maintain the tree informations.
	// Does nothing if the given node is not a leaf node.
	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::RemoveLeafNode(NodeT* node)
	{
		if (!node->isLeaf)
			return;
		auto id = Pack(node->d, node->x1, node->y1, w, h);
		// Remove from the global table.
		m.erase(id);
		// maintains the max depth.
		--numDepthTable[node->d];
		if (node->d == maxd)
		{
			// If we are removing a leaf node at the largest depth
			// We need to maintain the maxd, it may change.
			// the iterations will certainly smaller than MAX_DEPTH (29);
			while (numDepthTable[maxd] == 0)
				--maxd;
		}
		// Finally delete this node.
		delete node;
		--numLeafNodes;
	}

	// splitHelper1 helps to create nodes recursively until all descendant nodes are not able to split.
	// The d is the depth of the node to create.
	// The (x1,y1) and (x2, y2) is the upper-left and lower-right corners of the node to create.
	// The upstreamObjects is from the upstream node, we should filter the ones inside the
	// rectangle (x1,y1,x2,y2) if this node is going to be a leaf node.
	// The createdLeafNodes is to collect created leaf nodes.
	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>* Quadtree<Object, ObjectHasher>::SplitHelper1(
		uint8_t d, int x1, int y1, int x2, int y2, ObjectsT& upstreamObjects,
		NodeSet& createdLeafNodes)
	{
		// boundary checks.
		if (!(x1 >= 0 && x1 < w && y1 >= 0 && y1 < h))
			return nullptr;
		if (!(x2 >= 0 && x2 < w && y2 >= 0 && y2 < h))
			return nullptr;
		if (!(x1 <= x2 && y1 <= y2))
			return nullptr;
		// steal objects inside this rectangle from upstream.
		ObjectsT objs;
		for (const auto& k : upstreamObjects)
		{
			auto [x, y, o] = k;
			if (x >= x1 && x <= x2 && y >= y1 && y <= y2)
			{
				objs.insert(k);
			}
		}
		// Erase from upstreamObjects.
		// An object should always go to only one branch.
		for (const auto& k : objs)
			upstreamObjects.erase(k);
		// Creates a leaf node if the rectangle is not able to split any more.
		if (!IsSplitable(x1, y1, x2, y2, objs.size()))
		{
			auto node = CreateNode(true, d, x1, y1, x2, y2);
			node->objects.swap(objs);
			createdLeafNodes.insert(node);
			return node;
		}
		// Creates a non-leaf node if the rectangle is able to split,
		// and then continue to split down recursively.
		auto node = CreateNode(false, d, x1, y1, x2, y2);
		// Add the objects to this node temply, it will finally be stealed
		// by the its descendant leaf nodes.
		node->objects.swap(objs);
		SplitHelper2(node, createdLeafNodes);
		return node;
	}

	// splitHelper2 helps to split given node into 4 children.
	// The node passing in should guarantee:
	// 1. it's a leaf node (with empty children).
	// 2. or it's marked to be a non-leaf node (with empty children).
	// After this call, the node always turns into a non-leaf node.
	// The createdLeafNodes is to collect created leaf nodes.
	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::SplitHelper2(NodeT* node, NodeSet& createdLeafNodes)
	{
		auto x1 = node->x1, y1 = node->y1;
		auto x2 = node->x2, y2 = node->y2;
		auto d = node->d;
		// the following (x3,y3) is the middle point*:
		//
		//     x1    x3       x2
		//  y1 -+------+------+-
		//      |  0   |  1   |
		//  y3  |    * |      |
		//     -+------+------+-
		//      |  2   |  3   |
		//      |      |      |
		//  y2 -+------+------+-
		int x3 = x1 + (x2 - x1) / 2, y3 = y1 + (y2 - y1) / 2;

		// determines which side each axis x3 and y3 belongs, take x axis for instance:
		// by default, we assume x3 belongs to the left side.
		// but if the ids of x1 and x3 are going to dismatch, which means the x3 should belong to the
		// right side, that is we should minus x3 by 1.
		// And minus by 1 should be enough, because x3-2 always equals to x3-4, x3-8,.. until x1.
		// Potential optimization: how to avoid the division here?
		uint64_t k = 1 << (d + 1);
		if ((k * x3 / w) != (k * x1 / w))
			--x3;
		if ((k * y3 / h) != (k * y1 / h))
			--y3;

		node->children[0] = SplitHelper1(d + 1, x1, y1, x3, y3, node->objects, createdLeafNodes);
		node->children[1] = SplitHelper1(d + 1, x3 + 1, y1, x2, y3, node->objects, createdLeafNodes);
		node->children[2] = SplitHelper1(d + 1, x1, y3 + 1, x3, y2, node->objects, createdLeafNodes);
		node->children[3] = SplitHelper1(d + 1, x3 + 1, y3 + 1, x2, y2, node->objects, createdLeafNodes);

		// anyway, it's not a leaf node any more.
		if (node->isLeaf)
		{
			--numLeafNodes;
			node->isLeaf = false;
		}
	}

	// try to split given leaf node if possible.
	// Returns true if the spliting happens.
	template <typename Object, typename ObjectHasher>
	bool Quadtree<Object, ObjectHasher>::TrySplitDown(NodeT* node)
	{
		if (node->isLeaf && IsSplitable(node->x1, node->y1, node->x2, node->y2, node->objects.size()))
		{
			// The createdLeafNodes is to collect created leaf nodes.
			NodeSet createdLeafNodes;
			SplitHelper2(node, createdLeafNodes);

			// The node itself should turn to be a non-leaf node.
			if (afterLeafRemoved != nullptr)
				afterLeafRemoved(node);
			// call hook function for each created leaf nodes.
			if (afterLeafCreated != nullptr)
			{
				for (auto createdNode : createdLeafNodes)
					afterLeafCreated(createdNode);
			}

			return true;
		}
		return false;
	}

	// Returns true if given node is mergeable with its brothers.
	// And sets the passed-in parent pointer.
	template <typename Object, typename ObjectHasher>
	bool Quadtree<Object, ObjectHasher>::IsMergeable(NodeT* node, NodeT*& parent) const
	{
		// The root stops to merge up.
		if (node == root)
			return false;
		// We can only merge from the leaf node.
		if (!node->isLeaf)
			return false;
		// The parent of this leaf node.
		parent = ParentOf(node);
		if (parent == nullptr)
			return false;
		// We can only merge if all the brothers are all leaf nodes.
		for (int i = 0; i < 4; i++)
		{
			auto child = parent->children[i];
			if (child != nullptr && !child->isLeaf)
			{
				return false;
			}
		}
		// Count the objects inside the parent.
		int n = 0;
		for (int i = 0; i < 4; i++)
		{
			auto child = parent->children[i];
			if (child != nullptr)
			{
				n += child->objects.size();
			}
		}

		// Check if the parent node should be a leaf node now.
		// If it's still splitable, then it should stay be a non-leaf node.
		if (IsSplitable(parent->x1, parent->y1, parent->x2, parent->y2, n))
			return false;
		return true;
	}

	// helps to merge up given node with its brothers into its parent recursively.
	// Returns an ancestor node (or itself) that stops to merge, aka the final leaf node after all
	// merging done. The removedLeafNodes collects the original leaf nodes that were removed during
	// this round.
	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>* Quadtree<Object, ObjectHasher>::MergeHelper(
		NodeT* node, NodeSet& removedLeafNodes)
	{
		NodeT* parent;
		if (!IsMergeable(node, parent))
			return node;

		// Merges the managed objects up into the parent's objects.
		for (int i = 0; i < 4; i++)
		{
			auto child = parent->children[i];
			if (child != nullptr)
			{
				for (const auto& k : child->objects)
				{
					parent->objects.insert(k); // copy
				}
				RemoveLeafNode(child);
				removedLeafNodes.insert(child);
				parent->children[i] = nullptr;
			}
		}
		// this parent node now turns to be leaf node.
		parent->isLeaf = true;
		++numLeafNodes;
		// Continue the merging to the parent, until the root or some parent is splitable.
		auto rt = MergeHelper(parent, removedLeafNodes);
		// the parent itself is not a leaf node originally.
		// so we should ensure it doesn' exist in removedLeafNodes.
		// since the parent node may be added to removedLeafNodes in the mergeHelper(parent) call above.
		removedLeafNodes.erase(parent);
		return rt;
	}

	// A non-leaf node should stay in a not-splitable state, if given leaf node's parent turns to
	// not-splitable, we merge the node with its brothers into this parent.
	// Does nothing if this node itself is the root.
	// Does nothing if this node itself is not a leaf node.
	// Returns true if the merging happens.
	template <typename Object, typename ObjectHasher>
	bool Quadtree<Object, ObjectHasher>::TryMergeUp(NodeT* node)
	{
		NodeSet removedLeafNodes;
		auto	ancestor = MergeHelper(node, removedLeafNodes);
		if (ancestor != node)
		{
			// the node should be disapeared, merged into the ancestor
			if (afterLeafRemoved != nullptr)
			{
				for (auto removedNode : removedLeafNodes)
					afterLeafRemoved(removedNode);
			}
			// The ancestor node is the new leaf node.
			if (afterLeafCreated != nullptr)
				afterLeafCreated(ancestor);
			return true;
		}
		return false;
	}

	// AABB overlap testing.
	// Check if rectangle ((ax1, ay1), (ax2, ay2)) and ((bx1,by1), (bx2, by2)) overlaps.
	inline bool isOverlap(int ax1, int ay1, int ax2, int ay2, int bx1, int by1, int bx2, int by2)
	{
		//  (ax1,ay1)
		//      +--------------+
		//   A  |    (bx1,by1) |
		//      |       +------|-------+
		//      +-------+------+       |   B
		//              |    (ax2,ay2) |
		//              +--------------+ (bx2, by2)
		//
		// Ref: https://silentmatt.com/rectangle-intersection/
		//
		// ay1 < by2 => A's left boundary is above B's bottom boundary.
		// ay2 > by1 => A's bottom boundary is below B's upper boundary.
		//
		//                ***********  B's upper                      A's upper    -----------
		//   A's upper    -----------                       OR                     ***********  B's upper
		//                ***********  B's bottom                     A's bottom   -----------
		//   A's bottom   -----------                                              ***********  B's bottom
		//
		// ax1 < bx2 => A's left boundary is on the left of B's right boundary.
		// ax2 > bx1 => A's right boundary is on the right of B's left boundary.
		//
		//           A's left         A's right                  A's left        A's right
		//
		//      *       |       *       |              OR           |       *       |        *
		//      *       |       *       |                           |       *       |        *
		//      *       |       *       |                           |       *       |        *
		//  B's left        B's right                                    B's left          B's right
		//
		// We can also see that, swapping the roles of A and B, the formula remains unchanged.
		//
		//
		// And we are processing overlapping on integeral coordinates, the (x1,y1) and (x2,y2) are
		// considered inside the rectangle. so we use <= and >= instead of < and >.
		return ax1 <= bx2 && ax2 >= bx1 && ay1 <= by2 && ay2 >= by1;
	}

	// Query objects or leaf nodes located in given rectangle in the given node.
	// the objectsCollector and nodeVisitor is optional
	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::QueryRange(NodeT* node, CollectorT& objectsCollector,
		VisitorT& nodeVisitor, int x1, int y1, int x2,
		int y2) const
	{
		if (node == nullptr)
			return;
		// AABB overlap test.
		if (!isOverlap(node->x1, node->y1, node->x2, node->y2, x1, y1, x2, y2))
			return;

		if (!node->isLeaf)
		{
			// recursively down to the children.
			for (int i = 0; i < 4; i++)
			{
				QueryRange(node->children[i], objectsCollector, nodeVisitor, x1, y1, x2, y2);
			}
			return;
		}
		// Visit leaf node if provided.
		if (nodeVisitor != nullptr)
			nodeVisitor(node);
		// Visit objects if provided.
		if (objectsCollector != nullptr)
		{
			// Collects objects inside the rectangle for this leaf node.
			for (auto [x, y, o] : node->objects)
				if (x >= x1 && x <= x2 && y >= y1 && y <= y2)
				{
					objectsCollector(x, y, o);
				}
		}
	}

	// Using binary search to guess the depth of the target node.
	// Reason: the id = (d, x*2^d/w, y*2^d/h), it's the same for all (x,y) inside the same
	// node. If id(d,x,y) is not found in the map m, the guessed depth is too large, we should
	// shrink the upper bound. Otherwise, if we found a node, but it's not a leaf, the answer
	// is too small, we should make the lower bound larger, And finally, if we found a leaf node,
	// it's the correct answer. The time complexity is O(log maxd), where maxd is the depth of
	// this whole tree.
	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>* Quadtree<Object, ObjectHasher>::Find(int x, int y) const
	{
		int l = 0, r = maxd;
		while (l <= r)
		{
			// note: use int instead of uint8_t
			int	 d = (l + r) >> 1;
			auto id = Pack(d, x, y, w, h);
			auto it = m.find(id);
			if (it == m.end())
			{ // too large
				r = d - 1;
			}
			else
			{
				auto node = it->second;
				if (node->isLeaf)
					return node;
				l = d + 1; // too small
			}
		}
		return nullptr;
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::Add(int x, int y, Object o)
	{
		// boundary checks.
		if (!(x >= 0 && x < w && y >= 0 && y < h))
			return;
		// find the leaf node.
		auto node = Find(x, y);
		if (node == nullptr)
			return;
		// add the object to this leaf node.
		auto [_, inserted] = node->objects.insert({ x, y, o });
		if (inserted)
		{
			++numObjects;
			// At most only one of "split and merge" will be performed.
			TrySplitDown(node) || TryMergeUp(node);
		}
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::Remove(int x, int y, Object o)
	{
		// boundary checks.
		if (!(x >= 0 && x < w && y >= 0 && y < h))
			return;
		// find the leaf node.
		auto node = Find(x, y);
		if (node == nullptr)
			return;
		// remove the object from this node.
		if (node->objects.erase({ x, y, o }) > 0)
		{
			--numObjects;
			// At most only one of "split and merge" will be performed.
			TryMergeUp(node) || TrySplitDown(node);
		}
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::RemoveObjects(int x, int y)
	{
		// boundary checks.
		if (!(x >= 0 && x < w && y >= 0 && y < h))
			return;
		// find the leaf node.
		auto node = Find(x, y);
		if (node == nullptr)
			return;
		int size = node->objects.size();
		node->objects.clear();
		if (size)
		{
			numObjects -= size;
			// At most only one of "split and merge" will be performed.
			TryMergeUp(node) || TrySplitDown(node);
		}
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::Build()
	{
		root = CreateNode(true, 0, 0, 0, w - 1, h - 1);
		if (!TrySplitDown(root))
		{
			// If the root is not splited, it's finally a new-created leaf node.
			if (afterLeafCreated != nullptr)
				afterLeafCreated(root);
		}
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::ForEachNode(VisitorT& visitor) const
	{
		for (auto [id, node] : m)
			visitor(node);
	}

	// Using binary search to guess the smallest node that contains the given rectangle range.
	// The key is to guess a largest depth d, where the id(d,x1,y1) and id(d,x2,y2) got the same node.
	// The dma is the max value of the depth to guess.
	// Returns nullptr if any one of x1,y1,x2,y2 crosses the boundary.
	template <typename Object, typename ObjectKeyHasher>
	Node<Object, ObjectKeyHasher>* Quadtree<Object, ObjectKeyHasher>::FindSmallestNodeCoveringRangeHelper(
		int x1, int y1, int x2, int y2, int dma) const
	{
		// boundary checks
		if (!(x1 >= 0 && x1 < w && y1 >= 0 && y1 < h))
			return nullptr;
		if (!(x2 >= 0 && x2 < w && y2 >= 0 && y2 < h))
			return nullptr;
		// Find the target
		int	   l = 0, r = dma;
		NodeT* node = root;
		while (l < r)
		{
			// note: use int instead of uint8_t
			int	 d = (l + r + 1) >> 1;
			auto id1 = Pack(d, x1, y1, w, h);
			auto id2 = Pack(d, x2, y2, w, h);
			// We should track the largest d and corresponding node that satisfies both:
			// id1==id2 and the node at this id exists.
			if (id1 == id2)
			{
				auto it = m.find(id1);
				if (it != m.end())
				{
					l = d;
					node = it->second;
					continue;
				}
			}
			// Otherwise, the d is too large, skip this answer.
			// Makes the upper bound smaller.
			r = d - 1;
		}
		return node;
	}

	template <typename Object, typename ObjectHasher>
	Node<Object, ObjectHasher>* Quadtree<Object, ObjectHasher>::FindSmallestNodeCoveringRange(
		int x1, int y1, int x2, int y2) const
	{
		return FindSmallestNodeCoveringRangeHelper(x1, y1, x2, y2, maxd);
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::QueryRange(int x1, int y1, int x2, int y2,
		CollectorT& collector) const
	{
		if (!(x1 <= x2 && y1 <= y2))
			return;

		x1 = std::max(x1, 0), y1 = std::max(y1, 0);
		x2 = std::min(x2, w - 1), y2 = std::min(y2, h - 1);

		auto node = FindSmallestNodeCoveringRange(x1, y1, x2, y2);
		if (node == nullptr)
			node = root;
		VisitorT nodeVisitor = nullptr;
		QueryRange(node, collector, nodeVisitor, x1, y1, x2, y2);
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::QueryRange(int x1, int y1, int x2, int y2,
		CollectorT&& collector) const
	{
		QueryRange(x1, y1, x2, y2, collector);
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::QueryLeafNodesInRange(int x1, int y1, int x2, int y2,
		VisitorT& collector) const
	{
		if (!(x1 <= x2 && y1 <= y2))
			return;

		x1 = std::max(x1, 0), y1 = std::max(y1, 0);
		x2 = std::min(x2, w - 1), y2 = std::min(y2, h - 1);

		auto node = FindSmallestNodeCoveringRange(x1, y1, x2, y2);
		if (node == nullptr)
			node = root;
		CollectorT objectsCollector = nullptr;
		QueryRange(node, objectsCollector, collector, x1, y1, x2, y2);
	}

	template <typename Object, typename ObjectHasher>
	void Quadtree<Object, ObjectHasher>::QueryLeafNodesInRange(int x1, int y1, int x2, int y2,
		VisitorT&& collector) const
	{
		QueryLeafNodesInRange(x1, y1, x2, y2, collector);
	}

	// Get the neighbour position (px,py) on given diagonal direction of given node.
	// The a,b,c,d is the target neighbour position for each direction:
	//
	//         x1    x2
	//     4  a|     |b   5
	//       --+-----+--    y1
	//         |     |
	//       --+-----+--    y2
	//     7  d|     |c   6
	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::GetNeighbourPositionDiagonal(NodeT* node, int direction,
		int& px, int& py) const
	{
		int x1 = node->x1, y1 = node->y1, x2 = node->x2, y2 = node->y2;
		switch (direction)
		{
			case 4: // a
				px = x1 - 1, py = y1 - 1;
				return;
			case 5: // b
				px = x2 + 1, py = y1 - 1;
				return;
			case 6: // c
				px = x2 + 1, py = y2 + 1;
				return;
			case 7: // d
				px = x1 - 1, py = y2 + 1;
				return;
		}
	}

	// FindNeighbourLeafNodes for the diagonal directions.
	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::FindNeighbourLeafNodesDiagonal(NodeT* node, int direction,
		VisitorT& visitor) const
	{
		// neighbour position
		int px = -1, py = -1;
		GetNeighbourPositionDiagonal(node, direction, px, py);
		// find the neighbour leaf containing (px,py)
		auto neighbour = Find(px, py);
		if (neighbour != nullptr)
			visitor(neighbour);
	}

	// Get the neighbour positions (px1,py1) and (px2,py2) on given non-diagonal direction (NEWS) of
	// given node.
	// The ab,cd,ef,gh are the target neighbour positions at each direction:
	//
	//            N:0
	//         x1    x2
	//         |     |
	//         a     b
	//       -g+-----+c-    y1
	//   W:3   |     |        E:1
	//         |     |
	//       -h+-----+d-    y2
	//         e     f
	//         |     |
	//           S:2
	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::GetNeighbourPositionsHV(NodeT* node, int direction,
		int& px1, int& py1, int& px2,
		int& py2) const
	{
		int x1 = node->x1, y1 = node->y1, x2 = node->x2, y2 = node->y2;
		switch (direction)
		{
			case 0:						// N
				px1 = x1, py1 = y1 - 1; // a
				px2 = x2, py2 = y1 - 1; // b
				return;
			case 1:						// E
				px1 = x2 + 1, py1 = y1; // c
				px2 = x2 + 1, py2 = y2; // d
				return;
			case 2:						// S
				px1 = x1, py1 = y2 + 1; // e
				px2 = x2, py2 = y2 + 1; // f
				return;
			case 3:						// W
				px1 = x1 - 1, py1 = y1; // g
				px2 = x1 - 1, py2 = y2; // h
				return;
		}
	}

	// FindNeighbourLeafNodes for non-diagonal directions.
	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::FindNeighbourLeafNodesHV(NodeT* node, int direction,
		VisitorT& visitor) const
	{
		// neighbour positions
		int px1 = -1, py1 = -1, px2 = -1, py2 = -1;
		GetNeighbourPositionsHV(node, direction, px1, py1, px2, py2);
		// find the smallest neighbour node contains this region (px1,py1),(px2,py2)
		auto p = FindSmallestNodeCoveringRangeHelper(px1, py1, px2, py2, node->d);
		if (p == nullptr)
			return;
		// find all leaf nodes inside p locating on the opposite direction.
		GetLeafNodesAtDirection(p, direction ^ 2, visitor);
	}

	// Jump table for: [flag][direction] => {children index (-1 for invalid)}
	// Checkout the document of function getLeafNodesAtDirection for the flag's meaning.
	const int GET_LEAF_NODES_AT_DIRECTION_JUMP_TABLE[10][4][2] = {
		// 0:N       1:E        2:S       3:W
		{ { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } }, // 0, 0b0000 leaf node
		{ { 0, -1 }, { 0, -1 }, { 0, -1 }, { 0, -1 } },		// 1, 0b0001 (0---), single grid
		{ { 1, -1 }, { 1, -1 }, { 1, -1 }, { 1, -1 } },		// 2, 0b0010 (-1--), single grid
		{ { 2, -1 }, { 2, -1 }, { 2, -1 }, { 2, -1 } },		// 3, 0b0100 (--2-), single grid
		{ { 3, -1 }, { 3, -1 }, { 3, -1 }, { 3, -1 } },		// 4, 0b1000 (---3), single grid
		{ { 0, 1 }, { -1, 1 }, { 0, 1 }, { 0, -1 } },		// 5, 0b0011 (01--) horizonal 1x2 grids, [ 0 | 1 ]
		{ { 2, 3 }, { -1, 3 }, { 2, 3 }, { 2, -1 } },		// 6, 0b1100 (--23) horizonal 1x2 grids, [ 2 | 3 ]
		{ { 0, -1 }, { 0, 2 }, { -1, 2 }, { 0, 2 } },		// 7, 0b0101 (0-2-) vertical 2x1 grids [ 0 ]
															//                                     [ 2 ]
		{ { 1, -1 }, { 1, 3 }, { -1, 3 }, { 1, 3 } },		// 8, 0b1010 (-1-3) vertical 2x1 grids [ 1 ]
															//                                     [ 3 ]
		{ { 0, 1 }, { 1, 3 }, { 2, 3 }, { 0, 2 } },			// 9, 0b1111, 4 grids
	};

	const int GET_LEAF_NODES_AT_DIRECTION_MASK_TO_FLAG_TABLE[16] = {
		0,	// 0 => 0b0000
		1,	// 1 => 0b0001
		2,	// 2 => 0b0010
		5,	// 3 => 0b0011
		3,	// 4 => 0b0100
		7,	// 5 => 0b0101
		-1, // 6 => 0b0110
		-1, // 7 => 0b0111
		4,	// 8 => 0b1000
		-1, // 9 => 0b1001
		8,	// 10 => 0b1010
		-1, // 11 => 0b1011
		6,	// 12 => 0b1100
		-1, // 13 => 0b1101
		-1, // 14 => 0b1110
		9,	// 15 => 0b1111
	};

	//
	// Collects all leaf nodes in given node recursively at given direction.
	// There are 5 cases (10 in detail):
	//      0 | 1
	//     ---+---
	//      2 | 3
	// 1. node has none children, it's a leaf node.
	// 2. node has 1 non-null child, (0---,-1--, --2- or ---3).
	// 3. node has 2 non-null children, horizonal layout (01-- or 23--).
	// 4. node has 3 non-null children, vertical layout (0-2- or -1-3)
	// 5. node has 4 non-null children (0123)
	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::GetLeafNodesAtDirection(NodeT* node, int direction,
		VisitorT& visitor) const
	{
		if (node->isLeaf)
		{
			visitor(node);
			return;
		}
		// layout flag of children inside this node.
		int mask = 0;
		if (node->children[0] != nullptr)
			mask |= 0b0001;
		if (node->children[1] != nullptr)
			mask |= 0b0010;
		if (node->children[2] != nullptr)
			mask |= 0b0100;
		if (node->children[3] != nullptr)
			mask |= 0b1000;

		int flag = GET_LEAF_NODES_AT_DIRECTION_MASK_TO_FLAG_TABLE[mask];
		if (flag == -1)
			return;

		// the children to go down, (at most 2)
		const auto& t = GET_LEAF_NODES_AT_DIRECTION_JUMP_TABLE[flag][direction];
		if (t[0] != -1)
			GetLeafNodesAtDirection(node->children[t[0]], direction, visitor);
		if (t[1] != -1)
			GetLeafNodesAtDirection(node->children[t[1]], direction, visitor);
	}

	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::FindNeighbourLeafNodes(NodeT* node, int direction,
		VisitorT& visitor) const
	{
		if (direction >= 4)
			return FindNeighbourLeafNodesDiagonal(node, direction, visitor);
		return FindNeighbourLeafNodesHV(node, direction, visitor); // NEWS
	}

	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::ForceSyncLeafNode(NodeT* leafNode)
	{
		if (leafNode == nullptr)
			return;
		// Is it still exist?
		auto id = Pack(leafNode->d, leafNode->x1, leafNode->y1, w, h);
		if (m.find(id) == m.end())
			return;
		// only one will happen.
		TryMergeUp(leafNode) || TrySplitDown(leafNode);
	}

	template <typename Object, typename ObjectKeyHasher>
	void Quadtree<Object, ObjectKeyHasher>::BatchAddToLeafNode(
		NodeT* leafNode, const std::vector<BatchOperationItemT>& items)
	{
		if (leafNode == nullptr || !leafNode->isLeaf)
			return;

		int numAdded = 0;

		for (const auto& [x, y, o] : items)
		{
			if (!(x >= leafNode->x1 && x <= leafNode->x2 && y >= leafNode->y1 && y <= leafNode->y2))
				continue;
			leafNode->objects.insert({ x, y, o });
			++numAdded;
			++numObjects;
		}

		if (numAdded)
		{
			TrySplitDown(leafNode) || TryMergeUp(leafNode);
		}
	}

} // namespace Quadtree

#endif
